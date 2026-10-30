/*
 * hakuX native core — library code that waits on emulated hardware.
 *
 * DirectSound hands the APU's GP DSP a command through a mailbox in memory
 * and spins until the DSP takes it:
 *
 *     add eax, 0x800
 *     cmp dword [eax+0x10], 0     ; the previous command still pending
 *     jne $-6
 *
 * The emulated GP runs a slice of its program per audio frame unless a CPU
 * waits on it (mcpx_apu_spin_wait: frames then run to completion), so the
 * DSP reaches its command check only every ~40 ms, and the spinning thread
 * (PGR2's audio thread, above the main thread's priority) holds the CPU all
 * that time. QEMU's TCG recognises the spin and tells the APU; here the loop
 * jumps to a stub that asks the kernel (NC_KORDINAL_APU_SPIN) to hurry the
 * APU and wait for the mailbox, the CPU's thread blocked on the host rather
 * than spinning in translated code. The library is the same in every title
 * seen (one copy, in DSOUND). debug.hakux.apuspin=0 turns it off.
 */
#include "native/kernel/kernel.h"
#include "native/dev/dev.h"

#include <string.h>

#include <sys/system_properties.h>
#include "hw/xbox/hle/prop.h"

#include "hw/xbox/hle/core/xbe.h"

static const uint8_t k_mailbox_wait[] = {0x05, 0x00, 0x08, 0x00, 0x00,  /* add eax, 0x800 */
                                         0x83, 0x78, 0x10, 0x00,        /* cmp dword [eax+0x10], 0 */
                                         0x75, 0xFA};                   /* jne the cmp */
#define LOOP_OFF 5 /* the cmp */

/* D3D8 CDevice::Init push-buffer idle checks after KickOff:
 *   test edx, 0x0FFFFFFF       ; F7 C2 FF FF FF 0F (Ninja Gaiden, NFS MW, Halo)
 *   test ecx, 0x0FFFFFFF       ; F7 C1 FF FF FF 0F (Halo secondary KickOff)
 * Following instruction:
 *   je  +offset                ; 74 ?? -> patched to EB ?? (jmp)
 *   pop reg; je +offset        ; 58..5F 74 ?? -> pop kept, patched to pop reg; EB ?? (jmp)
 */
static const uint8_t k_d3d_init_pb_wait_edx[] = {
    0xF7, 0xC2, 0xFF, 0xFF, 0xFF, 0x0F
};
static const uint8_t k_d3d_init_pb_wait_ecx[] = {
    0xF7, 0xC1, 0xFF, 0xFF, 0xFF, 0x0F
};

/* D3D::BlockOnTime push-buffer fence wait:
 *   cmp eax, 0x2000            ; 3D 00 20 00 00
 *   jae +0x??                  ; 73 ?? -> patched to 90 90 (nop nop)
 *   mov eax, 0x040100          ; B8 00 01 04 00
 */
static const uint8_t k_d3d_block_on_time_prefix[] = {
    0x3D, 0x00, 0x20, 0x00, 0x00,
    0x73
};
#define D3D_BOT_JAE_OFF 5

static bool apu_spin_on(void)
{
    char v[PROP_VALUE_MAX] = "";
    hle_prop_get("debug.hakux.apuspin", v);
    return strcmp(v, "0") != 0;
}

/* The stub for the loop at va (its cmp; 6 bytes):
 *   L: cmp dword [eax+0x10], 0 ; je out ; int 3Fh APU_SPIN ; jmp L
 *   out: jmp va+6 */
static uint32_t mailbox_stub(NcKernel *k, uint32_t va)
{
    uint8_t b[32];
    unsigned n = 0;
    b[n++] = 0x83, b[n++] = 0x78, b[n++] = 0x10, b[n++] = 0x00;
    b[n++] = 0x74, b[n++] = 6; /* je out */
    b[n++] = 0xCD, b[n++] = NC_KCALL_VECTOR;
    b[n++] = NC_KORDINAL_APU_SPIN & 0xFF, b[n++] = NC_KORDINAL_APU_SPIN >> 8;
    b[n++] = 0xEB, b[n++] = (uint8_t)-12; /* jmp L (from offset 12) */
    const unsigned out = n;                /* 12: the je's target */
    const uint32_t stub = nc_kernel_code_alloc(k, out + 5);
    if (!stub) {
        return 0;
    }
    b[n++] = 0xE9;
    const uint32_t rel = (va + 6) - (stub + out + 5);
    memcpy(b + n, &rel, 4);
    n += 4;
    memcpy(nc_vm_host(k->vm, stub), b, n);
    return stub;
}

static bool section_mapped(const NcVm *vm, uint32_t va, uint32_t len)
{
    for (uint32_t p = va & ~(NC_PAGE - 1); p < va + len; p += NC_PAGE) {
        if (nc_vm_phys(vm, p) == UINT32_MAX) {
            return false;
        }
    }
    return true;
}

void nc_kernel_title_fixups(NcKernel *k, const HleXbeInfo *xbe)
{
    if (!apu_spin_on()) {
        nc_log("hakuX-native", "fixups: DirectSound mailbox waits left spinning (debug.hakux.apuspin=0)");
        return;
    }
    unsigned patched = 0;
    for (unsigned i = 0; i < xbe->nsections; i++) {
        const HleXbeSection *s = &xbe->sections[i];
        const uint32_t scan_len = s->raw_size ? s->raw_size : s->vsize;
        if (!scan_len || !section_mapped(k->vm, s->va, scan_len)) {
            continue; /* not loaded at boot */
        }
        const uint8_t *base = nc_vm_host(k->vm, s->va);
        const uint8_t *p = base, *end = base + scan_len;
        while ((p = memmem(p, (size_t)(end - p), k_mailbox_wait, sizeof(k_mailbox_wait))) != NULL) {
            const uint32_t va = s->va + (uint32_t)(p - base) + LOOP_OFF;
            const uint32_t stub = mailbox_stub(k, va);
            if (stub) {
                uint8_t *site = nc_vm_host(k->vm, va);
                const uint32_t rel = stub - (va + 5);
                site[0] = 0xE9;
                memcpy(site + 1, &rel, 4);
                site[5] = 0x90;
                patched++;
                nc_log("hakuX-native", "fixups: DirectSound mailbox wait at %08x (%s) waits on the APU (stub %08x)",
                       va, s->name, stub);
            }
            p += sizeof(k_mailbox_wait);
        }
        const uint8_t *needles[] = {k_d3d_init_pb_wait_edx, k_d3d_init_pb_wait_ecx};
        for (unsigned n = 0; n < sizeof(needles) / sizeof(needles[0]); n++) {
            const uint8_t *dp = base;
            while ((dp = memmem(dp, (size_t)(end - dp), needles[n], 6)) != NULL) {
                uint32_t off = 6;
                if (dp + off < end && dp[off] >= 0x58 && dp[off] <= 0x5F) {
                    off++; /* preserve pop reg instruction (e.g. pop edi in Halo) */
                }
                if (dp + off < end && dp[off] == 0x74) {
                    const uint32_t va = s->va + (uint32_t)(dp - base) + off;
                    uint8_t *site = nc_vm_host(k->vm, va);
                    site[0] = 0xEB; /* je -> jmp */
                    nc_log("hakuX-native", "fixups: D3D CDevice::Init push-buffer wait bypassed at %08x (%s)", va, s->name);
                }
                dp += 6;
            }
        }
        const uint8_t *bp = base;
        while ((bp = memmem(bp, (size_t)(end - bp), k_d3d_block_on_time_prefix, sizeof(k_d3d_block_on_time_prefix))) != NULL) {
            const uint32_t va = s->va + (uint32_t)(bp - base) + D3D_BOT_JAE_OFF;
            uint8_t *site = nc_vm_host(k->vm, va);
            site[0] = 0x90; /* jae -> nop nop */
            site[1] = 0x90;
            nc_log("hakuX-native", "fixups: D3D BlockOnTime push-buffer wait bypassed at %08x (%s)", va, s->name);
            bp += sizeof(k_d3d_block_on_time_prefix);
        }
    }
    if (!patched) {
        nc_log("hakuX-native", "fixups: no DirectSound mailbox wait found");
    }
}

void nc_kernel_apu_spin(NcKernel *k, NcFrame *f)
{
    const uint32_t word = nc_frame_gpr(f, NC_EAX) + 0x10;
    if (nc_vm_phys(k->vm, word) != UINT32_MAX) {
        nc_dev_apu_spin_wait(nc_vm_host(k->vm, word));
    }
    k->apu_spins++;
    nc_frame_set_eip(f, nc_frame_eip(f) + 4); /* past the int: the stub checks again */
}

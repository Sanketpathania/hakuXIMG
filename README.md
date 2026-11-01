# hakuX (HakuxPVR)

> [!NOTE]
> **Experimental PowerVR GPU Support**: This fork adds experimental support for Imagination PowerVR GPUs (including the Google Pixel 10 Pro XL / Google Tensor G5 `laguna`), custom Tile-Based Deferred Rendering (TBDR) Vulkan pipeline quirks, and generic Direct3D push-buffer fence deadlock fixes.

hakuX runs original Xbox games on Android (arm64), with support for Adreno and experimental PowerVR GPUs.

Its core runs the game's x86 code on a JIT (a FEX fork) under a
reimplementation of the Xbox kernel, and draws by decoding the push buffer
the game's own Direct3D sends the NV2A into Vulkan. Audio is the MCPX APU,
ported from xemu.

## hakuX is free

hakuX is free, and always will be. The only official download is
[hakuX's GitHub releases](https://github.com/rfandango/hakuX/releases).

If you paid for hakuX in any way, shape or form, you were scammed. That
includes a store purchase, a subscription, an in-app unlock, a "premium"
build, or an app that repackages it. Ask for a refund wherever you paid, and
get hakuX here for free.

## Requirements

- An arm64 Android device with Vulkan:
  - Adreno GPUs (with custom Turnip driver support)
  - **Imagination PowerVR GPUs** (including Google Pixel 10 Pro XL)
- Your own game images (ISO or XISO), dumped from discs you own.

No BIOS, MCPX ROM or hard disk image is needed. hakuX doesn't include any
copyrighted Xbox software.

## Building

See [android/README.md](android/README.md).

## License

hakuX is free software under the GNU General Public License, version 3 or
later ([COPYING](COPYING)): anyone who distributes it, or a work based on it,
must make the complete source available under the same license. The code
ported from xemu (the MCPX APU) and the third-party components keep their
own licenses, listed in [LICENSE](LICENSE) and [licenses/](licenses/).

## Notes

- hakuX is not affiliated with Microsoft.

# PS5 stack check, 2026-10-14

These repositories were checked while building the GTA IV title. Each was built
from a clean clone on Ubuntu 24.04 with clang 20, at the revision named below.
Nothing was run on a console. "Builds" means it compiled and linked here.

## The chain the GTA IV title builds with

| Repository | Revision | Result |
| --- | --- | --- |
| KongaTime/PS5_PayloadSDK | `1de8b37` (main) | **Builds.** The template's pin installs. |
| KongaTime/PS5_Vulkan | main | **Builds**, with one problem. The native tool and `libc.prx` build, and `libc.prx` matches its pinned hash. **Problem:** `tools/setup-native-dependencies.sh` pins payload SDK `b83202b`, which exists only in mihawk-99/PS5_PayloadSDK, not in KongaTime's fork, so a bootstrap from KongaTime's repositories alone fails. Fetching that commit from mihawk-99 fixes it. Its own Hello World app then fails to link (`ps5_fp_ieee` undefined, though `libps5platform.a` defines it). Titles don't use that app. |
| KongaTime/PS5_Mesa | `7b59ef2` (PS5_Vulkan's pin) | **Builds.** The RADV release archive (`libvulkan_radeon.ps5.a`, 274 MB) builds. It needs meson 1.4 or later; Ubuntu 24.04 ships 1.3.2. |
| KongaTime/rexglue-sdk | `d3ddb0b` (main) | **Builds.** `rexruntime` compiles for the PS5. The template's `tools/build-rexglue.sh` then reports 3 unresolved imports and exits 1: `sceSystemServiceHideSplashScreen`, `vk_icdGetInstanceProcAddr` and `wsi_videoout_set_flip_rate`. These are false positives: the check's module list predates linking the GPU (RADV) into the title, and the template's test app links with them resolved. |
| KongaTime/PS5_ReXGlueTemplate | main | **Builds.** The test app (`PPSA99001`, 27 MB) builds against rexglue-sdk. Its `tools/stage-notices.py` can't read a ReXGlue SDK that is a folder inside another repository; this repository's `ps5/patches/ps5-rexglue-template-notices.patch` fixes that. |
| KongaTime/LibertyRecomp | this branch | **Builds.** The GTA IV title (`PPSA99400`, `eboot.bin` 147 MB) builds, links and signs (`ps5/build.sh`). |

Two problems came from this build machine's network, not from the
repositories: zlib.net and Meson's WrapDB were blocked. The same files came from
their GitHub mirrors, and their hashes matched the pins.

## Looked at, not built

These need things this check didn't have, or are large builds unrelated to the
GTA IV title.

| Repository | What it is | Why it wasn't built |
| --- | --- | --- |
| PS5_COD3, CoD3Recomp | Call of Duty 3 (XenonRecomp) on PS5 and PC | Its recompiled code comes from the game's files, which aren't committed |
| PS5_EdgeOfTimeTitle, EdgeOfTimeRecomp | Spider-Man: Edge of Time on PS5 and PC | Same: its code comes from `rexglue codegen` on the game's files |
| TheSimpsonsGame-PS5-Builder | The Simpsons Game on PS5 | The first ReXGlue game on the PS5. Its pad driver was compared with the fork's. |
| PS5_RPCS3, PS5_RPCS3Title | RPCS3 as a PS5 title | Large build, unrelated to GTA IV |
| PS5_MihawkGen | Neural frame generation | Unrelated to GTA IV |
| PS5_Dusklight, ps5-opengl, PS5_VulkanTemplate, prospero-win | Other PS5 ports and SDKs | Unrelated to GTA IV |
| SpaceRanger, UnleashedRecomp | N64 and Xbox 360 recompilations for PC | No PS5 target |

## LibertyRecomp's own repository

- `tools/XenosRecomp/thirdparty/dxc-bin` ships `dxc-linux` without
  `libdxcompiler.so`. On Linux the native renderer's shader overrides need a DXC
  release's `lib` folder (`DXC_LIBRARY_PATH`).
- The newer app, `gta4-recomp`, is built only on macOS. On Linux and Windows the
  build compiles the older `LibertyRecomp/` app. The PS5 title uses
  `gta4-recomp` and its `gta4-native` renderer.
- The PS4 target (`toolchains/ps4-openorbis.cmake`) describes "Vulkan via
  OpenOrbis SDL-PS4", but the PS4 has no Vulkan, so it can't draw as written.

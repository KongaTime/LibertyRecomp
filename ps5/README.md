# Liberty Recompiled on the PS5

> [!CAUTION]
> Experimental. This builds a PS5 homebrew title from Liberty Recompiled. It has
> not been run on a console yet: treat the first launches as bring-up, and read
> `liberty-results.txt` and `logs/` in the title folder after each one.

The title (`PPSA99400`, **Liberty Recompiled**) runs the same recompiled GTA IV
code as the desktop app. It draws through the native Vulkan renderer
(`gta4-native`) on RADV, Mesa's Vulkan driver for the console. Sound goes out
through `sceAudioOut` and the DualSense is read through `scePad`.

It is built with the [PS5 ReXGlue Template](https://github.com/KongaTime/PS5_ReXGlueTemplate).
This folder is one of its game folders. The ReXGlue runtime it links is this
repository's own SDK, `glue/rexglue-sdk-main`, with the PS5 target of
[KongaTime/rexglue-sdk](https://github.com/KongaTime/rexglue-sdk) merged in
(`glue/rexglue-sdk-main/ps5/PORTED.md`).

## What you need

- **To build:** a Linux PC with clang 20 or later and lld, CMake 3.29 or later,
  Ninja, meson 1.4 or later and Python 3.
  - Set up the PS5 ReXGlue Template once (its `tools/bootstrap.sh`), beside this
    repository: `../ps5/PS5_ReXGlueTemplate`, or point `PS5_REXGLUE_TEMPLATE` at
    it.
  - This build does not need the template's `tools/build-rexglue.sh`.
- **To play:**
  - a PS5 that runs homebrew (etaHEN), with ShadowMountPlus and an FTP server;
  - your own Xbox 360 copy of GTA IV, installed with Liberty Recompiled on a PC.

## Build

```bash
DXC_LIBRARY_PATH=/path/to/dxc/lib ps5/build.sh
```

- `DXC_LIBRARY_PATH` is the `lib` folder of a Linux
  [DirectXShaderCompiler release](https://github.com/microsoft/DirectXShaderCompiler/releases).
  The renderer's shader overrides are compiled with `tools/XenosRecomp`'s
  `dxc-linux`, and that submodule does not carry the Linux `libdxcompiler.so`.
- The build makes the runtime in `build/ps5-rexglue`, then the title in the
  template's `build/title-ps5`. The title folder lands in the template's
  `dist/PPSA99400/`.

## Install

1. Copy `dist/PPSA99400/` to `/data/homebrew/` on the console.
2. Copy the contents of the PC install's `LibertyRecomp/game` folder into
   `/data/homebrew/PPSA99400/gamedata/`, and its `dlc` folder (if you have the
   episodes) into `gamedata/dlc/`.
3. Launch **Liberty Recompiled** from the home screen.

## How it differs from the desktop app

| | Desktop | PS5 |
| --- | --- | --- |
| Game files | installed by the app from a disc image | `gamedata/`, copied from a PC install; no installer |
| Renderer | `gta4-native` (Vulkan) or `gta4-metal`, loaded as a plugin | `gta4-native`, linked in (`host/gpu_static_ps5.cpp`) |
| Online, voice chat | community backend (curl, OpenSSL), SDL audio | offline (`host/platform_services_ps5.cpp`) |
| Achievements, User Music | Game Center, AVFoundation (macOS) | none |
| Button prompts | from the connected pad | the DualSense's |
| Saves, settings, logs | the user folder | the title folder (`userdata/`, `logs/`) |

## Known gaps

- Nothing has run on a console yet.
- The runtime's emulated Xenos GPU plugin is not built for the PS5. GTA IV uses
  `gta4-native`, which does not need it.
- Online multiplayer, voice and text chat are off.

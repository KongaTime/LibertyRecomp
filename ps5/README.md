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
  - Apply `ps5/patches/ps5-rexglue-template-notices.patch` to the template once
    (`git -C <template> apply ...`). Its notices step otherwise expects the
    ReXGlue SDK to be a repository of its own, and this one is a folder in this
    repository. `ps5/build.sh` checks for it.
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

You need your own Xbox 360 copy of GTA IV, **USA, version 1.00**, and its **v8
title update**. The title checks both and refuses anything else.

1. Copy `dist/PPSA99400/` to `/data/homebrew/` on the console.
2. Put your game in the title's folder (`gamedata/README.txt` there says the same):
   - `gamedata/`: the files extract-xiso extracted from your disc (`default.xex`,
     `common.rpf`, `xbox360.rpf`, `audio.rpf` and the rest), or the `.iso` itself.
   - `title-update/`: the v8 title update. That's the TU file from your Xbox
     360's hard drive (`Content/0000000000000000/545407F2/000B0000/`), or its
     `default.xexp`.
   - `episodes/TLAD/`, `episodes/TBOGT/`: optional, the episodes' files.
3. Launch **Liberty Recompiled**. On the first launch it unpacks the game into
   `game/`, which takes several minutes and needs about twice the game's size
   free. The PC app's installer does the same. Later launches start the game
   directly, and `gamedata/` and `title-update/` can then be emptied.

A game folder already installed by Liberty Recompiled on a PC (`LibertyRecomp/game`)
can go in `gamedata/` instead. It's used as it is.

## How it differs from the desktop app

| | Desktop | PS5 |
| --- | --- | --- |
| Game files | installed by the app from a disc image | installed by the title on its first launch, from the files in its folder |
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

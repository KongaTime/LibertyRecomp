#!/usr/bin/env bash
# Liberty Recompiled on the PS5 - build the title folder, dist/PPSA99400/ in the
# PS5 ReXGlue Template.
#
#   ps5/build.sh
#
# Needs the PS5 ReXGlue Template with its stack set up once (its
# tools/bootstrap.sh: PS5_Vulkan with the RADV release archive, the payload SDK
# fork), at PS5_REXGLUE_TEMPLATE or beside this repository
# (../ps5/PS5_ReXGlueTemplate or ../PS5_ReXGlueTemplate). The ReXGlue runtime is
# not the template's rexglue-sdk checkout: it is this repository's SDK,
# glue/rexglue-sdk-main, which carries the GTA IV renderer, built here into
# build/ps5-rexglue.
#
#   DXC_LIBRARY_PATH  the folder with libdxcompiler.so for tools/XenosRecomp's
#                     dxc-linux, which compiles the renderer's shader overrides
#                     (the submodule carries no Linux library)
#   PS5_CLANG         the host clang 20 or later the SDK's wrappers drive
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
for candidate in "${PS5_REXGLUE_TEMPLATE:-}" "$root/../ps5/PS5_ReXGlueTemplate" "$root/../PS5_ReXGlueTemplate"; do
    if [[ -n $candidate && -f $candidate/cmake/rexglue-ps5.cmake ]]; then
        template=$(cd -- "$candidate" && pwd)
        break
    fi
done
[[ -n ${template:-} ]] || { echo "no PS5 ReXGlue Template: clone KongaTime/PS5_ReXGlueTemplate beside" \
    "this repository, or set PS5_REXGLUE_TEMPLATE" >&2; exit 2; }
# The template's notices read this repository's SDK, a folder inside it: its
# tools/stage-notices.py needs ps5/patches/ps5-rexglue-template-notices.patch
grep -q 'f"{rev}:./{path}"' "$template/tools/stage-notices.py" || {
    echo "the template's tools/stage-notices.py cannot read an SDK inside a repository: run" >&2
    echo "  git -C $template apply $root/ps5/patches/ps5-rexglue-template-notices.patch" >&2
    exit 2; }
sdk=$(bash "$template/tools/setup-sdk.sh")
rexglue="$root/glue/rexglue-sdk-main"
build="$root/build/ps5-rexglue"
ninja=$(command -v ninja || echo "$HOME/.local/bin/ninja")
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}

step() { printf '==> %s\n' "$*"; }

# The SDK's submodules and the dependency patches the desktop build applies
step "the SDK's sources"
for submodule in cli11 libmspack FFmpeg tomlplusplus simde xxHash spdlog fmt utfcpp imgui sdl3 o1heap \
        vulkan-headers vulkan-memory-allocator spirv-headers spirv-tools glslang snappy inja volk; do
    [[ -e $rexglue/thirdparty/$submodule/.git ]] ||
        git -C "$root" submodule update --init --depth 1 "glue/rexglue-sdk-main/thirdparty/$submodule"
done
for submodule in smol-v zstd xxHash dxc-bin; do
    [[ -e $root/tools/XenosRecomp/thirdparty/$submodule/.git ]] ||
        git -C "$root/tools/XenosRecomp" submodule update --init --depth 1 "thirdparty/$submodule"
done
python3 "$root/tools/setup_repo.py" --root "$root" --patch-directory "$root/cmake/dependency-patches" \
    --prepare-only --only libmspack-xmem

# The runtime and the GTA IV renderer, for the console
if [[ ! -f $build/build.ninja ]]; then
    step "configuring the runtime (glue/rexglue-sdk-main) for the PS5"
    mkdir -p "$build"
    flags="-march=znver2 -fno-omit-frame-pointer"
    cmake -S "$rexglue" -B "$build" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" \
        -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" -DREXGLUE_ENABLE_TRACY=OFF \
        -DREXGLUE_BUILD_TESTS=OFF -DLIBERTY_RECOMP_WORKSPACE_ROOT="$root" \
        ${DXC_LIBRARY_PATH:+-DGTA4_NATIVE_DXC_LIBRARY_PATH="$DXC_LIBRARY_PATH"} \
        > "$build/configure.log" 2>&1 || { tail -40 "$build/configure.log" >&2; exit 1; }
fi
step "building the runtime and the gta4-native renderer"
"$ninja" -C "$build" rexruntime rexgpu-gta4-native

# The title, with the template
step "building the title"
REXGLUE_SRC="$rexglue" REXGLUE_PS5_BUILD="$build" GAME="$root/ps5" bash "$template/tools/build.sh"

# What the title reads beside eboot.bin: the DualSense's button prompts, the
# native renderer's font atlases, and the key the RPF installer reads with
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' \
    "$root/ps5/sce_sys/param.json")
app="$template/dist/$title_id"
step "staging the title's files in $app"
mkdir -p "$app/button_prompts" "$app/font_atlases"
rsync -a --delete "$root/LibertyRecompLib/private/button_prompts/ps5/" "$app/button_prompts/ps5/"
for episode in "" tlad/ tbogt/; do
    mkdir -p "$app/font_atlases/$episode"
    cp "$root/LibertyRecompLib/font_atlases/$episode"{font1.png,font2.png,font3.png,manifest.json} \
        "$app/font_atlases/$episode"
done
[[ -f $root/LibertyRecompLib/aes_key.bin ]] && cp "$root/LibertyRecompLib/aes_key.bin" "$app/aes_key.bin"
mkdir -p "$app/gamedata" "$app/title-update" "$app/episodes"
cp "$root/ps5/gamedata-note.txt" "$app/gamedata/README.txt"
cp "$root/ps5/gamedata-note.txt" "$app/title-update/README.txt"
step "$title_id ready: copy $app to /data/homebrew/ on the console, with your game files in its gamedata/"

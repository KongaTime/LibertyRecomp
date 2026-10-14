/*
 * Liberty Recompiled on the PS5 - the native renderer, linked into the title.
 *
 * The desktop loads gta4-native as a shared library (librexgpu-gta4-native)
 * and looks up its two entry points there. A title cannot load code of its
 * own that way, so the plugin is linked in (src/graphics/CMakeLists.txt names
 * its entry points rex_gpu_gta4_native_*), and this registers them under the
 * plugin's file name, where LoadGpuPlugin's DynamicLibrary finds them.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstdint>

#include <rex/platform/dynlib.h>
#include <rex/system/gpu_plugin.h>

extern "C" uint32_t rex_gpu_gta4_native_abi_version(void);
extern "C" rex::system::IGraphicsSystem* rex_gpu_gta4_native_create(
    uint32_t abi_version, const rex::system::GpuCreateInfo* info);

namespace {
const bool registered = [] {
  static const rex::platform::StaticSymbol symbols[] = {
      {rex::system::kGpuAbiVersionSymbol,
       reinterpret_cast<void*>(&rex_gpu_gta4_native_abi_version)},
      {rex::system::kGpuCreateSymbol, reinterpret_cast<void*>(&rex_gpu_gta4_native_create)},
  };
  rex::platform::RegisterStaticLibrary("librexgpu-gta4-native.so", symbols, 2);
  return true;
}();
}  // namespace

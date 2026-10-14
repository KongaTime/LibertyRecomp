# Liberty Recompiled on the PS5 - the game, as the PS5 ReXGlue Template builds it.
#
#   ps5/build.sh
#
# This folder is a game folder of KongaTime/PS5_ReXGlueTemplate (game.cmake,
# sce_sys/, notices.json). ps5/build.sh builds the ReXGlue runtime from this
# repository's SDK (glue/rexglue-sdk-main, with its PS5 target) and then the
# title with the template, which includes this file. It compiles, as the
# desktop app does (glue/rexglue-sdk-main/gta4-recomp/CMakeLists.txt):
#
#   - the game's recompiled code, gta4-recomp/generated (gta4_recomp);
#   - the app, gta4-recomp/src (gta4_app), less what only the desktop has: the
#     Objective-C++ files (Game Center, User Music, the microphone), the SDL
#     install dialog and the community backend and voice audio (curl, OpenSSL,
#     SDL), which ps5/host/platform_services_ps5.cpp stands in for;
#   - ps5/host/gpu_static_ps5.cpp, which registers the gta4-native renderer the
#     runtime build linked statically (out/ps5-amd64/librexgpu-gta4-native.a).
#
# The game's files are not in the title: players copy the game folder of a PC
# install into the title's gamedata/ folder (ps5/README.md).
#
# SPDX-License-Identifier: GPL-3.0-or-later

get_filename_component(LIBERTY_ROOT "${TITLE_DIR}/.." ABSOLUTE)
set(gta4 ${REXGLUE_SRC}/gta4-recomp)
if(NOT EXISTS ${gta4}/generated/sources.cmake)
	message(FATAL_ERROR "No recompiled code at ${gta4}/generated (REXGLUE_SRC must be this repository's "
		"glue/rexglue-sdk-main; ps5/build.sh sets it)")
endif()
if(NOT EXISTS ${rex_runtime_dir}/librexgpu-gta4-native.a)
	message(FATAL_ERROR "No gta4-native renderer at ${rex_runtime_dir}: run ps5/build.sh, which builds it")
endif()

set(TITLE_RESULTS_FILE /app0/liberty-results.txt)

# What the SDK compiles everything with (its CMakeLists.txt add_compile_options
# and rexui's public definitions), besides the template's REX_OPTIONS
set(gta4_options -ffp-model=strict -fno-char8_t -mcmodel=large -msse4.1 -Wno-everything)
set(gta4_definitions REX_HAS_FIDELITYFX_FSR1=1)
set(gta4_includes ${gta4}/src ${gta4}/generated ${REXGLUE_SRC}/src ${REXGLUE_SRC}/thirdparty/crypto
	${REXGLUE_SRC}/thirdparty/inja/third_party/include ${REXGLUE_SRC}/thirdparty/vulkan-headers/include
	${REXGLUE_SRC}/thirdparty/volk ${REXGLUE_SRC}/thirdparty/vulkan-memory-allocator/include)

# --- The recompiled code ----------------------------------------------------------
rex_ps5_module(gta4_recomp GENERATED ${gta4}/generated PCH ${gta4}/generated/gta4_init.h
	INCLUDES ${gta4_includes})
target_compile_options(gta4_recomp PRIVATE ${gta4_options})
target_compile_definitions(gta4_recomp PRIVATE ${gta4_definitions})
# As on the desktop: the start-up registrar is one very large straight-line
# function, which optimizing takes the most memory of anything here
set_source_files_properties(${gta4}/generated/gta4_register.cpp PROPERTIES COMPILE_OPTIONS -O0)

# --- The app ----------------------------------------------------------------------
set(gta4_app_sources
	main.cpp gta4_app.cpp gta4_frontend_hooks.cpp gta4_font_selection_trace.cpp
	gta4_presentation_options.cpp gta4_presentation_hooks.cpp gta4_aspect_hooks.cpp
	gta4_quicksave_hooks.cpp gta4_input_hooks.cpp gta4_sony_feedback.cpp gta4_sony_event_hooks.cpp
	gta4_loading_parallax_fix.cpp gta4_motion_bridge.cpp gta4_motion_hooks.cpp
	gta4_motion_vehicle_hooks.cpp gta4_audio_lifetime_trace.cpp gta4_audio_ramped_mix.cpp
	gta4_lzx_hooks.cpp gta4_multiplayer_64_hooks.cpp gta4_online_hooks.cpp gta4_native_hooks.cpp
	gta4_population_hooks.cpp gta4_streaming_diagnostics.cpp gta4_streaming_hooks.cpp
	gta4_touch_coordinator.cpp gta4_transition_hooks.cpp install/gta4_installer.cpp
	install/gta4_rpf_extractor.cpp install/gta4_source_inspector.cpp legal_screen_trace.cpp
	rpf_button_prompts.cpp input/text_chat_dialog.cpp input/context_touch_controls.cpp
	input/context_touch_context.cpp input/context_touch_camera_hooks.cpp
	input/context_touch_settings.cpp input/context_touch_draw.cpp input/context_touch_layout.cpp
	input/context_touch_activity.cpp input/context_touch_activity_context.cpp
	input/context_touch_activity_layout.cpp input/context_touch_overlay.cpp gta4_touch_guest_hooks.cpp)
list(TRANSFORM gta4_app_sources PREPEND ${gta4}/src/)
add_library(gta4_app OBJECT ${gta4_app_sources} ${REXGLUE_SRC}/thirdparty/crypto/rijndael-alg-fst.c
	${TITLE_DIR}/host/platform_services_ps5.cpp ${TITLE_DIR}/host/gpu_static_ps5.cpp ${REX_APP_SOURCE})
target_include_directories(gta4_app PRIVATE ${gta4_includes})
rex_ps5_settings(gta4_app)
target_compile_options(gta4_app PRIVATE ${gta4_options})
target_compile_definitions(gta4_app PRIVATE ${gta4_definitions} GTA4_TOUCH_PRIMARY_PLAYER_ALIAS=1
	GTA4_RECOMP_ASSET_XEX="/app0/gamedata/default.xex"
	GTA4_BUTTON_PROMPT_ASSET_ROOT="/app0/button_prompts"
	GTA4_RPF_AES_KEY_SOURCE="/app0/aes_key.bin")
set_source_files_properties(${gta4}/src/gta4_sony_feedback.cpp ${gta4}/src/gta4_sony_event_hooks.cpp
	PROPERTIES COMPILE_DEFINITIONS GTA4_SONY_PRIMARY_PLAYER_ALIAS)
# The C file builds as C, without the C++ options
set_source_files_properties(${REXGLUE_SRC}/thirdparty/crypto/rijndael-alg-fst.c PROPERTIES
	COMPILE_OPTIONS "-std=c11;-w")
rex_ps5_link_objects(gta4_app)

# --- Files beside eboot.bin ---------------------------------------------------------
# The button prompts and the fonts the native renderer draws with, from this
# repository, staged into the title folder once it is packaged (ps5/build.sh)

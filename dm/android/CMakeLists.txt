# Android 原生移植起点：设备探针与现有纹理代码，依据 NDK / Vulkan 接口。
# Android native port starting point: device probe and existing texture code, based on NDK / Vulkan APIs.
cmake_minimum_required(VERSION 3.22)
project(dm_android C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT ANDROID)
    enable_testing()
    option(DM_ANDROID_REWRITE_SCAN "Build host offline rewrite audit with Capstone 5" ON)
    if(DM_ANDROID_REWRITE_SCAN)
        find_path(DM_CAPSTONE_INCLUDE capstone/capstone.h PATHS /opt/homebrew/include REQUIRED)
        find_library(DM_CAPSTONE_LIBRARY capstone PATHS /opt/homebrew/lib REQUIRED)
        add_library(dm_android_decode STATIC rewrite/decode.c rewrite/jump_table.c rewrite/window_decode.c rewrite/exclusive.c rewrite/prepare.c)
        target_include_directories(dm_android_decode PUBLIC ${DM_CAPSTONE_INCLUDE})
        target_link_libraries(dm_android_decode PUBLIC ${DM_CAPSTONE_LIBRARY} dm_android_artifact)
        target_compile_options(dm_android_decode PRIVATE -Wall -Wextra -Werror)
        add_executable(dm_rewrite_scan rewrite/scan.c)
        target_link_libraries(dm_rewrite_scan PRIVATE dm_android_decode)
        target_compile_options(dm_rewrite_scan PRIVATE -Wall -Wextra -Werror)
        add_executable(dm_x18_decode_test tests/x18_decode.c)
        target_link_libraries(dm_x18_decode_test PRIVATE dm_android_decode)
        target_compile_options(dm_x18_decode_test PRIVATE -Wall -Wextra -Werror)
        add_test(NAME x18_decode COMMAND dm_x18_decode_test)
        add_test(NAME rewrite_scan_cli COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/rewrite_scan.sh
            $<TARGET_FILE:dm_rewrite_scan> $<TARGET_FILE:dm_x18_decode_test>)
        add_executable(dm_x18_decoded_test tests/x18_patch.c)
        target_compile_definitions(dm_x18_decoded_test PRIVATE DM_TEST_DECODE=1)
        target_compile_options(dm_x18_decoded_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_x18_decoded_test PRIVATE dm_android_decode dm_android_runtime dm_android_nso)
        add_test(NAME x18_decoded_execution COMMAND dm_x18_decoded_test)
        add_executable(dm_x18_decoded_regions_test tests/x18_regions.c)
        target_compile_definitions(dm_x18_decoded_regions_test PRIVATE DM_TEST_REGIONS=1)
        target_compile_options(dm_x18_decoded_regions_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_x18_decoded_regions_test PRIVATE dm_android_decode dm_android_runtime dm_android_nso)
        add_test(NAME x18_decoded_regions COMMAND dm_x18_decoded_regions_test)
        add_executable(dm_rewrite_prepare_test tests/rewrite_prepare.c)
        target_compile_options(dm_rewrite_prepare_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_rewrite_prepare_test PRIVATE dm_android_decode)
        add_test(NAME rewrite_preparation COMMAND dm_rewrite_prepare_test)
        add_executable(dm_exclusive_test tests/exclusive.c)
        target_compile_options(dm_exclusive_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_exclusive_test PRIVATE dm_android_decode)
        add_test(NAME exclusive_paths COMMAND dm_exclusive_test)
        set_tests_properties(x18_decode rewrite_scan_cli x18_decoded_execution x18_decoded_regions rewrite_preparation exclusive_paths PROPERTIES TIMEOUT 30)
    endif()
endif()
include(dependencies.cmake)
# 自有音频模块与既有运行时解耦；只新增子目录，不改现有 target。
# Owned audio module is decoupled from the existing runtime; additive subdirectory only.
add_subdirectory(audio)

add_library(dm_android_nso STATIC nso.c nso_file.c image_map.c module.c module_map.c)
target_link_libraries(dm_android_nso PUBLIC dm_nso_dependencies)
target_compile_options(dm_android_nso PRIVATE -Wall -Wextra -Werror)
add_executable(dm_nso_inspect nso_inspect.c)
target_link_libraries(dm_nso_inspect PRIVATE dm_android_nso)
target_compile_options(dm_nso_inspect PRIVATE -Wall -Wextra -Werror)
add_executable(dm_nso_test nso_test.c)
target_link_libraries(dm_nso_test PRIVATE dm_android_nso)
target_compile_options(dm_nso_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_image_test tests/image_map.c)
target_link_libraries(dm_image_test PRIVATE dm_android_nso)
target_compile_options(dm_image_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_module_inspect module_inspect.c)
target_link_libraries(dm_module_inspect PRIVATE dm_android_nso dm_android_runtime)
target_compile_options(dm_module_inspect PRIVATE -Wall -Wextra -Werror)
add_executable(dm_module_test tests/module.c)
target_link_libraries(dm_module_test PRIVATE dm_android_nso)
target_compile_options(dm_module_test PRIVATE -Wall -Wextra -Werror)

add_library(dm_android_tex STATIC ../gfx/nvn/nvn_tex.c ../gfx/tex/nvn_deswizzle.c)
if(NOT ANDROID)
    add_executable(dm_binding_cache_test tests/binding_cache_test.c)
    add_executable(dm_batch_policy_test tests/batch_policy_test.c)
    target_include_directories(dm_batch_policy_test PRIVATE native)
    target_compile_options(dm_batch_policy_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME batch_policy_host COMMAND dm_batch_policy_test)
    target_compile_options(dm_binding_cache_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME binding_cache_host COMMAND dm_binding_cache_test)
    add_executable(dm_compute_state_test tests/compute_state_test.c)
    target_compile_options(dm_compute_state_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME compute_state_host COMMAND dm_compute_state_test)
    add_executable(dm_descriptor_lookup_test tests/descriptor_lookup_test.c)
    target_compile_options(dm_descriptor_lookup_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME descriptor_lookup_host COMMAND dm_descriptor_lookup_test)
    add_executable(dm_frame_timing_test tests/frame_timing_test.c)
    target_include_directories(dm_frame_timing_test PRIVATE native)
    target_compile_options(dm_frame_timing_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME frame_timing_host COMMAND dm_frame_timing_test)
    add_executable(dm_debug_settings_test tests/debug_settings_test.c native/launch_settings.c)
    target_include_directories(dm_debug_settings_test PRIVATE native)
    target_compile_options(dm_debug_settings_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME debug_settings_host COMMAND dm_debug_settings_test)
    add_executable(dm_output_resolution_test tests/output_resolution_test.c native/launch_settings.c)
    target_include_directories(dm_output_resolution_test PRIVATE native)
    target_compile_options(dm_output_resolution_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME output_resolution_host COMMAND dm_output_resolution_test)
    add_executable(dm_vulkan_upload_test tests/vulkan_upload.c)
    target_include_directories(dm_vulkan_upload_test PRIVATE ../gfx/nvn/include)
    target_link_libraries(dm_vulkan_upload_test PRIVATE dm_android_tex)
    target_compile_options(dm_vulkan_upload_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME vulkan_upload_host COMMAND dm_vulkan_upload_test)
    add_executable(dm_pending_ranges_test tests/pending_ranges.c native/pending_ranges.c)
    target_compile_options(dm_pending_ranges_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME pending_ranges_host COMMAND dm_pending_ranges_test)
    add_executable(dm_performance_hint_test tests/performance_hint.c)
    target_include_directories(dm_performance_hint_test PRIVATE tests/performance-hint-stubs)
    target_compile_definitions(dm_performance_hint_test PRIVATE _POSIX_C_SOURCE=200809L _DARWIN_C_SOURCE)
    target_compile_options(dm_performance_hint_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_link_libraries(dm_performance_hint_test PRIVATE ${CMAKE_DL_LIBS} pthread)
    add_test(NAME performance_hint_host COMMAND dm_performance_hint_test)
    add_test(NAME performance_hint_disabled COMMAND dm_performance_hint_test disabled)
    add_test(NAME performance_hint_unavailable COMMAND dm_performance_hint_test unavailable)
    add_test(NAME performance_hint_unsupported COMMAND dm_performance_hint_test unsupported)
endif()
add_executable(dm_texture_test texture_test.c)
target_link_libraries(dm_texture_test PRIVATE dm_android_tex)
target_compile_options(dm_texture_test PRIVATE -Wall -Wextra -Werror)

find_package(Threads REQUIRED)
add_library(dm_android_runtime STATIC runtime/bridge.c runtime/context.c runtime/accounting.c runtime/heap.c runtime/memory.c runtime/svc.c runtime/event.c runtime/dispatch.c runtime/patch.c runtime/x18_patch.c runtime/x18_control.c runtime/x18_jump.c runtime/x18_window.c runtime/input.c runtime/romfs.c runtime/sync.c runtime/ipc.c runtime/sm.c runtime/lm.c runtime/lm_wire.c runtime/arbiter.c runtime/mutex.c)
target_link_libraries(dm_android_runtime PUBLIC Threads::Threads dm_android_nso)
target_compile_options(dm_android_runtime PRIVATE -Wall -Wextra -Werror)
add_library(dm_android_artifact STATIC artifact.c)
target_link_libraries(dm_android_artifact PUBLIC dm_android_runtime dm_android_nso)
target_compile_options(dm_android_artifact PRIVATE -Wall -Wextra -Werror)
add_executable(dm_artifact_inspect artifact_inspect.c)
target_link_libraries(dm_artifact_inspect PRIVATE dm_android_artifact)
target_compile_options(dm_artifact_inspect PRIVATE -Wall -Wextra -Werror)
add_executable(dm_artifact_test tests/artifact.c)
target_link_libraries(dm_artifact_test PRIVATE dm_android_artifact)
target_compile_options(dm_artifact_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_runtime_test tests/runtime.c)
target_link_libraries(dm_runtime_test PRIVATE dm_android_runtime)
target_compile_options(dm_runtime_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_system_patch_test tests/system_patch.c)
target_link_libraries(dm_system_patch_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_system_patch_test PRIVATE -Wall -Wextra -Werror)
# #4 独占监视器返回边界反例 | #4 exclusive-monitor return-boundary counterexample
add_executable(dm_exclusive_return_test tests/exclusive_return.c)
target_compile_options(dm_exclusive_return_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_x18_patch_test tests/x18_patch.c)
target_link_libraries(dm_x18_patch_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_x18_patch_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_x18_control_test tests/x18_control.c)
target_link_libraries(dm_x18_control_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_x18_control_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_x18_regions_test tests/x18_regions.c)
target_link_libraries(dm_x18_regions_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_x18_regions_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_input_test tests/input.c)
target_link_libraries(dm_input_test PRIVATE dm_android_runtime)
target_compile_options(dm_input_test PRIVATE -Wall -Wextra -Werror)
add_library(dm_android_touch_core STATIC ../ios/Sources/dm_touch_controls.c)
target_include_directories(dm_android_touch_core PRIVATE ../ios/Sources ../host)
target_compile_options(dm_android_touch_core PRIVATE -Wall -Wextra -Werror)
add_library(dm_android_touch STATIC runtime/touch_adapter.c)
target_include_directories(dm_android_touch PUBLIC runtime ../ios/Sources ../host)
target_link_libraries(dm_android_touch PUBLIC dm_android_touch_core)
target_compile_options(dm_android_touch PRIVATE -Wall -Wextra -Werror)
add_library(dm_android_touch_game STATIC runtime/touch_game_bridge.c)
target_include_directories(dm_android_touch_game PUBLIC runtime)
target_link_libraries(dm_android_touch_game PUBLIC dm_android_touch)
target_compile_options(dm_android_touch_game PRIVATE -Wall -Wextra -Werror)
add_executable(dm_touch_adapter_test tests/touch_adapter.c)
target_link_libraries(dm_touch_adapter_test PRIVATE dm_android_touch dm_android_runtime m)
target_compile_options(dm_touch_adapter_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME touch_adapter COMMAND dm_touch_adapter_test)
set_tests_properties(touch_adapter PROPERTIES TIMEOUT 30)
# #33 live touch owns the original core directly; this fixture exercises the
# belt request consumer and held-gesture quarantine through the production seam.
add_executable(dm_live_touch_belt_test tests/live_touch_belt_test.c native/live_touch.c)
target_link_libraries(dm_live_touch_belt_test PRIVATE dm_android_touch_core m)
target_include_directories(dm_live_touch_belt_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_belt_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_belt COMMAND dm_live_touch_belt_test)
set_tests_properties(live_touch_belt PROPERTIES TIMEOUT 30)
# #33 difficulty selector request consumer through the production live-touch seam.
add_executable(dm_live_touch_difficulty_test tests/live_touch_difficulty_test.c)
target_link_libraries(dm_live_touch_difficulty_test PRIVATE dm_android_touch_core m)
target_include_directories(dm_live_touch_difficulty_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_difficulty_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_difficulty COMMAND dm_live_touch_difficulty_test)
set_tests_properties(live_touch_difficulty PROPERTIES TIMEOUT 30)
# #255 portal action consumer through the real live-touch core.
add_executable(dm_live_touch_portal_test tests/live_touch_portal_test.c)
target_link_libraries(dm_live_touch_portal_test PRIVATE dm_android_touch_core m)
target_include_directories(dm_live_touch_portal_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_portal_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_portal COMMAND dm_live_touch_portal_test)
set_tests_properties(live_touch_portal PROPERTIES TIMEOUT 30)
# Synthetic boundary fixture for the real Vulkan shop-ghost capture/replay.
add_executable(dm_ui_ghost_policy_test tests/ui_ghost_policy_test.c)
target_include_directories(dm_ui_ghost_policy_test PRIVATE native)
target_compile_options(dm_ui_ghost_policy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME ui_ghost_policy COMMAND dm_ui_ghost_policy_test)
set_tests_properties(ui_ghost_policy PROPERTIES TIMEOUT 30)
add_test(NAME shop_ghost_sync COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/shop_ghost_sync_test.py)
set_tests_properties(shop_ghost_sync PROPERTIES TIMEOUT 30)
# Physical controller vs touchscreen layout routing (real input_ready classifier).
add_executable(dm_padmode_route_test tests/padmode_route_test.c)
target_include_directories(dm_padmode_route_test PRIVATE native)
target_compile_options(dm_padmode_route_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME padmode_route COMMAND dm_padmode_route_test)
set_tests_properties(padmode_route PROPERTIES TIMEOUT 30)
add_executable(dm_live_touch_items_test tests/live_touch_items_test.c native/live_touch.c native/live_touch_items.c)
target_link_libraries(dm_live_touch_items_test PRIVATE dm_android_touch_core m)
target_include_directories(dm_live_touch_items_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_items_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_items COMMAND dm_live_touch_items_test)
set_tests_properties(live_touch_items PROPERTIES TIMEOUT 30)
add_executable(dm_live_touch_skill_empty_test tests/live_touch_skill_empty_test.c native/live_touch.c)
target_link_libraries(dm_live_touch_skill_empty_test PRIVATE dm_android_touch_core m)
target_include_directories(dm_live_touch_skill_empty_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_skill_empty_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_skill_empty COMMAND dm_live_touch_skill_empty_test)
set_tests_properties(live_touch_skill_empty PROPERTIES TIMEOUT 30)
add_executable(dm_live_touch_bind_tap_test tests/live_touch_bind_tap_test.c native/live_touch.c)
target_link_libraries(dm_live_touch_bind_tap_test PRIVATE dm_android_touch_core m)
target_include_directories(dm_live_touch_bind_tap_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_bind_tap_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_bind_tap COMMAND dm_live_touch_bind_tap_test)
set_tests_properties(live_touch_bind_tap PROPERTIES TIMEOUT 30)
add_executable(dm_live_touch_bind_tap_items_test tests/live_touch_bind_tap_items_test.c native/live_touch.c native/live_touch_items.c)
target_link_libraries(dm_live_touch_bind_tap_items_test PRIVATE dm_android_touch_core m)
target_include_directories(dm_live_touch_bind_tap_items_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_bind_tap_items_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_bind_tap_items COMMAND dm_live_touch_bind_tap_items_test)
set_tests_properties(live_touch_bind_tap_items PROPERTIES TIMEOUT 30)
add_executable(dm_live_touch_glyph_test tests/live_touch_glyph_test.c
    native/live_touch.c native/live_touch_glyph.c tests/touch_art_synthetic.c
    ../ios/Sources/dm_touch_controls.c)
target_link_libraries(dm_live_touch_glyph_test PRIVATE m)
target_include_directories(dm_live_touch_glyph_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_glyph_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_glyph COMMAND dm_live_touch_glyph_test)
set_tests_properties(live_touch_glyph PROPERTIES TIMEOUT 30)
add_executable(dm_live_touch_glyph_race_test tests/live_touch_glyph_race_test.c
    native/live_touch.c native/live_touch_glyph.c tests/touch_art_synthetic.c
    ../ios/Sources/dm_touch_controls.c)
target_link_libraries(dm_live_touch_glyph_race_test PRIVATE m Threads::Threads)
target_include_directories(dm_live_touch_glyph_race_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_live_touch_glyph_race_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_touch_glyph_race COMMAND dm_live_touch_glyph_race_test)
set_tests_properties(live_touch_glyph_race PROPERTIES TIMEOUT 30)
# #35 shared guest skill snapshot bridge: parse/group/identity, Android safe
# reader semantics, gate absent/present attach and scene/TTL fail-closed.
add_executable(dm_skill_snapshot_test tests/skill_snapshot.c
    ../ios/Sources/dm_touch_game_state.c
    ../ios/Sources/dm_touch_game_identity.c
    ../ios/Sources/dm_touch_game_bridge.c)
target_link_libraries(dm_skill_snapshot_test PRIVATE Threads::Threads m)
target_include_directories(dm_skill_snapshot_test PRIVATE native ../ios/Sources ../host)
target_compile_options(dm_skill_snapshot_test PRIVATE -Wall -Wextra -Werror)
if(ANDROID)
    target_sources(dm_skill_snapshot_test PRIVATE native/host_posix.c)
    add_executable(dm_debug_collection_test tests/debug_collection_test.c native/host_posix.c)
    target_include_directories(dm_debug_collection_test PRIVATE native)
    target_compile_options(dm_debug_collection_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
add_test(NAME skill_snapshot COMMAND dm_skill_snapshot_test)
set_tests_properties(skill_snapshot PROPERTIES TIMEOUT 30)
add_library(dm_android_app_lifecycle STATIC app/lifecycle.c)
target_include_directories(dm_android_app_lifecycle PUBLIC app)
target_compile_options(dm_android_app_lifecycle PRIVATE -Wall -Wextra -Werror)
add_executable(dm_app_lifecycle_test tests/app_lifecycle.c)
target_link_libraries(dm_app_lifecycle_test PRIVATE dm_android_app_lifecycle)
target_compile_options(dm_app_lifecycle_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME app_lifecycle COMMAND dm_app_lifecycle_test)
set_tests_properties(app_lifecycle PROPERTIES TIMEOUT 30)
# #157 Android「自定義」偏好事務模組：凍結上游鍵/缺省值/Apply-Discard-Defaults，有界解析 + 注入持久化回調。
# #157 Android Custom-preferences transaction module: frozen upstream keys/defaults/Apply-Discard-Defaults, bounded
# parser and an injected persistence callback.
add_library(dm_android_app_custom_preferences STATIC app/custom_preferences.c)
target_include_directories(dm_android_app_custom_preferences PUBLIC app)
target_compile_options(dm_android_app_custom_preferences PRIVATE -Wall -Wextra -Werror)
add_executable(dm_custom_preferences_test tests/custom_preferences.c)
target_link_libraries(dm_custom_preferences_test PRIVATE dm_android_app_custom_preferences)
target_compile_options(dm_custom_preferences_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME custom_preferences COMMAND dm_custom_preferences_test)
set_tests_properties(custom_preferences PROPERTIES TIMEOUT 30)
# #167 app-private file adapter for the accepted custom-preferences transaction.
add_library(dm_android_custom_preferences_store STATIC app/custom_preferences_store.c)
target_include_directories(dm_android_custom_preferences_store PUBLIC app)
target_link_libraries(dm_android_custom_preferences_store PUBLIC dm_android_app_custom_preferences)
target_compile_options(dm_android_custom_preferences_store PRIVATE -Wall -Wextra -Werror)
add_executable(dm_custom_preferences_store_test tests/custom_preferences_store.c)
target_link_libraries(dm_custom_preferences_store_test PRIVATE dm_android_custom_preferences_store dm_android_app_custom_preferences)
target_compile_options(dm_custom_preferences_store_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME custom_preferences_store COMMAND dm_custom_preferences_store_test)
set_tests_properties(custom_preferences_store PROPERTIES TIMEOUT 60)
# #167 test-only post-fsync/pre-rename fault hook; production library has no hook.
add_executable(dm_custom_preferences_store_fault_test
    tests/custom_preferences_store.c
    app/custom_preferences_store.c
    app/custom_preferences.c)
target_include_directories(dm_custom_preferences_store_fault_test PRIVATE app)
target_compile_definitions(dm_custom_preferences_store_fault_test PRIVATE DM_CP_STORE_TEST_FAULT=1)
target_compile_options(dm_custom_preferences_store_fault_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME custom_preferences_store_fault COMMAND dm_custom_preferences_store_fault_test)
set_tests_properties(custom_preferences_store_fault PROPERTIES TIMEOUT 60)
add_library(dm_android_touch_bridge STATIC app/touch_bridge.c)
target_include_directories(dm_android_touch_bridge PUBLIC app runtime)
target_link_libraries(dm_android_touch_bridge PUBLIC dm_android_touch dm_android_touch_game)
target_compile_options(dm_android_touch_bridge PRIVATE -Wall -Wextra -Werror)
add_executable(dm_touch_bridge_test tests/touch_bridge.c)
target_link_libraries(dm_touch_bridge_test PRIVATE dm_android_touch_bridge dm_android_touch dm_android_runtime m)
target_compile_options(dm_touch_bridge_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME touch_bridge COMMAND dm_touch_bridge_test)
set_tests_properties(touch_bridge PROPERTIES TIMEOUT 30)
add_executable(dm_touch_game_bridge_test tests/touch_game_bridge.c)
target_link_libraries(dm_touch_game_bridge_test PRIVATE dm_android_touch_bridge dm_android_touch_game dm_android_touch dm_android_runtime m)
target_compile_options(dm_touch_game_bridge_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME touch_game_bridge COMMAND dm_touch_game_bridge_test)
set_tests_properties(touch_game_bridge PROPERTIES TIMEOUT 30)
add_library(dm_android_view_policy STATIC app/view_policy.c)
target_include_directories(dm_android_view_policy PUBLIC app)
target_compile_options(dm_android_view_policy PRIVATE -Wall -Wextra -Werror)
add_executable(dm_view_policy_test tests/view_policy.c)
target_link_libraries(dm_view_policy_test PRIVATE dm_android_view_policy dm_android_touch_bridge dm_android_touch dm_android_runtime m)
target_compile_options(dm_view_policy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME view_policy COMMAND dm_view_policy_test)
set_tests_properties(view_policy PROPERTIES TIMEOUT 30)
add_library(dm_android_input_policy STATIC app/input_policy.c)
target_include_directories(dm_android_input_policy PUBLIC app)
target_compile_options(dm_android_input_policy PRIVATE -Wall -Wextra -Werror)
add_executable(dm_input_policy_test tests/input_policy.c)
target_link_libraries(dm_input_policy_test PRIVATE dm_android_input_policy dm_android_touch_bridge dm_android_touch dm_android_runtime m)
target_compile_options(dm_input_policy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME input_policy COMMAND dm_input_policy_test)
set_tests_properties(input_policy PROPERTIES TIMEOUT 30)
# #154 Physical gamepad AInputEvent getters -> accepted dm_npad_state. Host
# tests replace only the narrow Android getter boundary; no App wiring.
add_library(dm_android_gamepad_events STATIC app/gamepad_events.c)
target_include_directories(dm_android_gamepad_events PUBLIC app runtime ${CMAKE_CURRENT_SOURCE_DIR}/../host)
target_link_libraries(dm_android_gamepad_events PUBLIC m)
target_compile_options(dm_android_gamepad_events PRIVATE -Wall -Wextra -Werror)
add_executable(dm_gamepad_events_test tests/gamepad_events.c)
target_link_libraries(dm_gamepad_events_test PRIVATE dm_android_gamepad_events)
target_compile_options(dm_gamepad_events_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME gamepad_events COMMAND dm_gamepad_events_test)
set_tests_properties(gamepad_events PROPERTIES TIMEOUT 30)
# #150 Touch/gamepad source arbitration. The module links the accepted real
# bridge/adapter and uses dm_npad_state; no Android event extraction or UI.
add_library(dm_android_input_source STATIC app/input_source.c)
target_include_directories(dm_android_input_source PUBLIC app runtime)
target_link_libraries(dm_android_input_source PUBLIC dm_android_touch_bridge dm_android_touch dm_android_runtime m)
target_compile_options(dm_android_input_source PRIVATE -Wall -Wextra -Werror)
add_executable(dm_input_source_test tests/input_source.c)
target_link_libraries(dm_input_source_test PRIVATE dm_android_input_source)
target_compile_options(dm_input_source_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME input_source COMMAND dm_input_source_test)
set_tests_properties(input_source PROPERTIES TIMEOUT 30)
if(NOT ANDROID)
    # #159 actual arbiter invalidation -> real bridge request retirement.
    add_executable(dm_input_source_requests_test tests/input_source_requests.c app/input_source.c)
    target_include_directories(dm_input_source_requests_test PRIVATE app runtime)
    target_compile_definitions(dm_input_source_requests_test PRIVATE DM_INPUT_TEST)
    target_link_libraries(dm_input_source_requests_test PRIVATE dm_android_touch_bridge dm_android_runtime m)
    target_compile_options(dm_input_source_requests_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME input_source_requests COMMAND dm_input_source_requests_test)
    set_tests_properties(input_source_requests PROPERTIES TIMEOUT 30)
    # #165 arbiters' read-only view and atomic geometry over the same bridge.
    add_executable(dm_input_source_view_test tests/input_source_view.c app/input_source.c)
    target_include_directories(dm_input_source_view_test PRIVATE app runtime)
    target_compile_definitions(dm_input_source_view_test PRIVATE DM_INPUT_TEST)
    target_link_libraries(dm_input_source_view_test PRIVATE dm_android_touch_bridge dm_android_runtime m)
    target_compile_options(dm_input_source_view_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME input_source_view COMMAND dm_input_source_view_test)
    set_tests_properties(input_source_view PROPERTIES TIMEOUT 30)
endif()
# #117 自有诊断场景与窗口呈现策略：纯 C，无 Vulkan/Android 头文件，故在 CI 的 Vulkan=OFF 配置下也构建并运行。
# #117 owned diagnostic scene and window presentation policy: plain C without Vulkan/Android headers, so it builds
# and runs in the Vulkan=OFF CI configurations too.
add_library(dm_android_app_scene STATIC app/diagnostic_scene.c app/present_policy.c)
target_include_directories(dm_android_app_scene PUBLIC app)
target_link_libraries(dm_android_app_scene PUBLIC dm_android_app_lifecycle)
target_compile_options(dm_android_app_scene PRIVATE -Wall -Wextra -Werror)
add_executable(dm_app_diagnostic_scene_test tests/app_diagnostic_scene.c)
target_link_libraries(dm_app_diagnostic_scene_test PRIVATE dm_android_app_scene dm_android_app_lifecycle)
target_compile_options(dm_app_diagnostic_scene_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME app_diagnostic_scene COMMAND dm_app_diagnostic_scene_test)
add_executable(dm_app_present_policy_test tests/app_present_policy.c)
target_link_libraries(dm_app_present_policy_test PRIVATE dm_android_app_scene)
target_compile_options(dm_app_present_policy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME app_present_policy COMMAND dm_app_present_policy_test)
set_tests_properties(app_diagnostic_scene app_present_policy PROPERTIES TIMEOUT 30)
# #153 音频焦点/播放意图策略：纯 C，无 Android/AAudio 头文件，后端完全注入；不发起真实焦点请求或播放。
# #153 audio-focus / desired-play policy: plain C without Android/AAudio headers, backend fully injected; it performs
# no real focus request and no playback.
add_library(dm_android_app_audio_focus STATIC app/audio_focus.c)
target_include_directories(dm_android_app_audio_focus PUBLIC app)
target_compile_options(dm_android_app_audio_focus PRIVATE -Wall -Wextra -Werror)
add_executable(dm_audio_focus_test tests/audio_focus.c)
target_link_libraries(dm_audio_focus_test PRIVATE dm_android_app_audio_focus m)
target_compile_options(dm_audio_focus_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME audio_focus COMMAND dm_audio_focus_test)
set_tests_properties(audio_focus PROPERTIES TIMEOUT 30)
# #143 显式 WSI 诊断控制器：纯 C 状态机，宿主可注入失败/陈旧窗口/重建。
# #143 explicit WSI diagnostic controller: pure-C state machine, host-testable with injected failures, stale windows and rebuilds.
add_library(dm_android_app_wsi STATIC app/wsi_diagnostic.c app/wsi_bridge.c app/wsi_report.c)
target_include_directories(dm_android_app_wsi PUBLIC app)
target_compile_options(dm_android_app_wsi PRIVATE -Wall -Wextra -Werror)
add_executable(dm_app_wsi_test tests/app_wsi_diagnostic.c)
target_link_libraries(dm_app_wsi_test PRIVATE dm_android_app_wsi)
target_compile_options(dm_app_wsi_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME app_wsi_diagnostic COMMAND dm_app_wsi_test)
set_tests_properties(app_wsi_diagnostic PROPERTIES TIMEOUT 30)
# #143 桥测试用真实 dm_app_lifecycle，验证"排空 → 生命周期释放"顺序与失败保留。
# #143 bridge test uses the real dm_app_lifecycle to check the drain -> lifecycle-release order and failure retention.
add_executable(dm_wsi_bridge_test tests/wsi_bridge.c)
target_link_libraries(dm_wsi_bridge_test PRIVATE dm_android_app_wsi dm_android_app_lifecycle)
target_compile_options(dm_wsi_bridge_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME wsi_bridge COMMAND dm_wsi_bridge_test)
set_tests_properties(wsi_bridge PROPERTIES TIMEOUT 30)
# #147 有界证据报告：确定性时间戳下的统计/格式边界。| #147 bounded evidence report with deterministic timestamps.
add_executable(dm_wsi_report_test tests/wsi_report.c)
target_link_libraries(dm_wsi_report_test PRIVATE dm_android_app_wsi)
target_compile_options(dm_wsi_report_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME wsi_report COMMAND dm_wsi_report_test)
set_tests_properties(wsi_report PROPERTIES TIMEOUT 30)
add_executable(dm_image_code_test tests/image_code.c)
target_link_libraries(dm_image_code_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_image_code_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_romfs_test tests/romfs.c)
target_link_libraries(dm_romfs_test PRIVATE dm_android_runtime)
target_compile_options(dm_romfs_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_sync_test tests/sync.c)
target_link_libraries(dm_sync_test PRIVATE dm_android_runtime)
target_compile_options(dm_sync_test PRIVATE -Wall -Wextra -Werror)
add_library(dm_ipc_test_ipc OBJECT runtime/ipc.c)
target_include_directories(dm_ipc_test_ipc PRIVATE runtime)
target_compile_definitions(dm_ipc_test_ipc PRIVATE dm_host_memory_pin_locked=test_output_pin)
target_compile_options(dm_ipc_test_ipc PRIVATE -Wall -Wextra -Werror)
add_library(dm_ipc_test_sm OBJECT runtime/sm.c)
target_include_directories(dm_ipc_test_sm PRIVATE runtime)
target_compile_definitions(dm_ipc_test_sm PRIVATE dm_host_ipc_create_session=test_create_session)
target_compile_options(dm_ipc_test_sm PRIVATE -Wall -Wextra -Werror)
add_library(dm_ipc_test_fault OBJECT tests/ipc_fault.c)
target_include_directories(dm_ipc_test_fault PRIVATE runtime)
target_compile_options(dm_ipc_test_fault PRIVATE -Wall -Wextra -Werror)
add_executable(dm_ipc_test tests/ipc.c
    $<TARGET_OBJECTS:dm_ipc_test_ipc>
    $<TARGET_OBJECTS:dm_ipc_test_sm>
    $<TARGET_OBJECTS:dm_ipc_test_fault>)
target_link_libraries(dm_ipc_test PRIVATE dm_android_runtime)
target_compile_options(dm_ipc_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME ipc_transport COMMAND dm_ipc_test)
add_executable(dm_lm_wire_test tests/lm_wire.c)
target_link_libraries(dm_lm_wire_test PRIVATE dm_android_runtime)
target_compile_options(dm_lm_wire_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME lm_wire COMMAND dm_lm_wire_test)
add_executable(dm_lm_install_test tests/lm_install.c)
target_link_libraries(dm_lm_install_test PRIVATE dm_android_runtime)
target_compile_options(dm_lm_install_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME lm_install COMMAND dm_lm_install_test)
add_executable(dm_sm_install_test tests/sm_install.c)
target_link_libraries(dm_sm_install_test PRIVATE dm_android_runtime)
target_compile_options(dm_sm_install_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME sm_install COMMAND dm_sm_install_test)
add_executable(dm_cancel_test tests/cancel.c)
target_link_libraries(dm_cancel_test PRIVATE dm_android_runtime dm_android_nso ${CMAKE_DL_LIBS})
target_compile_options(dm_cancel_test PRIVATE -Wall -Wextra -Werror)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    target_sources(dm_cancel_test PRIVATE tests/cancel.S)
endif()
if(ANDROID)
    target_link_options(dm_cancel_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME horizon_cancel COMMAND dm_cancel_test)
    set_tests_properties(horizon_cancel PROPERTIES TIMEOUT 30)
endif()
add_executable(dm_priority_test tests/priority.c)
target_link_libraries(dm_priority_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_priority_test PRIVATE -Wall -Wextra -Werror)
if(ANDROID)
    target_link_options(dm_priority_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME horizon_priority COMMAND dm_priority_test)
    set_tests_properties(horizon_priority PROPERTIES TIMEOUT 30)
endif()
add_executable(dm_virtual_core_test tests/virtual_core.c)
target_link_libraries(dm_virtual_core_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_virtual_core_test PRIVATE -Wall -Wextra -Werror)
if(ANDROID)
    target_link_options(dm_virtual_core_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME virtual_core_policy COMMAND dm_virtual_core_test)
    set_tests_properties(virtual_core_policy PROPERTIES TIMEOUT 30)
endif()
add_executable(dm_arbiter_test tests/arbiter.c)
target_link_libraries(dm_arbiter_test PRIVATE dm_android_runtime)
target_compile_options(dm_arbiter_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_thread_test tests/thread.c)
target_link_libraries(dm_thread_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_thread_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_bootstrap_test tests/bootstrap.c)
target_link_libraries(dm_bootstrap_test PRIVATE dm_android_runtime)
target_compile_options(dm_bootstrap_test PRIVATE -Wall -Wextra -Werror)
if(NOT ANDROID AND TARGET dm_android_decode AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    add_library(dm_diag_normal_svc OBJECT runtime/svc.c)
    target_compile_definitions(dm_diag_normal_svc PRIVATE dm_host_svc=dm_diag_normal_svc)
    target_compile_options(dm_diag_normal_svc PRIVATE -Wall -Wextra -Werror)
    add_executable(dm_startup_diagnostic_test startup_diagnostic.c startup_diagnostic_svc.c
        tests/startup_diagnostic.c tests/startup_diagnostic.S $<TARGET_OBJECTS:dm_diag_normal_svc>)
    target_link_libraries(dm_startup_diagnostic_test PRIVATE dm_android_decode dm_android_runtime dm_android_nso)
    target_compile_options(dm_startup_diagnostic_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME startup_diagnostic COMMAND dm_startup_diagnostic_test)
    set_tests_properties(startup_diagnostic PROPERTIES TIMEOUT 30)
endif()
add_executable(dm_accounting_test tests/accounting.c)
target_link_libraries(dm_accounting_test PRIVATE dm_android_runtime dm_android_nso ${CMAKE_DL_LIBS})
target_compile_options(dm_accounting_test PRIVATE -Wall -Wextra -Werror)
if(ANDROID)
    target_link_options(dm_accounting_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME process_accounting COMMAND dm_accounting_test)
    set_tests_properties(process_accounting PROPERTIES TIMEOUT 30)
endif()
add_executable(dm_process_memory_test tests/process_memory.c)
target_link_libraries(dm_process_memory_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_process_memory_test PRIVATE -Wall -Wextra -Werror)
if(ANDROID)
    target_link_options(dm_process_memory_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME process_memory COMMAND dm_process_memory_test)
    set_tests_properties(process_memory PROPERTIES TIMEOUT 30)
endif()
add_executable(dm_memory_test tests/memory.c)
target_link_libraries(dm_memory_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_memory_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_mutex_test tests/mutex.c)
target_link_libraries(dm_mutex_test PRIVATE dm_android_runtime dm_android_nso)
target_compile_options(dm_mutex_test PRIVATE -Wall -Wextra -Werror)

if(ANDROID OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    enable_language(ASM)
    target_sources(dm_bootstrap_test PRIVATE tests/bootstrap_entry.S)
    target_sources(dm_memory_test PRIVATE tests/bootstrap_entry.S)
    add_library(dm_android_gate OBJECT gate.S runtime/thread_stack.S)
    add_library(dm_android_runtime_gate OBJECT gate.S runtime/thread_stack.S runtime/bridge.S runtime/bridge_imports.S)
    target_compile_definitions(dm_android_runtime_gate PRIVATE DM_RUNTIME_BRIDGE=1)
    target_link_libraries(dm_android_runtime PUBLIC dm_android_runtime_gate)
    add_executable(dm_small_stack_test tests/small_stack.c tests/small_stack.S)
    target_link_libraries(dm_small_stack_test PRIVATE dm_android_runtime dm_android_nso)
    target_compile_options(dm_small_stack_test PRIVATE -Wall -Wextra -Werror)
    add_executable(dm_bridge_test tests/bridge.c tests/bridge.S)
    target_link_libraries(dm_bridge_test PRIVATE dm_android_runtime dm_android_nso)
    target_compile_options(dm_bridge_test PRIVATE -Wall -Wextra -Werror)
    add_executable(dm_bridge_imports_test tests/bridge_imports.c tests/bridge_imports.S)
    target_link_libraries(dm_bridge_imports_test PRIVATE dm_android_runtime)
    target_compile_options(dm_bridge_imports_test PRIVATE -Wall -Wextra -Werror)
    if(ANDROID)
        target_link_options(dm_small_stack_test PRIVATE "-Wl,-z,max-page-size=16384")
        target_link_options(dm_bridge_test PRIVATE "-Wl,-z,max-page-size=16384")
        target_link_options(dm_bridge_imports_test PRIVATE "-Wl,-z,max-page-size=16384")
    else()
        add_test(NAME small_stack COMMAND dm_small_stack_test)
        set_tests_properties(small_stack PROPERTIES TIMEOUT 30)
        add_test(NAME bridge_core COMMAND dm_bridge_test)
        set_tests_properties(bridge_core PROPERTIES TIMEOUT 30)
        add_test(NAME bridge_imports COMMAND dm_bridge_imports_test)
        set_tests_properties(bridge_imports PROPERTIES TIMEOUT 30)
    endif()
    target_link_libraries(dm_image_test PRIVATE dm_android_gate)
    add_executable(dm_gate_test ../verify/gatefn/gate_test.c tests/gate_tramp.S)
    target_link_libraries(dm_gate_test PRIVATE dm_android_gate)
    add_executable(dm_gate_vectors tests/gate_vectors.c tests/gate_vectors.S)
    target_link_libraries(dm_gate_vectors PRIVATE dm_android_gate)
    target_compile_options(dm_gate_vectors PRIVATE -Wall -Wextra -Werror)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/synthetic-gates.tsv "100 svc 0 1000\n104 obs 1 1004\n")
    foreach(reg RANGE 0 29)
        if(NOT reg EQUAL 18)
            file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/synthetic-gates.tsv "108 cntpct ${reg} 1008\n")
        endif()
    endforeach()
endif()

option(DM_ANDROID_VULKAN "Build Vulkan offscreen foundation and GPU verification" ON)
if(DM_ANDROID_VULKAN)
    add_subdirectory(vulkan)
    # #111 离屏触摸 HUD：几何直接复用冻结 dm_tc_layout；无 App/swapchain。
    find_program(DM_GLSLC glslc REQUIRED)
    set(dm_hud_vert ${CMAKE_CURRENT_BINARY_DIR}/vulkan/hud.vert.spv)
    set(dm_hud_frag ${CMAKE_CURRENT_BINARY_DIR}/vulkan/hud.frag.spv)
    add_custom_command(OUTPUT ${dm_hud_vert}
        COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/hud.vert -o ${dm_hud_vert}
        DEPENDS vulkan/hud.vert VERBATIM)
    add_custom_command(OUTPUT ${dm_hud_frag}
        COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/hud.frag -o ${dm_hud_frag}
        DEPENDS vulkan/hud.frag VERBATIM)
    add_custom_target(dm_touch_hud_shaders DEPENDS ${dm_hud_vert} ${dm_hud_frag})
    add_library(dm_android_touch_hud STATIC runtime/touch_hud.c runtime/touch_hud_overlay.c)
    target_include_directories(dm_android_touch_hud PUBLIC runtime ../ios/Sources vulkan)
    target_link_libraries(dm_android_touch_hud PUBLIC dm_android_vk dm_android_touch_core)
    target_compile_options(dm_android_touch_hud PRIVATE -Wall -Wextra -Werror)
    # #199 owned HUD→swapchain presenter; clear-only chain API remains default.
    add_library(dm_android_touch_hud_swapchain STATIC runtime/touch_hud_swapchain.c)
    target_include_directories(dm_android_touch_hud_swapchain PUBLIC runtime vulkan)
    target_link_libraries(dm_android_touch_hud_swapchain PUBLIC dm_android_touch_hud dm_android_vk)
    target_compile_options(dm_android_touch_hud_swapchain PRIVATE -Wall -Wextra -Werror)
    add_library(dm_android_diag_scene_swapchain STATIC app/diagnostic_scene_swapchain.c)
    target_include_directories(dm_android_diag_scene_swapchain PUBLIC app runtime vulkan)
    target_link_libraries(dm_android_diag_scene_swapchain PUBLIC dm_android_app_scene dm_android_touch_hud_swapchain dm_android_vk)
    target_compile_options(dm_android_diag_scene_swapchain PRIVATE -Wall -Wextra -Werror)
    add_executable(dm_touch_hud_swapchain_test tests/touch_hud_swapchain.c vulkan/vk_swapchain.c runtime/touch_hud_swapchain.c runtime/touch_hud.c)
    target_include_directories(dm_touch_hud_swapchain_test PRIVATE runtime ../ios/Sources vulkan)
    target_compile_options(dm_touch_hud_swapchain_test PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(dm_touch_hud_swapchain_test PRIVATE
        DM_TOUCH_HUD_GEOMETRY_ONLY DM_TOUCH_HUD_TEST_CALLOC=hud_test_calloc)
    target_link_libraries(dm_touch_hud_swapchain_test PRIVATE dm_android_touch_core m)
    foreach(symbol vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceFormatsKHR
        vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceFormatProperties vkCreateSwapchainKHR
        vkGetSwapchainImagesKHR vkDestroySwapchainKHR vkCreateRenderPass vkDestroyRenderPass vkCreateImageView
        vkDestroyImageView vkCreateFramebuffer vkDestroyFramebuffer vkAllocateCommandBuffers vkFreeCommandBuffers
        vkBeginCommandBuffer vkCmdBeginRenderPass vkCmdEndRenderPass vkEndCommandBuffer vkCreateSemaphore
        vkDestroySemaphore vkCreateFence vkDestroyFence vkWaitForFences vkResetFences vkQueueWaitIdle
        vkDeviceWaitIdle vkAcquireNextImageKHR vkQueueSubmit vkQueuePresentKHR
        vkCreatePipelineLayout vkDestroyPipelineLayout vkCreateGraphicsPipelines vkDestroyPipeline
        vkDestroyShaderModule vkCmdBindPipeline vkCmdBindVertexBuffers vkCmdSetViewport vkCmdSetScissor
        vkCmdPushConstants vkCmdDraw)
        target_compile_definitions(dm_touch_hud_swapchain_test PRIVATE ${symbol}=test_${symbol})
    endforeach()
    if(NOT ANDROID)
        find_package(Vulkan REQUIRED)
        target_include_directories(dm_touch_hud_swapchain_test PRIVATE ${Vulkan_INCLUDE_DIRS})
    endif()
    # Private mutation control: no actual draw must fail the same checks.
    add_executable(dm_touch_hud_swapchain_mutant_test tests/touch_hud_swapchain.c vulkan/vk_swapchain.c runtime/touch_hud_swapchain.c runtime/touch_hud.c)
    target_include_directories(dm_touch_hud_swapchain_mutant_test PRIVATE runtime ../ios/Sources vulkan)
    target_compile_options(dm_touch_hud_swapchain_mutant_test PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(dm_touch_hud_swapchain_mutant_test PRIVATE
        DM_TOUCH_HUD_GEOMETRY_ONLY DM_TOUCH_HUD_TEST_CALLOC=hud_test_calloc
        vkCmdDraw=mutant_vkCmdDraw DM_HUD_TEST_MUTANT_NO_DRAW)
    target_link_libraries(dm_touch_hud_swapchain_mutant_test PRIVATE dm_android_touch_core m)
    foreach(symbol vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceFormatsKHR
        vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceFormatProperties vkCreateSwapchainKHR
        vkGetSwapchainImagesKHR vkDestroySwapchainKHR vkCreateRenderPass vkDestroyRenderPass vkCreateImageView
        vkDestroyImageView vkCreateFramebuffer vkDestroyFramebuffer vkAllocateCommandBuffers vkFreeCommandBuffers
        vkBeginCommandBuffer vkCmdBeginRenderPass vkCmdEndRenderPass vkEndCommandBuffer vkCreateSemaphore
        vkDestroySemaphore vkCreateFence vkDestroyFence vkWaitForFences vkResetFences vkQueueWaitIdle
        vkDeviceWaitIdle vkAcquireNextImageKHR vkQueueSubmit vkQueuePresentKHR
        vkCreatePipelineLayout vkDestroyPipelineLayout vkCreateGraphicsPipelines vkDestroyPipeline
        vkDestroyShaderModule vkCmdBindPipeline vkCmdBindVertexBuffers vkCmdSetViewport vkCmdSetScissor
        vkCmdPushConstants)
        target_compile_definitions(dm_touch_hud_swapchain_mutant_test PRIVATE ${symbol}=test_${symbol})
    endforeach()
    if(NOT ANDROID)
        target_include_directories(dm_touch_hud_swapchain_mutant_test PRIVATE ${Vulkan_INCLUDE_DIRS})
    endif()
    add_executable(dm_touch_hud_test tests/touch_hud.c)
    target_link_libraries(dm_touch_hud_test PRIVATE dm_android_touch_hud)
    target_compile_options(dm_touch_hud_test PRIVATE -Wall -Wextra -Werror)
    add_dependencies(dm_touch_hud_test dm_touch_hud_shaders)
    # #121 actual bridge event -> observation -> HUD geometry/pixels seam.
    add_executable(dm_touch_hud_live_test tests/touch_hud_live.c)
    target_include_directories(dm_touch_hud_live_test PRIVATE app runtime ../ios/Sources vulkan)
    target_link_libraries(dm_touch_hud_live_test PRIVATE dm_android_touch_hud dm_android_touch_bridge dm_android_runtime m)
    target_compile_options(dm_touch_hud_live_test PRIVATE -Wall -Wextra -Werror)
    add_dependencies(dm_touch_hud_live_test dm_touch_hud_shaders)
    # #121 CPU window seam: actual bridge events -> observation -> CPU overlay,
    # scheduler coalescing/retry, stride/padding, density, cancel/resize and
    # finite 1e30 clipping. Platform-neutral; no Vulkan or Android.
    add_executable(dm_touch_hud_cpu_test tests/touch_hud_cpu.c)
    target_include_directories(dm_touch_hud_cpu_test PRIVATE app runtime ../ios/Sources)
    target_link_libraries(dm_touch_hud_cpu_test PRIVATE dm_android_touch_hud dm_android_touch_bridge dm_android_runtime m)
    target_compile_options(dm_touch_hud_cpu_test PRIVATE -Wall -Wextra -Werror)
    # GPU-free lifecycle oracle: mocks the actual Vulkan/dm_vk calls and counts
    # real create/destroy, allocate/free, map/unmap, command and fence pairs.
    add_executable(dm_touch_hud_mock_test runtime/touch_hud.c tests/touch_hud_mock.c)
    target_include_directories(dm_touch_hud_mock_test PRIVATE runtime ../ios/Sources vulkan)
    if(NOT ANDROID)
        find_package(Vulkan REQUIRED)
        target_include_directories(dm_touch_hud_mock_test PRIVATE ${Vulkan_INCLUDE_DIRS})
    endif()
    target_link_libraries(dm_touch_hud_mock_test PRIVATE dm_android_touch_core)
    target_compile_options(dm_touch_hud_mock_test PRIVATE -Wall -Wextra -Werror)
    if(ANDROID)
        target_link_options(dm_touch_hud_test PRIVATE "-Wl,-z,max-page-size=16384")
        target_link_options(dm_touch_hud_live_test PRIVATE "-Wl,-z,max-page-size=16384")
        target_link_options(dm_touch_hud_mock_test PRIVATE "-Wl,-z,max-page-size=16384")
        target_link_options(dm_touch_hud_swapchain_test PRIVATE "-Wl,-z,max-page-size=16384")
    else()
        add_test(NAME touch_hud_swapchain COMMAND dm_touch_hud_swapchain_test)
        set_tests_properties(touch_hud_swapchain PROPERTIES TIMEOUT 60)
        if(NOT ANDROID)
            add_executable(dm_scene_hud_swapchain_test
                tests/scene_hud_swapchain.c vulkan/vk_swapchain.c
                runtime/touch_hud_swapchain.c runtime/touch_hud.c
                app/diagnostic_scene_swapchain.c)
            target_include_directories(dm_scene_hud_swapchain_test PRIVATE
                app runtime ../ios/Sources vulkan)
            target_compile_options(dm_scene_hud_swapchain_test PRIVATE -Wall -Wextra -Werror)
            target_compile_definitions(dm_scene_hud_swapchain_test PRIVATE
                DM_TOUCH_HUD_GEOMETRY_ONLY DM_TOUCH_HUD_TEST_CALLOC=hud_test_calloc)
            target_link_libraries(dm_scene_hud_swapchain_test PRIVATE
                dm_android_app_scene dm_android_touch_core m)
            foreach(symbol vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceFormatsKHR
                vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceFormatProperties vkCreateSwapchainKHR
                vkGetSwapchainImagesKHR vkDestroySwapchainKHR vkCreateRenderPass vkDestroyRenderPass vkCreateImageView
                vkDestroyImageView vkCreateFramebuffer vkDestroyFramebuffer vkAllocateCommandBuffers vkFreeCommandBuffers
                vkBeginCommandBuffer vkCmdEndRenderPass vkEndCommandBuffer vkCreateSemaphore
                vkDestroySemaphore vkCreateFence vkDestroyFence vkWaitForFences vkResetFences vkQueueWaitIdle
                vkDeviceWaitIdle vkAcquireNextImageKHR vkQueueSubmit vkQueuePresentKHR
                vkCreatePipelineLayout vkDestroyPipelineLayout vkCreateGraphicsPipelines vkDestroyPipeline
                vkDestroyShaderModule vkCmdBindPipeline vkCmdBindVertexBuffers vkCmdSetViewport vkCmdSetScissor
                vkCmdPushConstants vkCreateDescriptorSetLayout vkDestroyDescriptorSetLayout
                vkCreateDescriptorPool vkDestroyDescriptorPool vkAllocateDescriptorSets vkUpdateDescriptorSets
                vkCmdBindDescriptorSets vkCmdBindIndexBuffer)
                target_compile_definitions(dm_scene_hud_swapchain_test PRIVATE ${symbol}=test_${symbol})
            endforeach()
            target_include_directories(dm_scene_hud_swapchain_test PRIVATE ${Vulkan_INCLUDE_DIRS})
            add_test(NAME scene_hud_swapchain COMMAND dm_scene_hud_swapchain_test)
            set_tests_properties(scene_hud_swapchain PROPERTIES TIMEOUT 60)
            foreach(mutant_def DM_SCENE_MUTANT_NO_DRAW DM_SCENE_MUTANT_NO_CLEAR)
                string(TOLOWER "${mutant_def}" mutant_suffix)
                add_executable(dm_scene_hud_swapchain_${mutant_suffix}_test
                    tests/scene_hud_swapchain.c vulkan/vk_swapchain.c
                    runtime/touch_hud_swapchain.c runtime/touch_hud.c
                    app/diagnostic_scene_swapchain.c)
                target_include_directories(dm_scene_hud_swapchain_${mutant_suffix}_test PRIVATE
                    app runtime ../ios/Sources vulkan ${Vulkan_INCLUDE_DIRS})
                target_compile_options(dm_scene_hud_swapchain_${mutant_suffix}_test PRIVATE -Wall -Wextra -Werror)
                target_compile_definitions(dm_scene_hud_swapchain_${mutant_suffix}_test PRIVATE
                    DM_TOUCH_HUD_GEOMETRY_ONLY DM_TOUCH_HUD_TEST_CALLOC=hud_test_calloc ${mutant_def})
                target_link_libraries(dm_scene_hud_swapchain_${mutant_suffix}_test PRIVATE
                    dm_android_app_scene dm_android_touch_core m)
                foreach(symbol vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceFormatsKHR
                    vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceFormatProperties vkCreateSwapchainKHR
                    vkGetSwapchainImagesKHR vkDestroySwapchainKHR vkCreateRenderPass vkDestroyRenderPass vkCreateImageView
                    vkDestroyImageView vkCreateFramebuffer vkDestroyFramebuffer vkAllocateCommandBuffers vkFreeCommandBuffers
                    vkBeginCommandBuffer vkCmdEndRenderPass vkEndCommandBuffer vkCreateSemaphore
                    vkDestroySemaphore vkCreateFence vkDestroyFence vkWaitForFences vkResetFences vkQueueWaitIdle
                    vkDeviceWaitIdle vkAcquireNextImageKHR vkQueueSubmit vkQueuePresentKHR
                    vkCreatePipelineLayout vkDestroyPipelineLayout vkCreateGraphicsPipelines vkDestroyPipeline
                    vkDestroyShaderModule vkCmdBindPipeline vkCmdBindVertexBuffers vkCmdSetViewport vkCmdSetScissor
                    vkCmdPushConstants vkCreateDescriptorSetLayout vkDestroyDescriptorSetLayout
                    vkCreateDescriptorPool vkDestroyDescriptorPool vkAllocateDescriptorSets vkUpdateDescriptorSets
                    vkCmdBindDescriptorSets vkCmdBindIndexBuffer)
                    target_compile_definitions(dm_scene_hud_swapchain_${mutant_suffix}_test PRIVATE ${symbol}=test_${symbol})
                endforeach()
            endforeach()
        endif()
        add_test(NAME touch_hud_offscreen COMMAND dm_touch_hud_test
            ${dm_hud_vert} ${dm_hud_frag} ${CMAKE_CURRENT_BINARY_DIR}/touch-hud-artifacts)
        set_tests_properties(touch_hud_offscreen PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
        add_test(NAME touch_hud_live COMMAND dm_touch_hud_live_test ${dm_hud_vert} ${dm_hud_frag})
        set_tests_properties(touch_hud_live PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
        add_test(NAME touch_hud_lifecycle COMMAND dm_touch_hud_mock_test)
        set_tests_properties(touch_hud_lifecycle PROPERTIES TIMEOUT 90)
        add_test(NAME touch_hud_cpu COMMAND dm_touch_hud_cpu_test)
        set_tests_properties(touch_hud_cpu PROPERTIES TIMEOUT 90)
    endif()
endif()

if(NOT ANDROID)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME vulkan_buffer_cache_host
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/vulkan_buffer_cache_host.py
                ${CMAKE_CURRENT_SOURCE_DIR}/native/nvn_vulkan.c
                ${CMAKE_CURRENT_BINARY_DIR}/vulkan_buffer_cache_fixture.c ${CMAKE_C_COMPILER})
        set_tests_properties(vulkan_buffer_cache_host PROPERTIES TIMEOUT 30)

        add_test(NAME vulkan_cpu_phases_host
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/vulkan_cpu_phases_host.py
                ${CMAKE_CURRENT_SOURCE_DIR}/native/nvn_vulkan_cpu_phases.inc
                ${CMAKE_CURRENT_BINARY_DIR}/vulkan_cpu_phases_fixture.c ${CMAKE_C_COMPILER})
        set_tests_properties(vulkan_cpu_phases_host PROPERTIES TIMEOUT 30)

        add_test(NAME nvn_fence_domain_host
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/nvn_fence_domain_host.py
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(nvn_fence_domain_host PROPERTIES TIMEOUT 30)
        # #157 私有變異：忽略保存結果或忽略語言顯式狀態，必須讓真實測試失敗。
        # #157 private mutations: ignoring the store result, or the language explicit state, must fail the real tests.
        add_test(NAME custom_preferences_mutation
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/custom_preferences_mutation.py
                --app ${CMAKE_CURRENT_SOURCE_DIR}/app
                --tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/custom_preferences.c
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(custom_preferences_mutation PROPERTIES TIMEOUT 120)
        add_test(NAME custom_preferences_store_mutation
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/custom_preferences_store_mutation.py
                --app ${CMAKE_CURRENT_SOURCE_DIR}/app
                --tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/custom_preferences_store.c
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(custom_preferences_store_mutation PROPERTIES TIMEOUT 120)
        add_test(NAME diagnostic_provenance
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_provenance.py)
        set_tests_properties(diagnostic_provenance PROPERTIES TIMEOUT 60)
        # #153 私有变异：忽略代次或用户意图必须让真实控制器测试失败。
        # #153 private mutations: ignoring the generation or the user intent must make the real tests fail.
        add_test(NAME audio_focus_mutation
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_focus_mutation.py
                --app ${CMAKE_CURRENT_SOURCE_DIR}/app
                --tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_focus.c
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(audio_focus_mutation PROPERTIES TIMEOUT 120)
        add_test(NAME native_callbacks
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_callbacks_host.py
                --native ${CMAKE_CURRENT_SOURCE_DIR}/app/native.c
                --app ${CMAKE_CURRENT_SOURCE_DIR}/app
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(native_callbacks PROPERTIES TIMEOUT 120)
        # The fixture compiles actual vk_host.h/wsi_frame_result.h. Prefer an
        # already-resolved official Vulkan include directory; otherwise fetch
        # only the pinned Khronos headers (types/constants), never a loader/SDK.
        # Vulkan-OFF host CI still needs and runs these two tests.
        set(WSI_HUD_VULKAN_INCLUDE "${Vulkan_INCLUDE_DIR}")
        if(NOT WSI_HUD_VULKAN_INCLUDE OR NOT EXISTS "${WSI_HUD_VULKAN_INCLUDE}/vulkan/vulkan.h")
            include(FetchContent)
            FetchContent_Declare(dm_vulkan_headers
                URL https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/c46850864f4661461b0f6cb9922c058ffea4915e
                URL_HASH SHA256=539ff8eaa4abf22fddbd98225c68a0c5ddb940075a4048c8b7f5d04449070a4e
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE
                SOURCE_SUBDIR unused)
            FetchContent_MakeAvailable(dm_vulkan_headers)
            set(WSI_HUD_VULKAN_INCLUDE "${dm_vulkan_headers_SOURCE_DIR}/include")
        endif()
        add_test(NAME render_policy
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/render_policy_host.py
                ${WSI_HUD_VULKAN_INCLUDE})
        add_test(NAME pipeline_disk_cache_host
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/pipeline_disk_cache_host.py
                ${WSI_HUD_VULKAN_INCLUDE} ${CMAKE_C_COMPILER})
        set_tests_properties(pipeline_disk_cache_host PROPERTIES TIMEOUT 30)
        add_test(NAME wsi_hud_caller
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/wsi_hud_host.py
                --native ${CMAKE_CURRENT_SOURCE_DIR}/app/native.c
                --vulkan-include=${WSI_HUD_VULKAN_INCLUDE}
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(wsi_hud_caller PROPERTIES TIMEOUT 120)
        add_test(NAME vulkan_lifetime_host
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/vulkan_lifetime_host.py
                ${WSI_HUD_VULKAN_INCLUDE} ${CMAKE_C_COMPILER})
        set_tests_properties(vulkan_lifetime_host PROPERTIES TIMEOUT 120)
        add_test(NAME wsi_hud_no_record_mutant
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/wsi_hud_host.py
                --native ${CMAKE_CURRENT_SOURCE_DIR}/app/native.c
                --vulkan-include=${WSI_HUD_VULKAN_INCLUDE}
                --cc ${CMAKE_C_COMPILER} --mutant)
        set_tests_properties(wsi_hud_no_record_mutant PROPERTIES TIMEOUT 120)
        add_test(NAME native_text_jni
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_text_jni_host.py
                --native ${CMAKE_CURRENT_SOURCE_DIR}/app/native.c
                --cc ${CMAKE_C_COMPILER} --mutants)
        set_tests_properties(native_text_jni PROPERTIES TIMEOUT 120)
        add_test(NAME gamepad_events_mutants
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/gamepad_events_mutants.py
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(gamepad_events_mutants PROPERTIES TIMEOUT 120)
        add_test(NAME input_source_mutants
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/input_source_mutants.py
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(input_source_mutants PROPERTIES TIMEOUT 120)
        add_test(NAME input_source_view_mutants
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/input_source_view_mutants.py
                --cc ${CMAKE_C_COMPILER})
        set_tests_properties(input_source_view_mutants PROPERTIES TIMEOUT 120)
    endif()
endif()

if(ANDROID)
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "The Android port targets arm64-v8a only")
    endif()
    # Imported reference NVN→Vulkan backend. It compiles against this tree's
    # nvn_obj.h/nvn_dirty.h and is linked beside the existing diagnostic lane;
    # it does not replace native.c or register a second production loader.
    add_library(dm_android_nvn_backend STATIC native/nvn_vulkan.c native/nvn_present_filter.c native/gpu_driver.c ../gfx/nvn/nvn_dirty.c)
    target_include_directories(dm_android_nvn_backend PUBLIC
        native native/compat ../gfx/nvn ../gfx/nvn/include ../gfx/tex ../host)
    target_link_libraries(dm_android_nvn_backend PUBLIC dm_android_tex android log dl m vulkan)
    target_compile_options(dm_android_nvn_backend PRIVATE -Wall -Wextra -Werror)
    set_source_files_properties(native/nvn_vulkan.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    add_executable(dm_android_probe probe.c)
    target_compile_options(dm_android_probe PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(dm_android_probe PRIVATE vulkan)
    if(DM_ANDROID_VULKAN)
        add_library(dm_android_app SHARED app/native.c app/native_audio.c)
        target_compile_options(dm_android_app PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_android_app PRIVATE dm_android_app_lifecycle dm_android_app_scene dm_android_app_wsi dm_android_wsi_frame dm_android_touch_bridge dm_android_input_source dm_android_gamepad_events dm_android_view_policy dm_android_input_policy dm_android_touch dm_android_runtime dm_android_artifact dm_android_vk dm_android_touch_hud dm_android_touch_hud_swapchain dm_android_diag_scene_swapchain dm_android_app_audio_session android log)
        target_link_options(dm_android_app PRIVATE "-Wl,-z,max-page-size=16384")
        add_dependencies(dm_android_app dm_touch_hud_shaders dm_vk_indexed_shaders)
        add_custom_command(TARGET dm_android_app POST_BUILD
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/bridge_shared.sh"
                "${CMAKE_READELF}" "${CMAKE_OBJDUMP}" "$<TARGET_FILE:dm_android_app>"
            VERBATIM)
    endif()
    foreach(target dm_android_probe dm_texture_test dm_nso_inspect dm_nso_test dm_image_test dm_runtime_test dm_system_patch_test dm_input_test dm_touch_adapter_test dm_live_touch_belt_test dm_live_touch_difficulty_test dm_live_touch_portal_test dm_live_touch_skill_empty_test dm_live_touch_bind_tap_test dm_live_touch_bind_tap_items_test dm_live_touch_glyph_test dm_live_touch_glyph_race_test dm_skill_snapshot_test dm_touch_bridge_test dm_touch_game_bridge_test dm_view_policy_test dm_input_policy_test dm_app_lifecycle_test dm_app_diagnostic_scene_test dm_app_present_policy_test dm_app_wsi_test dm_wsi_bridge_test dm_wsi_report_test dm_module_inspect dm_module_test dm_x18_patch_test dm_x18_control_test dm_x18_regions_test dm_image_code_test dm_artifact_inspect dm_artifact_test dm_romfs_test dm_sync_test dm_arbiter_test dm_thread_test dm_bootstrap_test dm_memory_test dm_mutex_test dm_exclusive_return_test)
        target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384")
    endforeach()
    foreach(target dm_gate_test dm_gate_vectors)
        target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384")
    endforeach()
else()
    add_test(NAME texture_roundtrip COMMAND dm_texture_test)
    add_test(NAME nso_decode COMMAND dm_nso_test)
    add_test(NAME image_mapping COMMAND dm_image_test)
    add_test(NAME image_code_mapping COMMAND dm_image_code_test)
    add_test(NAME artifact_loading COMMAND dm_artifact_test)
    add_test(NAME romfs_imports COMMAND dm_romfs_test)
    add_test(NAME horizon_events COMMAND dm_sync_test)
    add_test(NAME horizon_address_arbiter COMMAND dm_arbiter_test)
    add_test(NAME horizon_threads COMMAND dm_thread_test)
    add_test(NAME horizon_bootstrap COMMAND dm_bootstrap_test)
    add_test(NAME horizon_memory COMMAND dm_memory_test)
    add_test(NAME horizon_mutex COMMAND dm_mutex_test)
    set_tests_properties(horizon_events romfs_imports horizon_address_arbiter horizon_threads horizon_bootstrap horizon_memory horizon_mutex PROPERTIES TIMEOUT 30)
    add_test(NAME module_linking COMMAND dm_module_test)
    add_test(NAME guest_runtime COMMAND dm_runtime_test)
    add_test(NAME system_rewrite COMMAND dm_system_patch_test)
    add_test(NAME exclusive_return_boundary COMMAND dm_exclusive_return_test)
    set_tests_properties(exclusive_return_boundary PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME x18_rewrite COMMAND dm_x18_patch_test)
    add_test(NAME x18_control COMMAND dm_x18_control_test)
    add_test(NAME x18_regions COMMAND dm_x18_regions_test)
    add_test(NAME controller_input COMMAND dm_input_test)
    set_tests_properties(guest_runtime system_rewrite controller_input touch_adapter x18_rewrite x18_control x18_regions image_code_mapping PROPERTIES TIMEOUT 30)
    add_test(NAME nso_cli COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/nso_cli.sh
        $<TARGET_FILE:dm_nso_inspect> $<TARGET_FILE:dm_nso_test>)
    add_test(NAME game_files_cli COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/game_files_cli.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/check_game_files.sh)
    if(TARGET dm_gate_test)
        add_test(NAME gate_registers COMMAND dm_gate_test ${CMAKE_CURRENT_BINARY_DIR}/synthetic-gates.tsv)
        add_test(NAME gate_vectors COMMAND dm_gate_vectors)
    endif()
endif()

# #163 真实 AAudio + 焦点策略的组合 owner；宿主只替换窄 AAudio 边界，Android 链接真实 libaaudio。
# #163 composed owner of real AAudio plus the focus policy; host replaces only the narrow AAudio boundary and Android
# links the real libaaudio.
add_library(dm_android_app_audio_output STATIC app/audio_output.c)
target_include_directories(dm_android_app_audio_output PUBLIC app)
target_link_libraries(dm_android_app_audio_output PUBLIC dm_android_audio dm_android_app_audio_focus m)
target_compile_options(dm_android_app_audio_output PRIVATE -Wall -Wextra -Werror)

if(NOT ANDROID)
    add_executable(dm_audio_output_test tests/audio_output.c)
    target_link_libraries(dm_audio_output_test PRIVATE dm_android_app_audio_output dm_android_audio dm_android_app_audio_focus m)
    target_compile_options(dm_audio_output_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME audio_output COMMAND dm_audio_output_test)
    set_tests_properties(audio_output PROPERTIES TIMEOUT 60)
endif()

if(NOT ANDROID AND Python3_Interpreter_FOUND)
    # #163 私有变异：真实 PCM 回调忽略音量必须让组合测试失败。
    # #163 private mutation: ignoring the gain in the real PCM callback must fail the composition tests.
    add_test(NAME audio_output_mutation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_output_mutation.py
            --app ${CMAKE_CURRENT_SOURCE_DIR}/app
            --audio ${CMAKE_CURRENT_SOURCE_DIR}/audio
            --tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_output.c
            --cc ${CMAKE_C_COMPILER})
    set_tests_properties(audio_output_mutation PROPERTIES TIMEOUT 120)
endif()

if(ANDROID)
    # 仅真实 libaaudio 交叉链接检查，不打开设备。| Real libaaudio cross-link only; no device open.
    add_executable(dm_audio_output_cross tests/audio_output_cross.c)
    target_link_libraries(dm_audio_output_cross PRIVATE dm_android_app_audio_output dm_android_audio aaudio)
    target_compile_options(dm_audio_output_cross PRIVATE -Wall -Wextra -Werror)
    target_link_options(dm_audio_output_cross PRIVATE "-Wl,-z,max-page-size=16384")
endif()

# #170 Android AudioManager 焦点边界：主机 Java 测试（只替换 android.media 边界）。
# #170 Android AudioManager focus boundary: host Java tests replacing only the android.media boundary.
if(NOT ANDROID)
    find_package(Java COMPONENTS Development QUIET)
    if(Java_FOUND)
        add_test(NAME platform_audio_focus_java
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_audio_focus_java.sh)
        set_tests_properties(platform_audio_focus_java PROPERTIES TIMEOUT 120)
    endif()
endif()

# #184 焦点策略实例化 JNI 适配：Android 仅编译 C owner；host 需真实 JDK/JNI 才构建并运行 JVM 测试。
# #184 instance-owned JNI adapter: Android compiles the C owner; host builds and runs the JVM test only with a real
# JDK/JNI, so ordinary host builds without Java stay unaffected.
if(ANDROID)
    add_library(dm_android_app_audio_focus_jni STATIC app/audio_focus_jni.c)
    target_include_directories(dm_android_app_audio_focus_jni PUBLIC app)
    target_compile_options(dm_android_app_audio_focus_jni PRIVATE -Wall -Wextra -Werror)
endif()

if(NOT ANDROID)
    find_package(JNI QUIET)
    find_package(Java COMPONENTS Development QUIET)
    if(NOT JNI_FOUND AND Java_FOUND)
        # CMake 的 FindJNI 在部分 macOS JDK 上失败；从 javac 路径推导真实头文件目录。
        # FindJNI fails on some macOS JDKs; derive the header dirs from the javac path.
        get_filename_component(_afj_bin "${Java_JAVAC_EXECUTABLE}" DIRECTORY)
        get_filename_component(_afj_home "${_afj_bin}" DIRECTORY)
        if(EXISTS "${_afj_home}/include/jni.h")
            file(GLOB _afj_md "${_afj_home}/include/*")
            set(JNI_INCLUDE_DIRS "${_afj_home}/include" ${_afj_md})
            set(JNI_FOUND TRUE)
        endif()
    endif()
    if(JNI_FOUND AND Java_FOUND)
        add_library(dm_audio_focus_jni_host SHARED tests/audio_focus_jni_test.c app/audio_focus_jni.c)
        target_include_directories(dm_audio_focus_jni_host PRIVATE app ${JNI_INCLUDE_DIRS})
        target_link_libraries(dm_audio_focus_jni_host PRIVATE pthread)
        # JVM 加载的插件不能依赖 ASAN/UBSAN 运行时；CI host 的全局 sanitizer 标志在此目标上关闭。
        # A JVM-loaded plugin cannot depend on the ASAN/UBSAN runtime; disable the global sanitizer flags here.
        target_compile_options(dm_audio_focus_jni_host PRIVATE -Wall -Wextra -Werror -fno-sanitize=all)
        target_link_options(dm_audio_focus_jni_host PRIVATE -fno-sanitize=all)
        add_test(NAME audio_focus_jni
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_focus_jni_host.sh
                $<TARGET_FILE:dm_audio_focus_jni_host>
                ${Java_JAVAC_EXECUTABLE} ${Java_JAVA_EXECUTABLE})
        set_tests_properties(audio_focus_jni PROPERTIES TIMEOUT 180)
    endif()
endif()

# #195 串行音频会话：组合已验收 JNI 焦点适配与真实音频输出 owner；不复制队列/策略。
# #195 serialized audio session combining the accepted JNI focus adapter and the real audio output owner.
if(ANDROID)
    add_library(dm_android_app_audio_session STATIC app/audio_session.c)
    target_include_directories(dm_android_app_audio_session PUBLIC app)
    target_link_libraries(dm_android_app_audio_session PUBLIC dm_android_app_audio_focus_jni dm_android_app_audio_output)
    target_compile_options(dm_android_app_audio_session PRIVATE -Wall -Wextra -Werror)
endif()

if(NOT ANDROID)
    if(JNI_FOUND AND Java_FOUND)
        add_library(dm_audio_session_host SHARED
            tests/audio_session_test.c app/audio_session.c app/audio_focus_jni.c
            app/audio_output.c app/audio_focus.c audio/dm_aaudio.c)
        target_include_directories(dm_audio_session_host PRIVATE app audio ${JNI_INCLUDE_DIRS})
        target_link_libraries(dm_audio_session_host PRIVATE pthread m)
        # JVM 插件不能依赖 sanitizer 运行时；与 #184 相同处理。
        # A JVM plugin cannot depend on the sanitizer runtime; same as #184.
        target_compile_options(dm_audio_session_host PRIVATE -Wall -Wextra -Werror -fno-sanitize=all)
        target_link_options(dm_audio_session_host PRIVATE -fno-sanitize=all)
        add_test(NAME audio_session
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_session_host.sh
                $<TARGET_FILE:dm_audio_session_host>
                ${Java_JAVAC_EXECUTABLE} ${Java_JAVA_EXECUTABLE})
        set_tests_properties(audio_session PROPERTIES TIMEOUT 180)

        # #200 NativeActivity 音频钩子：真实 native_audio 驱动已验收 session 栈；fixture 仅替换 AAudio API 表。
        # #200 NativeActivity audio hooks over the accepted session stack; the fixture replaces only the AAudio API.
        add_library(dm_native_audio_host SHARED
            tests/native_audio_test.c app/native_audio.c app/audio_session.c app/audio_focus_jni.c
            app/audio_output.c app/audio_focus.c audio/dm_aaudio.c)
        target_include_directories(dm_native_audio_host PRIVATE app audio ${JNI_INCLUDE_DIRS})
        target_link_libraries(dm_native_audio_host PRIVATE pthread m)
        target_compile_options(dm_native_audio_host PRIVATE -Wall -Wextra -Werror -fno-sanitize=all)
        target_link_options(dm_native_audio_host PRIVATE -fno-sanitize=all)
        add_test(NAME native_audio
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_audio_host.sh
                $<TARGET_FILE:dm_native_audio_host>
                ${Java_JAVAC_EXECUTABLE} ${Java_JAVA_EXECUTABLE})
        set_tests_properties(native_audio PROPERTIES TIMEOUT 180)
    endif()
endif()

# #223 host drawing-switch handoff: two queued submits must not share state,
# and the one-shot force-full request must be consumed once.
if(NOT ANDROID)
    add_executable(dm_nvn_host_latch_test tests/nvn_host_latch.c)
    target_include_directories(dm_nvn_host_latch_test PRIVATE native)
    target_compile_options(dm_nvn_host_latch_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME nvn_host_latch COMMAND dm_nvn_host_latch_test)
    set_tests_properties(nvn_host_latch PROPERTIES TIMEOUT 30)

    add_executable(dm_nvn_shader_feature_decisions_test tests/nvn_shader_feature_decisions.c)
    target_include_directories(dm_nvn_shader_feature_decisions_test PRIVATE native)
    target_compile_options(dm_nvn_shader_feature_decisions_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME nvn_shader_feature_decisions COMMAND dm_nvn_shader_feature_decisions_test)
    set_tests_properties(nvn_shader_feature_decisions PROPERTIES TIMEOUT 30)

    add_executable(dm_texture_flat_probe_test tests/texture_flat_probe_test.c)
    target_include_directories(dm_texture_flat_probe_test PRIVATE native)
    target_compile_options(dm_texture_flat_probe_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME texture_flat_probe COMMAND dm_texture_flat_probe_test)
    set_tests_properties(texture_flat_probe PROPERTIES TIMEOUT 30)
    add_executable(dm_frame_trace_test tests/frame_trace_test.c)
    target_include_directories(dm_frame_trace_test PRIVATE native)
    target_compile_options(dm_frame_trace_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME frame_trace COMMAND dm_frame_trace_test)
    set_tests_properties(frame_trace PROPERTIES TIMEOUT 30)
    add_executable(dm_texture_shadow_alias_test tests/texture_shadow_alias_test.c ../gfx/nvn/nvn_tex.c ../gfx/tex/nvn_deswizzle.c)
    target_include_directories(dm_texture_shadow_alias_test PRIVATE native ../gfx/nvn ../gfx/tex)
    target_compile_options(dm_texture_shadow_alias_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME texture_shadow_alias COMMAND dm_texture_shadow_alias_test)
    set_tests_properties(texture_shadow_alias PROPERTIES TIMEOUT 30)
    add_executable(dm_nvn_depth_bias_test tests/nvn_depth_bias_test.c)
    target_include_directories(dm_nvn_depth_bias_test PRIVATE native)
    target_compile_options(dm_nvn_depth_bias_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME nvn_depth_bias COMMAND dm_nvn_depth_bias_test)
    add_test(NAME nvn_depth_bias_base_behavior_rejected COMMAND dm_nvn_depth_bias_base_test)
    set_tests_properties(nvn_depth_bias nvn_depth_bias_base_behavior_rejected PROPERTIES TIMEOUT 30)
    set_tests_properties(nvn_depth_bias_base_behavior_rejected PROPERTIES WILL_FAIL TRUE)
    add_executable(dm_nvn_depth_bias_base_test tests/nvn_depth_bias_test.c)
    target_include_directories(dm_nvn_depth_bias_base_test PRIVATE native)
    target_compile_definitions(dm_nvn_depth_bias_base_test PRIVATE NVN_DEPTH_BIAS_BASE_BEHAVIOR)
    target_compile_options(dm_nvn_depth_bias_base_test PRIVATE -Wall -Wextra -Werror)

    add_executable(dm_live_touch_readiness_test tests/live_touch_readiness_test.c)
    target_include_directories(dm_live_touch_readiness_test PRIVATE native)
    target_compile_options(dm_live_touch_readiness_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME live_touch_readiness COMMAND dm_live_touch_readiness_test)
    set_tests_properties(live_touch_readiness PROPERTIES TIMEOUT 30)

    add_executable(dm_live_touch_visual_test tests/live_touch_visual_test.c ../ios/Sources/dm_touch_controls.c)
    target_link_libraries(dm_live_touch_visual_test PRIVATE m)
    target_include_directories(dm_live_touch_visual_test PRIVATE native ../ios/Sources ../host)
    target_compile_options(dm_live_touch_visual_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME live_touch_visual COMMAND dm_live_touch_visual_test)
    set_tests_properties(live_touch_visual PROPERTIES TIMEOUT 30)

    # #318 Android MAP placement and iOS preservation from the shared layout.
    add_executable(dm_map_button_layout_test tests/map_button_layout_test.c ../ios/Sources/dm_touch_controls.c)
    target_compile_definitions(dm_map_button_layout_test PRIVATE __ANDROID__)
    target_link_libraries(dm_map_button_layout_test PRIVATE m)
    target_include_directories(dm_map_button_layout_test PRIVATE ../ios/Sources ../host)
    target_compile_options(dm_map_button_layout_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME map_button_layout COMMAND dm_map_button_layout_test)
    set_tests_properties(map_button_layout PROPERTIES TIMEOUT 30)

    add_executable(dm_map_button_layout_ios_test tests/map_button_layout_test.c ../ios/Sources/dm_touch_controls.c)
    target_link_libraries(dm_map_button_layout_ios_test PRIVATE m)
    target_include_directories(dm_map_button_layout_ios_test PRIVATE ../ios/Sources ../host)
    target_compile_options(dm_map_button_layout_ios_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME map_button_layout_ios COMMAND dm_map_button_layout_ios_test)
    set_tests_properties(map_button_layout_ios PROPERTIES TIMEOUT 30)

    add_executable(dm_ui_trace_test tests/ui_trace_test.c)
    target_include_directories(dm_ui_trace_test PRIVATE ../host)
    target_compile_options(dm_ui_trace_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ui_trace COMMAND dm_ui_trace_test)
    set_tests_properties(ui_trace PROPERTIES TIMEOUT 10)

    add_executable(dm_live_owner_timer_test tests/live_owner_timer_test.c)
    target_include_directories(dm_live_owner_timer_test PRIVATE native)
    target_compile_options(dm_live_owner_timer_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME live_owner_timer COMMAND dm_live_owner_timer_test)
    set_tests_properties(live_owner_timer PROPERTIES TIMEOUT 10)

    add_executable(dm_fe_selection_test tests/fe_selection_test.c)
    target_include_directories(dm_fe_selection_test PRIVATE ../host)
    target_compile_options(dm_fe_selection_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME fe_selection COMMAND dm_fe_selection_test)
    set_tests_properties(fe_selection PROPERTIES TIMEOUT 10)

    add_executable(dm_touch_overlay_contract_test tests/touch_overlay_contract_test.c native/touch_overlay_contract.c)
    target_include_directories(dm_touch_overlay_contract_test PRIVATE native)
    target_compile_options(dm_touch_overlay_contract_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME touch_overlay_contract COMMAND dm_touch_overlay_contract_test)
    set_tests_properties(touch_overlay_contract PROPERTIES TIMEOUT 10)

    add_executable(dm_texture_deswizzle_formats_test tests/texture_deswizzle_formats_test.c ../gfx/nvn/nvn_tex.c ../gfx/tex/nvn_deswizzle.c)
    target_include_directories(dm_texture_deswizzle_formats_test PRIVATE native ../gfx/nvn ../gfx/tex)
    target_compile_options(dm_texture_deswizzle_formats_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME texture_deswizzle_formats COMMAND dm_texture_deswizzle_formats_test)
    set_tests_properties(texture_deswizzle_formats PROPERTIES TIMEOUT 30)

    # #253 protected heap snapshot lease; compiles the production hle_svc header.
    add_executable(dm_hle_heap_snapshot_test tests/hle_heap_snapshot.c)
    target_include_directories(dm_hle_heap_snapshot_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../host)
    target_compile_options(dm_hle_heap_snapshot_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(dm_hle_heap_snapshot_test PRIVATE Threads::Threads)
    add_test(NAME hle_heap_snapshot COMMAND dm_hle_heap_snapshot_test)
    set_tests_properties(hle_heap_snapshot PROPERTIES TIMEOUT 30)

    add_executable(dm_guest_snapshot_guard_test tests/guest_snapshot_guard.c)
    target_include_directories(dm_guest_snapshot_guard_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../host)
    target_compile_options(dm_guest_snapshot_guard_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(dm_guest_snapshot_guard_test PRIVATE Threads::Threads)
    add_test(NAME guest_snapshot_guard COMMAND dm_guest_snapshot_guard_test)
    set_tests_properties(guest_snapshot_guard PROPERTIES TIMEOUT 30)

    add_executable(dm_gpu_snapshot_ordering_test tests/gpu_snapshot_ordering.c)
    target_include_directories(dm_gpu_snapshot_ordering_test PRIVATE native)
    target_compile_options(dm_gpu_snapshot_ordering_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME gpu_snapshot_ordering COMMAND dm_gpu_snapshot_ordering_test)
    set_tests_properties(gpu_snapshot_ordering PROPERTIES TIMEOUT 30)

    if(APPLE)
        add_executable(dm_nvn_dirty_transaction_test tests/nvn_dirty_transaction.c)
        add_executable(dm_lock_spin_test ../host/tests/t_lock_spin.c ../host/tests/t_condvar_stub.c)
        target_compile_definitions(dm_lock_spin_test PRIVATE DM_TEST_HOOKS)
        target_compile_options(dm_lock_spin_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_lock_spin_test PRIVATE Threads::Threads)
        foreach(spin_us 0 20 200)
            add_test(NAME guest_lock_spin_${spin_us} COMMAND dm_lock_spin_test ${spin_us})
            set_tests_properties(guest_lock_spin_${spin_us} PROPERTIES TIMEOUT 30)
        endforeach()
        target_include_directories(dm_nvn_dirty_transaction_test PRIVATE ../gfx/nvn ../host)
        target_compile_definitions(dm_nvn_dirty_transaction_test PRIVATE DM_NVN_DIRTY_TEST)
        target_compile_options(dm_nvn_dirty_transaction_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_nvn_dirty_transaction_test PRIVATE Threads::Threads)
        add_test(NAME nvn_dirty_transaction COMMAND dm_nvn_dirty_transaction_test)
        set_tests_properties(nvn_dirty_transaction PROPERTIES TIMEOUT 30)

        add_executable(dm_nvn_dirty_write_runs_test tests/nvn_dirty_write_runs.c)
        target_compile_definitions(dm_nvn_dirty_write_runs_test PRIVATE DM_NVN_DIRTY_TEST)
        target_compile_options(dm_nvn_dirty_write_runs_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_nvn_dirty_write_runs_test PRIVATE Threads::Threads)
        add_test(NAME nvn_dirty_write_runs COMMAND dm_nvn_dirty_write_runs_test)
        set_tests_properties(nvn_dirty_write_runs PROPERTIES TIMEOUT 30)
    endif()

    if(APPLE)
        add_test(NAME hle_heap_apple_symbol
            COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/hle_heap_apple_symbol.sh
                ${CMAKE_C_COMPILER} ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/hle_heap_apple_symbol.o)
        set_tests_properties(hle_heap_apple_symbol PROPERTIES TIMEOUT 60)
    endif()

    # #246 module-create failure boundary; no Vulkan loader/device is opened.
    find_package(Threads REQUIRED)
    add_executable(dm_nvn_optional_module_create_test tests/nvn_optional_module_create.c)
    target_include_directories(dm_nvn_optional_module_create_test PRIVATE native)
    target_compile_options(dm_nvn_optional_module_create_test PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(dm_nvn_optional_module_create_test PRIVATE Threads::Threads)
    add_test(NAME nvn_optional_module_create COMMAND dm_nvn_optional_module_create_test)
    set_tests_properties(nvn_optional_module_create PROPERTIES TIMEOUT 30)

    # #230 draw-parameter capability/chain counterexample. The feature policy
    # is pure Vulkan metadata; no physical device or loader is opened here.
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        # #298 final-present cubic policy: absent extension, unsupported source
        # format and supported cubic are decided by the same production helper.
        add_executable(dm_nvn_present_filter_test tests/nvn_present_filter_test.c native/nvn_present_filter.c)
        target_include_directories(dm_nvn_present_filter_test PRIVATE native ${Vulkan_INCLUDE_DIRS})
        target_compile_options(dm_nvn_present_filter_test PRIVATE -Wall -Wextra -Werror)
        add_test(NAME nvn_present_filter COMMAND dm_nvn_present_filter_test)
        set_tests_properties(nvn_present_filter PROPERTIES TIMEOUT 30)

        add_executable(dm_nvn_draw_parameters_test tests/nvn_draw_parameters.c native/gpu_driver.c)
        target_include_directories(dm_nvn_draw_parameters_test PRIVATE native ${Vulkan_INCLUDE_DIRS})
        target_compile_options(dm_nvn_draw_parameters_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_nvn_draw_parameters_test PRIVATE ${CMAKE_DL_LIBS})
        add_test(NAME nvn_draw_parameters COMMAND dm_nvn_draw_parameters_test)
        set_tests_properties(nvn_draw_parameters PROPERTIES TIMEOUT 30)

        add_executable(dm_nvn_draw_parameters_reset_test tests/nvn_draw_parameters_reset.c)
        target_include_directories(dm_nvn_draw_parameters_reset_test PRIVATE native ${Vulkan_INCLUDE_DIRS})
        target_compile_options(dm_nvn_draw_parameters_reset_test PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(dm_nvn_draw_parameters_reset_test PRIVATE ${CMAKE_DL_LIBS})
        add_test(NAME nvn_draw_parameters_reset COMMAND dm_nvn_draw_parameters_reset_test)
        set_tests_properties(nvn_draw_parameters_reset PROPERTIES TIMEOUT 30)
    endif()
endif()

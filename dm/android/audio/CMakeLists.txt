# 自有 AAudio 输出模块；宿主只替换窄 AAudio 边界，Android 链接真实 libaaudio。
# Owned AAudio output; host replaces only the narrow AAudio boundary, Android links real libaaudio.
add_library(dm_android_audio STATIC dm_aaudio.c)
target_include_directories(dm_android_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(dm_android_audio PRIVATE -Wall -Wextra -Werror)
if(ANDROID)
    target_link_libraries(dm_android_audio PUBLIC aaudio)
endif()

if(NOT ANDROID)
    add_executable(dm_audio_aaudio_test ../tests/audio_aaudio.c dm_aaudio.c)
    target_include_directories(dm_audio_aaudio_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(dm_audio_aaudio_test PRIVATE DM_AUDIO_TEST)
    target_compile_options(dm_audio_aaudio_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME audio_aaudio COMMAND dm_audio_aaudio_test)
    set_tests_properties(audio_aaudio PROPERTIES TIMEOUT 30)
endif()

# 仅交叉链接检查：真实 AAudio 表 + 模块符号，不打开设备、不运行音频。
# Cross-link check only: real AAudio table + module symbols; no device open or audio execution.
if(ANDROID)
    add_executable(dm_audio_aaudio_cross ../tests/audio_aaudio_cross.c)
    target_link_libraries(dm_audio_aaudio_cross PRIVATE dm_android_audio)
    target_compile_options(dm_audio_aaudio_cross PRIVATE -Wall -Wextra -Werror)
    target_link_options(dm_audio_aaudio_cross PRIVATE "-Wl,-z,max-page-size=16384")
endif()

# Vulkan 底座在 Android 链接系统 loader，开发机通过已安装 Vulkan SDK 验证；着色器由宿主 glslc 构建。
# Link Android's system loader, verify on hosts with an installed Vulkan SDK; compile the synthetic shader using host glslc.
add_library(dm_android_vk STATIC vk_host.c vk_memory.c vk_compute.c vk_clear.c vk_shader.c vk_draw.c vk_texture.c vk_shader_reflect.c)
target_link_libraries(dm_android_vk PUBLIC dm_android_tex)
target_link_libraries(dm_android_vk PRIVATE dm_nso_dependencies)   # 复用已固定的 MbedTLS SHA-256 | reuse pinned MbedTLS SHA-256
target_include_directories(dm_android_vk PRIVATE ${dm_mbedtls_SOURCE_DIR}/include)
target_compile_options(dm_android_vk PRIVATE -Wall -Wextra -Werror)
if(ANDROID)
    target_link_libraries(dm_android_vk PUBLIC vulkan)
else()
    find_package(Vulkan REQUIRED)
    target_link_libraries(dm_android_vk PUBLIC Vulkan::Vulkan)
endif()
find_program(DM_GLSLC glslc REQUIRED)
set(pattern ${CMAKE_CURRENT_BINARY_DIR}/pattern.spv)
add_custom_command(OUTPUT ${pattern}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/pattern.comp -o ${pattern}
    DEPENDS pattern.comp VERBATIM)
set(vertex ${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv)
set(fragment ${CMAKE_CURRENT_BINARY_DIR}/pixels.frag.spv)
add_custom_command(OUTPUT ${vertex}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/fullscreen.vert -o ${vertex}
    DEPENDS fullscreen.vert VERBATIM)
add_custom_command(OUTPUT ${fragment}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/pixels.frag -o ${fragment}
    DEPENDS pixels.frag VERBATIM)
add_custom_target(dm_vk_shaders DEPENDS ${pattern} ${vertex} ${fragment})
# #12 反射夹具：与 vulkan_reflect.c 配套的自有计算着色器（无游戏内容） | #12 reflection fixture: owned compute shader
set(reflect_spv ${CMAKE_CURRENT_BINARY_DIR}/reflect.spv)
add_custom_command(OUTPUT ${reflect_spv}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/reflect.comp -o ${reflect_spv}
    DEPENDS ../tests/fixtures/reflect.comp VERBATIM)
add_custom_target(dm_vk_reflect_fixture DEPENDS ${reflect_spv})
# image 夹具只用于证明 image 被明确拒绝，不参与执行 | the image fixture only proves images are rejected, never executed
set(image_spv ${CMAKE_CURRENT_BINARY_DIR}/image.spv)
add_custom_command(OUTPUT ${image_spv}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/image.comp -o ${image_spv}
    DEPENDS ../tests/fixtures/image.comp VERBATIM)
add_custom_target(dm_vk_reflect_image_fixture DEPENDS ${image_spv})
add_executable(dm_vulkan_test ../tests/vulkan.c)
target_link_libraries(dm_vulkan_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_test dm_vk_shaders)
if(ANDROID)
    target_link_options(dm_vulkan_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_offscreen COMMAND dm_vulkan_test ${pattern} ${vertex} ${fragment})
    set_tests_properties(vulkan_offscreen PROPERTIES TIMEOUT 45)
    if(APPLE)
        find_file(DM_MOLTENVK_ICD MoltenVK_icd.json PATHS /opt/homebrew/etc/vulkan/icd.d /usr/local/etc/vulkan/icd.d)
        if(DM_MOLTENVK_ICD)
            set_tests_properties(vulkan_offscreen PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
        endif()
    endif()
endif()

# 两种真实视图分别使用 2D/数组采样器；提交错误由无 GPU 的注入测试覆盖。
# Sample real 2D/array views with matching samplers; a GPU-free injection test covers submission errors.
set(texture_2d ${CMAKE_CURRENT_BINARY_DIR}/sample_texture_2d.spv)
set(texture_array ${CMAKE_CURRENT_BINARY_DIR}/sample_texture_array.spv)
add_custom_command(OUTPUT ${texture_2d}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O -DDM_TEXTURE_2D=1 ${CMAKE_CURRENT_SOURCE_DIR}/sample_texture.comp -o ${texture_2d}
    DEPENDS sample_texture.comp VERBATIM)
add_custom_command(OUTPUT ${texture_array}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/sample_texture.comp -o ${texture_array}
    DEPENDS sample_texture.comp VERBATIM)
add_custom_target(dm_vk_texture_shaders DEPENDS ${texture_2d} ${texture_array})
add_executable(dm_vulkan_texture_test ../tests/vulkan_texture.c)
target_link_libraries(dm_vulkan_texture_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_texture_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_texture_test dm_vk_texture_shaders)
# NVN RGBA8_SRGB 回归：同一批字节按 sRGB / UNORM 两种映像采样，期望独立计算。 | NVN RGBA8_SRGB regression: the same bytes sampled as sRGB and UNORM images, expectations computed independently.
add_executable(dm_vulkan_texture_srgb_test ../tests/vulkan_texture_srgb.c)
target_link_libraries(dm_vulkan_texture_srgb_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_texture_srgb_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_texture_srgb_test dm_vk_texture_shaders)
add_executable(dm_vulkan_submit_test ../tests/vulkan_submit.c vk_host.c)
target_link_libraries(dm_vulkan_submit_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_submit_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkEndCommandBuffer vkCreateFence vkQueueSubmit vkWaitForFences vkDeviceWaitIdle vkDestroyFence vkFreeCommandBuffers)
    target_compile_definitions(dm_vulkan_submit_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()
if(ANDROID)
    target_link_options(dm_vulkan_texture_test PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_options(dm_vulkan_texture_srgb_test PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_options(dm_vulkan_submit_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_texture COMMAND dm_vulkan_texture_test ${texture_2d} ${texture_array})
    add_test(NAME vulkan_texture_srgb COMMAND dm_vulkan_texture_srgb_test ${texture_2d})
    add_test(NAME vulkan_submit COMMAND dm_vulkan_submit_test)
    set_tests_properties(vulkan_texture vulkan_texture_srgb vulkan_submit PROPERTIES TIMEOUT 45)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_texture vulkan_texture_srgb PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

add_executable(dm_vulkan_texture_rows_test ../tests/vulkan_texture_rows.c)
target_link_libraries(dm_vulkan_texture_rows_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_texture_rows_test PRIVATE -Wall -Wextra -Werror)
if(ANDROID)
    target_link_options(dm_vulkan_texture_rows_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_texture_rows COMMAND dm_vulkan_texture_rows_test)
    set_tests_properties(vulkan_texture_rows PROPERTIES TIMEOUT 45)
    # 同一个资源回归在真实验证层下再跑一次；层不可用 → 77 SKIP，绝不当作通过（#42）。
    # The same resource regression under the real validation layer; unavailable layer -> 77 SKIP, never a pass (#42).
    add_test(NAME vulkan_texture_rows_validation COMMAND dm_vulkan_texture_rows_test --validation)
    set_tests_properties(vulkan_texture_rows_validation PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_texture_rows PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
        set_tests_properties(vulkan_texture_rows_validation PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

add_executable(dm_vulkan_texture_failures_test ../tests/vulkan_texture_failures.c vk_texture.c vk_memory.c vk_host.c)
target_link_libraries(dm_vulkan_texture_failures_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_texture_failures_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceImageFormatProperties vkCreateImage
    vkGetImageMemoryRequirements vkAllocateMemory vkBindImageMemory vkCreateBuffer vkGetBufferMemoryRequirements
    vkBindBufferMemory vkMapMemory vkFlushMappedMemoryRanges vkInvalidateMappedMemoryRanges vkCreateImageView
    vkAllocateCommandBuffers vkBeginCommandBuffer
    vkCmdPipelineBarrier vkCmdCopyBufferToImage vkCmdCopyImageToBuffer vkEndCommandBuffer vkCreateFence vkQueueSubmit vkWaitForFences
    vkDeviceWaitIdle vkDestroyFence vkFreeCommandBuffers vkDestroyImageView vkDestroyImage vkDestroyBuffer vkUnmapMemory vkFreeMemory)
    target_compile_definitions(dm_vulkan_texture_failures_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()
if(ANDROID)
    target_link_options(dm_vulkan_texture_failures_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_texture_failures COMMAND dm_vulkan_texture_failures_test)
    set_tests_properties(vulkan_texture_failures PROPERTIES TIMEOUT 45)
endif()

# #42：验证层诊断契约（层不可用 → 77 SKIP）与实例创建失败清理（注入 loader 边界）。
# #42 validation contract (unavailable layer -> 77 SKIP) and instance-creation failure cleanup (injected loader boundary).
add_executable(dm_vulkan_validation_test ../tests/vulkan_validation.c)
target_link_libraries(dm_vulkan_validation_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_validation_test PRIVATE -Wall -Wextra -Werror)
add_executable(dm_vulkan_validation_createfail_test ../tests/vulkan_validation_createfail.c vk_host.c)
target_link_libraries(dm_vulkan_validation_createfail_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_validation_createfail_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(dm_vulkan_validation_createfail_test PRIVATE vkCreateInstance=dm_test_vkCreateInstance)
if(ANDROID)
    target_link_options(dm_vulkan_validation_test PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_options(dm_vulkan_validation_createfail_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_validation COMMAND dm_vulkan_validation_test)
    add_test(NAME vulkan_validation_createfail COMMAND dm_vulkan_validation_createfail_test)
    set_tests_properties(vulkan_validation PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77)
    set_tests_properties(vulkan_validation_createfail PROPERTIES TIMEOUT 45)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_validation PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

# 注入 loader 边界：层存在 + debug utils 可用、messenger 失败/清理、回调计数与并发读取（无需 GPU/真实层）。
# Injected loader boundary: layer present + debug utils, messenger failure/cleanup, callback counters and concurrent
# readers, without GPU or a real layer.
find_package(Threads REQUIRED)
add_executable(dm_vulkan_validation_fakeloader_test ../tests/vulkan_validation_fakeloader.c vk_host.c)
target_link_libraries(dm_vulkan_validation_fakeloader_test PRIVATE dm_android_vk Threads::Threads)
target_compile_options(dm_vulkan_validation_fakeloader_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkEnumerateInstanceLayerProperties vkEnumerateInstanceExtensionProperties vkCreateInstance vkDestroyInstance
    vkGetInstanceProcAddr vkEnumeratePhysicalDevices vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceProperties
    vkGetPhysicalDeviceMemoryProperties vkEnumerateDeviceExtensionProperties vkCreateDevice vkDestroyDevice vkGetDeviceQueue
    vkCreateCommandPool vkDestroyCommandPool vkDeviceWaitIdle)
    target_compile_definitions(dm_vulkan_validation_fakeloader_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()
option(DM_VK_SANITIZE_THREAD "Build the fake-loader validation test with ThreadSanitizer" OFF)
if(DM_VK_SANITIZE_THREAD)
    target_compile_options(dm_vulkan_validation_fakeloader_test PRIVATE -fsanitize=thread -g)
    target_link_options(dm_vulkan_validation_fakeloader_test PRIVATE -fsanitize=thread)
endif()
if(ANDROID)
    target_link_options(dm_vulkan_validation_fakeloader_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_validation_fakeloader COMMAND dm_vulkan_validation_fakeloader_test)
    set_tests_properties(vulkan_validation_fakeloader PROPERTIES TIMEOUT 60)
endif()

# #12 反射消费方回归：正例真实 GPU 执行 + 独立数值复算，负例在创建 Vulkan 对象前拒绝。
# #12 reflection consumer regression: real-GPU positive run with an independent oracle, negatives rejected before
# any Vulkan object exists.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/reflect-tmp)
add_executable(dm_vulkan_reflect_test ../tests/vulkan_reflect.c)
target_link_libraries(dm_vulkan_reflect_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_reflect_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_reflect_test dm_vk_reflect_fixture dm_vk_reflect_image_fixture)
if(ANDROID)
    target_link_options(dm_vulkan_reflect_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_reflect COMMAND dm_vulkan_reflect_test ${reflect_spv}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/reflect.res.json ${image_spv}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/image.res.json ${CMAKE_CURRENT_BINARY_DIR}/reflect-tmp)
    set_tests_properties(vulkan_reflect PROPERTIES TIMEOUT 60)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_reflect PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

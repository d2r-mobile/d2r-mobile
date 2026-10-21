# Vulkan 底座在 Android 链接系统 loader，开发机通过已安装 Vulkan SDK 验证；着色器由宿主 glslc 构建。
# Link Android's system loader, verify on hosts with an installed Vulkan SDK; compile the synthetic shader using host glslc.
add_library(dm_android_vk STATIC vk_host.c vk_memory.c vk_compute.c vk_clear.c vk_shader.c vk_draw.c vk_texture.c)
target_link_libraries(dm_android_vk PUBLIC dm_android_tex)
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
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_texture_rows PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
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

# Vulkan 底座在 Android 链接系统 loader，开发机通过已安装 Vulkan SDK 验证；着色器由宿主 glslc 构建。
# Link Android's system loader, verify on hosts with an installed Vulkan SDK; compile the synthetic shader using host glslc.
add_library(dm_android_vk STATIC vk_host.c vk_memory.c vk_compute.c vk_clear.c vk_shader.c vk_draw.c vk_indexed.c vk_indexed_textured.c vk_graphics_interface.c vk_texture.c vk_texture_mips.c vk_sampler.c vk_shader_reflect.c)
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
set(indexed_vert ${CMAKE_CURRENT_BINARY_DIR}/indexed.vert.spv)
add_custom_command(OUTPUT ${indexed_vert}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/indexed.vert -o ${indexed_vert}
    DEPENDS indexed.vert VERBATIM)
set(indexed_frag ${CMAKE_CURRENT_BINARY_DIR}/indexed.frag.spv)
add_custom_command(OUTPUT ${indexed_frag}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/indexed.frag -o ${indexed_frag}
    DEPENDS indexed.frag VERBATIM)
set(indexed_textured_vert ${CMAKE_CURRENT_BINARY_DIR}/indexed_textured.vert.spv)
add_custom_command(OUTPUT ${indexed_textured_vert}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/indexed_textured.vert -o ${indexed_textured_vert}
    DEPENDS indexed_textured.vert VERBATIM)
set(indexed_textured_frag ${CMAKE_CURRENT_BINARY_DIR}/indexed_textured.frag.spv)
add_custom_command(OUTPUT ${indexed_textured_frag}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/indexed_textured.frag -o ${indexed_textured_frag}
    DEPENDS indexed_textured.frag VERBATIM)
add_custom_target(dm_vk_indexed_shaders DEPENDS ${indexed_vert} ${indexed_frag} ${indexed_textured_vert} ${indexed_textured_frag})
set(graphics_bad_model ${CMAKE_CURRENT_BINARY_DIR}/graphics_bad_model.spv)
add_custom_command(OUTPUT ${graphics_bad_model}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/graphics_bad_model.comp -o ${graphics_bad_model}
    DEPENDS graphics_bad_model.comp VERBATIM)
set(graphics_bad_type ${CMAKE_CURRENT_BINARY_DIR}/graphics_bad_type.spv)
add_custom_command(OUTPUT ${graphics_bad_type}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/graphics_bad_type.vert -o ${graphics_bad_type}
    DEPENDS graphics_bad_type.vert VERBATIM)
set(graphics_bad_varying ${CMAKE_CURRENT_BINARY_DIR}/graphics_bad_varying.spv)
add_custom_command(OUTPUT ${graphics_bad_varying}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/graphics_bad_varying.vert -o ${graphics_bad_varying}
    DEPENDS graphics_bad_varying.vert VERBATIM)
set(graphics_bad_descriptor ${CMAKE_CURRENT_BINARY_DIR}/graphics_bad_descriptor.spv)
add_custom_command(OUTPUT ${graphics_bad_descriptor}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/graphics_bad_descriptor.vert -o ${graphics_bad_descriptor}
    DEPENDS graphics_bad_descriptor.vert VERBATIM)
set(graphics_bad_scalar ${CMAKE_CURRENT_BINARY_DIR}/graphics_bad_scalar.spv)
add_custom_command(OUTPUT ${graphics_bad_scalar}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/graphics_bad_scalar.vert -o ${graphics_bad_scalar}
    DEPENDS graphics_bad_scalar.vert VERBATIM)
set(graphics_bad_push ${CMAKE_CURRENT_BINARY_DIR}/graphics_bad_push.spv)
add_custom_command(OUTPUT ${graphics_bad_push}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/graphics_bad_push.vert -o ${graphics_bad_push}
    DEPENDS graphics_bad_push.vert VERBATIM)
add_custom_target(dm_vk_graphics_interface_fixtures DEPENDS ${graphics_bad_model} ${graphics_bad_type}
    ${graphics_bad_varying} ${graphics_bad_descriptor} ${graphics_bad_scalar} ${graphics_bad_push})
set(textured_bad_uv ${CMAKE_CURRENT_BINARY_DIR}/textured_bad_uv.spv)
add_custom_command(OUTPUT ${textured_bad_uv}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/textured_bad_uv.vert -o ${textured_bad_uv}
    DEPENDS textured_bad_uv.vert VERBATIM)
set(textured_bad_link ${CMAKE_CURRENT_BINARY_DIR}/textured_bad_link.spv)
add_custom_command(OUTPUT ${textured_bad_link}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/textured_bad_link.vert -o ${textured_bad_link}
    DEPENDS textured_bad_link.vert VERBATIM)
set(textured_bad_set ${CMAKE_CURRENT_BINARY_DIR}/textured_bad_set.spv)
add_custom_command(OUTPUT ${textured_bad_set}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/textured_bad_set.frag -o ${textured_bad_set}
    DEPENDS textured_bad_set.frag VERBATIM)
set(textured_bad_binding ${CMAKE_CURRENT_BINARY_DIR}/textured_bad_binding.spv)
add_custom_command(OUTPUT ${textured_bad_binding}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/textured_bad_binding.frag -o ${textured_bad_binding}
    DEPENDS textured_bad_binding.frag VERBATIM)
set(textured_bad_array ${CMAKE_CURRENT_BINARY_DIR}/textured_bad_array.spv)
add_custom_command(OUTPUT ${textured_bad_array}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/textured_bad_array.frag -o ${textured_bad_array}
    DEPENDS textured_bad_array.frag VERBATIM)
set(textured_extra_desc ${CMAKE_CURRENT_BINARY_DIR}/textured_extra_desc.spv)
add_custom_command(OUTPUT ${textured_extra_desc}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/textured_extra_desc.frag -o ${textured_extra_desc}
    DEPENDS textured_extra_desc.frag VERBATIM)
set(textured_no_desc ${CMAKE_CURRENT_BINARY_DIR}/textured_no_desc.spv)
add_custom_command(OUTPUT ${textured_no_desc}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/textured_no_desc.frag -o ${textured_no_desc}
    DEPENDS textured_no_desc.frag VERBATIM)
set(textured_uint ${CMAKE_CURRENT_BINARY_DIR}/textured_uint.spv)
add_custom_command(OUTPUT ${textured_uint}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/textured_uint.frag -o ${textured_uint}
    DEPENDS textured_uint.frag VERBATIM)
add_custom_target(dm_vk_textured_interface_fixtures DEPENDS ${textured_bad_uv} ${textured_bad_link}
    ${textured_bad_set} ${textured_bad_binding} ${textured_bad_array} ${textured_extra_desc}
    ${textured_no_desc} ${textured_uint})
add_custom_target(dm_vk_shaders DEPENDS ${pattern} ${vertex} ${fragment})
# #13 采样绘制片段着色器（自有合成着色器） | #13 sampled-draw fragment shader (owned synthetic shader)
set(sample_frag ${CMAKE_CURRENT_BINARY_DIR}/sample2d.frag.spv)
add_custom_command(OUTPUT ${sample_frag}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/sample2d.frag -o ${sample_frag}
    DEPENDS sample2d.frag VERBATIM)
add_custom_target(dm_vk_sampler_shaders DEPENDS ${sample_frag})
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
add_executable(dm_vulkan_graphics_interface_test ../tests/vulkan_graphics_interface.c)
target_link_libraries(dm_vulkan_graphics_interface_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_graphics_interface_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_graphics_interface_test dm_vk_indexed_shaders dm_vk_graphics_interface_fixtures)
if(ANDROID)
    target_link_options(dm_vulkan_graphics_interface_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_graphics_interface COMMAND dm_vulkan_graphics_interface_test
        ${indexed_vert} ${indexed_frag} ${graphics_bad_model} ${graphics_bad_type} ${graphics_bad_varying} ${graphics_bad_descriptor}
        ${graphics_bad_scalar} ${graphics_bad_push})
    set_tests_properties(vulkan_graphics_interface PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_graphics_interface PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

add_executable(dm_vulkan_textured_interface_test ../tests/vulkan_textured_interface.c)
target_link_libraries(dm_vulkan_textured_interface_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_textured_interface_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_textured_interface_test dm_vk_indexed_shaders dm_vk_graphics_interface_fixtures
    dm_vk_textured_interface_fixtures)
if(ANDROID)
    target_link_options(dm_vulkan_textured_interface_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_textured_interface COMMAND dm_vulkan_textured_interface_test
        ${indexed_textured_vert} ${indexed_textured_frag} ${textured_bad_uv} ${textured_bad_link}
        ${textured_bad_set} ${textured_bad_binding} ${textured_bad_array} ${textured_extra_desc}
        ${textured_no_desc} ${textured_uint} ${graphics_bad_model})
    set_tests_properties(vulkan_textured_interface PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_textured_interface PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

add_executable(dm_vulkan_crosskind_test ../tests/vulkan_crosskind.c vk_indexed.c vk_indexed_textured.c)
target_compile_definitions(dm_vulkan_crosskind_test PRIVATE vkGetPhysicalDeviceFormatProperties=dm_test_probe_format_properties)
target_link_libraries(dm_vulkan_crosskind_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_crosskind_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_crosskind_test dm_vk_indexed_shaders)
if(ANDROID)
    target_link_options(dm_vulkan_crosskind_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_crosskind COMMAND dm_vulkan_crosskind_test ${indexed_vert} ${indexed_frag}
        ${indexed_textured_vert} ${indexed_textured_frag})
    set_tests_properties(vulkan_crosskind PROPERTIES TIMEOUT 60)
endif()

add_executable(dm_vulkan_indexed_test ../tests/vulkan_indexed.c)
target_link_libraries(dm_vulkan_indexed_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_indexed_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_indexed_test dm_vk_indexed_shaders)
if(ANDROID)
    target_link_options(dm_vulkan_indexed_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_indexed COMMAND dm_vulkan_indexed_test ${indexed_vert} ${indexed_frag})
    set_tests_properties(vulkan_indexed PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_indexed PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

add_executable(dm_vulkan_indexed_fail_test ../tests/vulkan_indexed_fail.c vk_indexed.c vk_memory.c vk_host.c vk_shader.c)
target_link_libraries(dm_vulkan_indexed_fail_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_indexed_fail_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkGetPhysicalDeviceFormatProperties vkCreateBuffer vkGetBufferMemoryRequirements vkAllocateMemory
    vkBindBufferMemory vkMapMemory vkFlushMappedMemoryRanges vkInvalidateMappedMemoryRanges vkCreateImage
    vkGetImageMemoryRequirements vkBindImageMemory vkCreateImageView vkCreateRenderPass vkCreateFramebuffer
    vkCreateShaderModule vkDestroyShaderModule vkCreatePipelineLayout vkDestroyPipelineLayout vkCreateGraphicsPipelines
    vkDestroyPipeline vkDestroyFramebuffer vkDestroyRenderPass vkDestroyImageView vkDestroyImage vkDestroyBuffer
    vkUnmapMemory vkFreeMemory vkAllocateCommandBuffers vkBeginCommandBuffer vkEndCommandBuffer vkCreateFence
    vkQueueSubmit vkWaitForFences vkDeviceWaitIdle vkDestroyFence vkFreeCommandBuffers vkCmdBeginRenderPass
    vkCmdBindPipeline vkCmdBindVertexBuffers vkCmdBindIndexBuffer vkCmdDrawIndexed vkCmdEndRenderPass
    vkCmdCopyImageToBuffer vkCmdPipelineBarrier)
    target_compile_definitions(dm_vulkan_indexed_fail_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()
add_dependencies(dm_vulkan_indexed_fail_test dm_vk_indexed_shaders)
if(ANDROID)
    target_link_options(dm_vulkan_indexed_fail_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_indexed_fail COMMAND dm_vulkan_indexed_fail_test ${indexed_vert} ${indexed_frag})
    set_tests_properties(vulkan_indexed_fail PROPERTIES TIMEOUT 60)
endif()

add_executable(dm_vulkan_indexed_textured_test ../tests/vulkan_indexed_textured.c)
target_link_libraries(dm_vulkan_indexed_textured_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_indexed_textured_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_indexed_textured_test dm_vk_indexed_shaders)
if(ANDROID)
    target_link_options(dm_vulkan_indexed_textured_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_indexed_textured COMMAND dm_vulkan_indexed_textured_test ${indexed_textured_vert} ${indexed_textured_frag})
    set_tests_properties(vulkan_indexed_textured PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_indexed_textured PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

add_executable(dm_vulkan_indexed_textured_fail_test ../tests/vulkan_indexed_textured_fail.c
    vk_indexed_textured.c vk_texture_mips.c vk_texture.c vk_sampler.c vk_memory.c vk_host.c vk_shader.c)
target_link_libraries(dm_vulkan_indexed_textured_fail_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_indexed_textured_fail_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceImageFormatProperties vkCreateBuffer
    vkGetBufferMemoryRequirements vkAllocateMemory vkBindBufferMemory vkMapMemory vkFlushMappedMemoryRanges
    vkInvalidateMappedMemoryRanges vkCreateImage vkGetImageMemoryRequirements vkBindImageMemory vkCreateImageView
    vkCreateSampler vkDestroySampler vkCreateRenderPass vkCreateFramebuffer vkCreateShaderModule vkDestroyShaderModule
    vkCreateDescriptorSetLayout vkDestroyDescriptorSetLayout vkCreatePipelineLayout vkDestroyPipelineLayout
    vkCreateGraphicsPipelines vkDestroyPipeline vkCreateDescriptorPool vkDestroyDescriptorPool vkAllocateDescriptorSets
    vkUpdateDescriptorSets vkAllocateCommandBuffers vkBeginCommandBuffer vkEndCommandBuffer vkCreateFence
    vkQueueSubmit vkWaitForFences vkDeviceWaitIdle vkDestroyFence vkFreeCommandBuffers vkDestroyFramebuffer
    vkDestroyRenderPass vkDestroyImageView vkDestroyImage vkDestroyBuffer vkUnmapMemory vkFreeMemory
    vkCmdBeginRenderPass vkCmdBindPipeline vkCmdBindDescriptorSets vkCmdBindVertexBuffers vkCmdBindIndexBuffer
    vkCmdDrawIndexed vkCmdEndRenderPass vkCmdCopyBufferToImage vkCmdCopyImageToBuffer vkCmdPipelineBarrier)
    target_compile_definitions(dm_vulkan_indexed_textured_fail_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()
add_dependencies(dm_vulkan_indexed_textured_fail_test dm_vk_indexed_shaders)
if(ANDROID)
    target_link_options(dm_vulkan_indexed_textured_fail_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_indexed_textured_fail COMMAND dm_vulkan_indexed_textured_fail_test ${indexed_textured_vert} ${indexed_textured_frag})
    set_tests_properties(vulkan_indexed_textured_fail PROPERTIES TIMEOUT 60)
endif()

# #117 应用诊断场景的 GPU 交叉验证：同一自有场景走已验收 #115 接口，宿主 GPU 上核对 20 个检查点。
# #117 GPU cross-check of the app diagnostic scene: the same owned scene through the accepted #115 API, verifying
# the 20 probes on the host GPU.
add_executable(dm_vulkan_app_scene_test ../tests/vulkan_app_diagnostic_scene.c)
target_link_libraries(dm_vulkan_app_scene_test PRIVATE dm_android_vk dm_android_app_scene)
target_compile_options(dm_vulkan_app_scene_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_app_scene_test dm_vk_indexed_shaders)
if(ANDROID)
    target_link_options(dm_vulkan_app_scene_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_app_diagnostic_scene COMMAND dm_vulkan_app_scene_test ${indexed_textured_vert} ${indexed_textured_frag})
    set_tests_properties(vulkan_app_diagnostic_scene PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_app_diagnostic_scene PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

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

add_executable(dm_vulkan_texture_rows_subresource_test ../tests/vulkan_texture_rows_subresource.c vk_texture.c vk_memory.c vk_host.c)
target_link_libraries(dm_vulkan_texture_rows_subresource_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_texture_rows_subresource_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceImageFormatProperties vkCreateImage
    vkGetImageMemoryRequirements vkAllocateMemory vkBindImageMemory vkCreateBuffer vkGetBufferMemoryRequirements
    vkBindBufferMemory vkMapMemory vkFlushMappedMemoryRanges vkInvalidateMappedMemoryRanges vkCreateImageView
    vkAllocateCommandBuffers vkBeginCommandBuffer vkCmdPipelineBarrier vkCmdCopyBufferToImage vkCmdCopyImageToBuffer
    vkEndCommandBuffer vkCreateFence vkQueueSubmit vkWaitForFences vkDeviceWaitIdle vkDestroyFence
    vkFreeCommandBuffers vkDestroyImageView vkDestroyImage vkDestroyBuffer vkUnmapMemory vkFreeMemory)
    target_compile_definitions(dm_vulkan_texture_rows_subresource_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()
if(ANDROID)
    target_link_options(dm_vulkan_texture_rows_subresource_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_texture_rows_subresource COMMAND dm_vulkan_texture_rows_subresource_test)
    set_tests_properties(vulkan_texture_rows_subresource PROPERTIES TIMEOUT 45)
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

# #13 采样器：自有 2x2 纹理 + nearest/linear、clamp/repeat 数值对照，以及 loader 失败注入。
# #13 samplers: owned 2x2 texture with nearest/linear and clamp/repeat numeric checks, plus loader failure injection.
add_executable(dm_vulkan_sampler_test ../tests/vulkan_sampler.c)
target_link_libraries(dm_vulkan_sampler_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_sampler_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_sampler_test dm_vk_sampler_shaders dm_vk_shaders)

add_executable(dm_vulkan_sampler_createfail_test ../tests/vulkan_sampler_createfail.c vk_sampler.c)
target_link_libraries(dm_vulkan_sampler_createfail_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_sampler_createfail_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(dm_vulkan_sampler_createfail_test PRIVATE vkCreateSampler=dm_test_vkCreateSampler)

# #13 minf 映射的 loader 捕获：断言实际下发的 VkSamplerCreateInfo 字段。 | #13 loader capture asserting emitted fields.
add_executable(dm_vulkan_sampler_nvnmap_test ../tests/vulkan_sampler_nvnmap.c vk_sampler.c)
target_link_libraries(dm_vulkan_sampler_nvnmap_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_sampler_nvnmap_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(dm_vulkan_sampler_nvnmap_test PRIVATE
    vkCreateSampler=dm_test_vkCreateSampler vkDestroySampler=dm_test_vkDestroySampler)

# #13 多级纹理/LOD 像素证明 | #13 multi-mip texture and LOD pixel proof
add_executable(dm_vulkan_sampler_mips_test ../tests/vulkan_sampler_mips.c vk_sampler.c vk_texture_mips.c)
target_link_libraries(dm_vulkan_sampler_mips_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_sampler_mips_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_sampler_mips_test dm_vk_sampler_shaders dm_vk_shaders)

# #13 上传中途失败注入：图像/内存已建后视图创建失败 | #13 mid-pipeline upload failure after image/memory exist
add_executable(dm_vulkan_texture_mips_createfail_test ../tests/vulkan_texture_mips_createfail.c vk_texture_mips.c)
target_link_libraries(dm_vulkan_texture_mips_createfail_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_texture_mips_createfail_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(dm_vulkan_texture_mips_createfail_test PRIVATE
    vkCreateImage=dm_test_vkCreateImage vkAllocateMemory=dm_test_vkAllocateMemory
    vkCreateImageView=dm_test_vkCreateImageView vkDestroyImage=dm_test_vkDestroyImage
    vkDestroyImageView=dm_test_vkDestroyImageView vkFreeMemory=dm_test_vkFreeMemory)

# #95 多级上传逐阶段失败/非法输入/填充布局与设备丢失非完成回归 | #95 per-stage multi-level failure, invalid input,
# padded layout and device-loss-not-completion regression
add_executable(dm_vulkan_texture_mips_failures_test ../tests/vulkan_texture_mips_failures.c vk_texture_mips.c vk_texture.c vk_memory.c vk_host.c)
target_link_libraries(dm_vulkan_texture_mips_failures_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_texture_mips_failures_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkGetPhysicalDeviceImageFormatProperties vkCreateImage vkGetImageMemoryRequirements vkAllocateMemory
    vkBindImageMemory vkCreateBuffer vkGetBufferMemoryRequirements vkBindBufferMemory vkMapMemory
    vkFlushMappedMemoryRanges vkCreateImageView vkAllocateCommandBuffers vkBeginCommandBuffer vkCmdPipelineBarrier
    vkCmdCopyBufferToImage vkEndCommandBuffer vkCreateFence vkQueueSubmit vkWaitForFences vkDeviceWaitIdle
    vkDestroyFence vkFreeCommandBuffers vkDestroyImageView vkDestroyImage vkDestroyBuffer vkUnmapMemory vkFreeMemory)
    target_compile_definitions(dm_vulkan_texture_mips_failures_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()

if(ANDROID)
    target_link_options(dm_vulkan_sampler_test PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_options(dm_vulkan_sampler_createfail_test PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_options(dm_vulkan_sampler_mips_test PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_options(dm_vulkan_texture_mips_failures_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_sampler COMMAND dm_vulkan_sampler_test ${vertex} ${sample_frag})
    add_test(NAME vulkan_sampler_createfail COMMAND dm_vulkan_sampler_createfail_test)
    add_test(NAME vulkan_sampler_nvnmap COMMAND dm_vulkan_sampler_nvnmap_test)
    add_test(NAME vulkan_sampler_mips COMMAND dm_vulkan_sampler_mips_test ${vertex} ${sample_frag})
    add_test(NAME vulkan_texture_mips_createfail COMMAND dm_vulkan_texture_mips_createfail_test)
    add_test(NAME vulkan_texture_mips_failures COMMAND dm_vulkan_texture_mips_failures_test)
    set_tests_properties(vulkan_sampler PROPERTIES TIMEOUT 60)
    set_tests_properties(vulkan_sampler_createfail PROPERTIES TIMEOUT 60)
    set_tests_properties(vulkan_sampler_nvnmap PROPERTIES TIMEOUT 60)
    set_tests_properties(vulkan_sampler_mips PROPERTIES TIMEOUT 60)
    set_tests_properties(vulkan_texture_mips_createfail PROPERTIES TIMEOUT 60)
    set_tests_properties(vulkan_texture_mips_failures PROPERTIES TIMEOUT 60)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_sampler PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
        set_tests_properties(vulkan_sampler_createfail PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
        set_tests_properties(vulkan_sampler_nvnmap PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
        set_tests_properties(vulkan_sampler_mips PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
        set_tests_properties(vulkan_texture_mips_createfail PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

# #12 反射消费方回归：正例真实 GPU 执行 + 独立数值复算，负例在创建 Vulkan 对象前拒绝。
# #12 reflection consumer regression: real-GPU positive run with an independent oracle, negatives rejected before
# any Vulkan object exists.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/reflect-tmp)
set(sampled_spv ${CMAKE_CURRENT_BINARY_DIR}/sampled.spv)
add_custom_command(OUTPUT ${sampled_spv}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/sampled.comp -o ${sampled_spv}
    DEPENDS ../tests/fixtures/sampled.comp VERBATIM)
add_custom_target(dm_vk_sampled_fixture DEPENDS ${sampled_spv})
set(sampled_uint_spv ${CMAKE_CURRENT_BINARY_DIR}/sampled_uint.spv)
set(sampled_sint_spv ${CMAKE_CURRENT_BINARY_DIR}/sampled_sint.spv)
set(sampled_shadow_spv ${CMAKE_CURRENT_BINARY_DIR}/sampled_shadow.spv)
foreach(pair "sampled_uint:${sampled_uint_spv}" "sampled_sint:${sampled_sint_spv}" "sampled_shadow:${sampled_shadow_spv}")
    string(REPLACE ":" ";" pair_list "${pair}")
    list(GET pair_list 0 srcname)
    list(GET pair_list 1 outspv)
    add_custom_command(OUTPUT ${outspv}
        COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/${srcname}.comp -o ${outspv}
        DEPENDS ../tests/fixtures/${srcname}.comp VERBATIM)
    list(APPEND sampled_negative_spv ${outspv})
endforeach()
add_custom_target(dm_vk_sampled_negative_fixtures DEPENDS ${sampled_negative_spv})
add_executable(dm_vulkan_reflect_sampled_test ../tests/vulkan_reflect_sampled.c)
target_link_libraries(dm_vulkan_reflect_sampled_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_reflect_sampled_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_reflect_sampled_test dm_vk_sampled_fixture dm_vk_sampled_negative_fixtures)
if(ANDROID)
    target_link_options(dm_vulkan_reflect_sampled_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_reflect_sampled COMMAND dm_vulkan_reflect_sampled_test ${sampled_spv}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/sampled.res.json
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/reflect.res.json
        ${sampled_uint_spv} ${sampled_sint_spv} ${sampled_shadow_spv})
    set_tests_properties(vulkan_reflect_sampled PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_reflect_sampled PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

add_executable(dm_vulkan_reflect_sampled_fail_test ../tests/vulkan_reflect_sampled_fail.c vk_shader_reflect.c vk_memory.c vk_host.c)
target_link_libraries(dm_vulkan_reflect_sampled_fail_test PRIVATE dm_android_vk)
target_include_directories(dm_vulkan_reflect_sampled_fail_test PRIVATE ${dm_mbedtls_SOURCE_DIR}/include)
target_compile_options(dm_vulkan_reflect_sampled_fail_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkCreateBuffer vkGetBufferMemoryRequirements vkAllocateMemory vkBindBufferMemory vkMapMemory
    vkInvalidateMappedMemoryRanges vkCreateShaderModule vkDestroyShaderModule vkCreateDescriptorSetLayout
    vkDestroyDescriptorSetLayout vkCreatePipelineLayout vkDestroyPipelineLayout vkCreateComputePipelines
    vkDestroyPipeline vkCreateDescriptorPool vkDestroyDescriptorPool vkAllocateDescriptorSets
    vkUpdateDescriptorSets vkAllocateCommandBuffers vkBeginCommandBuffer vkEndCommandBuffer vkCreateFence
    vkQueueSubmit vkWaitForFences vkDeviceWaitIdle vkDestroyFence vkFreeCommandBuffers vkDestroyBuffer
    vkUnmapMemory vkFreeMemory vkCmdBindPipeline vkCmdBindDescriptorSets vkCmdPushConstants vkCmdDispatch
    vkCmdPipelineBarrier vkDestroyImage vkDestroyImageView vkDestroySampler)
    target_compile_definitions(dm_vulkan_reflect_sampled_fail_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()
if(ANDROID)
    target_link_options(dm_vulkan_reflect_sampled_fail_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_reflect_sampled_fail COMMAND dm_vulkan_reflect_sampled_fail_test ${sampled_spv}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/sampled.res.json)
    set_tests_properties(vulkan_reflect_sampled_fail PROPERTIES TIMEOUT 60)
endif()

# #109 fixed-layout uniform fixture: one 16-byte std140 uvec4 + output storage buffer
set(uniform_spv ${CMAKE_CURRENT_BINARY_DIR}/uniform.spv)
add_custom_command(OUTPUT ${uniform_spv}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/uniform.comp -o ${uniform_spv}
    DEPENDS ../tests/fixtures/uniform.comp VERBATIM)
set(uniform_bad_spv ${CMAKE_CURRENT_BINARY_DIR}/uniform_bad.spv)
add_custom_command(OUTPUT ${uniform_bad_spv}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/uniform_bad.comp -o ${uniform_bad_spv}
    DEPENDS ../tests/fixtures/uniform_bad.comp VERBATIM)
set(uniform_array_spv ${CMAKE_CURRENT_BINARY_DIR}/uniform_array.spv)
add_custom_command(OUTPUT ${uniform_array_spv}
    COMMAND ${DM_GLSLC} --target-env=vulkan1.1 -O ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/uniform_array.comp -o ${uniform_array_spv}
    DEPENDS ../tests/fixtures/uniform_array.comp VERBATIM)
add_custom_target(dm_vk_uniform_fixture DEPENDS ${uniform_spv} ${uniform_bad_spv} ${uniform_array_spv})
add_executable(dm_vulkan_reflect_uniform_test ../tests/vulkan_reflect_uniform.c)
target_link_libraries(dm_vulkan_reflect_uniform_test PRIVATE dm_android_vk)
target_compile_options(dm_vulkan_reflect_uniform_test PRIVATE -Wall -Wextra -Werror)
add_dependencies(dm_vulkan_reflect_uniform_test dm_vk_uniform_fixture)
if(ANDROID)
    target_link_options(dm_vulkan_reflect_uniform_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_reflect_uniform COMMAND dm_vulkan_reflect_uniform_test ${uniform_spv}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/uniform.res.json ${uniform_bad_spv} ${uniform_array_spv})
    set_tests_properties(vulkan_reflect_uniform PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    if(APPLE AND DM_MOLTENVK_ICD)
        set_tests_properties(vulkan_reflect_uniform PROPERTIES ENVIRONMENT "VK_DRIVER_FILES=${DM_MOLTENVK_ICD}")
    endif()
endif()

add_executable(dm_vulkan_reflect_uniform_fail_test ../tests/vulkan_reflect_uniform_fail.c vk_shader_reflect.c vk_memory.c vk_host.c)
target_link_libraries(dm_vulkan_reflect_uniform_fail_test PRIVATE dm_android_vk)
target_include_directories(dm_vulkan_reflect_uniform_fail_test PRIVATE ${dm_mbedtls_SOURCE_DIR}/include)
target_compile_options(dm_vulkan_reflect_uniform_fail_test PRIVATE -Wall -Wextra -Werror)
foreach(symbol vkCreateBuffer vkGetBufferMemoryRequirements vkAllocateMemory vkBindBufferMemory vkMapMemory
    vkFlushMappedMemoryRanges vkInvalidateMappedMemoryRanges vkCreateShaderModule vkDestroyShaderModule
    vkCreateDescriptorSetLayout vkDestroyDescriptorSetLayout vkCreatePipelineLayout vkDestroyPipelineLayout
    vkCreateComputePipelines vkDestroyPipeline vkCreateDescriptorPool vkDestroyDescriptorPool vkAllocateDescriptorSets
    vkUpdateDescriptorSets vkAllocateCommandBuffers vkBeginCommandBuffer vkEndCommandBuffer vkCreateFence
    vkQueueSubmit vkWaitForFences vkDeviceWaitIdle vkDestroyFence vkFreeCommandBuffers vkDestroyBuffer
    vkUnmapMemory vkFreeMemory vkCmdBindPipeline vkCmdBindDescriptorSets vkCmdPushConstants vkCmdDispatch
    vkCmdPipelineBarrier)
    target_compile_definitions(dm_vulkan_reflect_uniform_fail_test PRIVATE ${symbol}=dm_test_${symbol})
endforeach()
if(ANDROID)
    target_link_options(dm_vulkan_reflect_uniform_fail_test PRIVATE "-Wl,-z,max-page-size=16384")
else()
    add_test(NAME vulkan_reflect_uniform_fail COMMAND dm_vulkan_reflect_uniform_fail_test ${uniform_spv}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/uniform.res.json)
    set_tests_properties(vulkan_reflect_uniform_fail PROPERTIES TIMEOUT 60)
endif()

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

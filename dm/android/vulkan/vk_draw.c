// 离屏图形管线／render pass／draw／读回；依据 Vulkan 1.1 与 Khronos 同步示例，不包含 NVN 游戏语义。
// Offscreen graphics pipeline / render pass / draw / readback; based on Vulkan 1.1 and Khronos synchronization examples, without game NVN semantics.
#include "vk_host.h"
#include <string.h>
#define TRY(call)do{result=(call);if(result!=VK_SUCCESS)goto done;}while(0)
VkResult dm_vk_draw_pixels(dm_vk*vk,const char*vertex_path,const char*fragment_path,dm_vk_pixels*pixels){
    if(!vk||!vk->device||!pixels||!pixels->bytes||!pixels->width||!pixels->height||pixels->width>16384||pixels->height>16384||
        pixels->width>vk->properties.limits.maxImageDimension2D||pixels->height>vk->properties.limits.maxImageDimension2D)
        return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t size=(uint64_t)pixels->width*pixels->height*4;if(size>pixels->capacity)return VK_ERROR_INITIALIZATION_FAILED;
    VkFormatProperties format;vkGetPhysicalDeviceFormatProperties(vk->physical,VK_FORMAT_R8G8B8A8_UNORM,&format);
    VkFormatFeatureFlags need=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if((format.optimalTilingFeatures&need)!=need)return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;
    VkRenderPass render=VK_NULL_HANDLE;VkFramebuffer framebuffer=VK_NULL_HANDLE;
    VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;dm_vk_buffer buffer={0};VkResult result;
    TRY(dm_vk_shader_load(vk,vertex_path,&vertex));TRY(dm_vk_shader_load(vk,fragment_path,&fragment));
    VkImageCreateInfo create={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={pixels->width,pixels->height,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    TRY(vkCreateImage(vk->device,&create,NULL,&image));VkMemoryRequirements requirements;vkGetImageMemoryRequirements(vk->device,image,&requirements);
    uint32_t selected=UINT32_MAX;for(uint32_t i=0;i<vk->memory.memoryTypeCount;i++)if(requirements.memoryTypeBits&(1u<<i)){
        selected=i;if(vk->memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)break;}
    if(selected==UINT32_MAX){result=VK_ERROR_FEATURE_NOT_PRESENT;goto done;}
    VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=selected};
    TRY(vkAllocateMemory(vk->device,&allocate,NULL,&memory));TRY(vkBindImageMemory(vk->device,image,memory,0));
    VkImageViewCreateInfo view_info={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1}};
    TRY(vkCreateImageView(vk->device,&view_info,NULL,&view));
    VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference reference={.attachment=0,.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&reference};
    VkSubpassDependency dependency={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo pass={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,
        .subpassCount=1,.pSubpasses=&subpass,.dependencyCount=1,.pDependencies=&dependency};
    TRY(vkCreateRenderPass(vk->device,&pass,NULL,&render));
    VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=render,.attachmentCount=1,
        .pAttachments=&view,.width=pixels->width,.height=pixels->height,.layers=1};TRY(vkCreateFramebuffer(vk->device,&fb,NULL,&framebuffer));
    VkPipelineLayoutCreateInfo layout_info={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    TRY(vkCreatePipelineLayout(vk->device,&layout_info,NULL,&layout));
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo input={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport={.width=(float)pixels->width,.height=(float)pixels->height,.maxDepth=1};VkRect2D scissor={.extent={pixels->width,pixels->height}};
    VkPipelineViewportStateCreateInfo viewport_info={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend={.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blending={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};
    VkGraphicsPipelineCreateInfo graphics={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,
        .pVertexInputState=&input,.pInputAssemblyState=&assembly,.pViewportState=&viewport_info,.pRasterizationState=&raster,
        .pMultisampleState=&samples,.pColorBlendState=&blending,.layout=layout,.renderPass=render,.basePipelineIndex=-1};
    TRY(vkCreateGraphicsPipelines(vk->device,VK_NULL_HANDLE,1,&graphics,NULL,&pipeline));
    dm_vk_buffer_info buffer_info={.size=size,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};TRY(dm_vk_buffer_create(vk,&buffer_info,&buffer));
    TRY(dm_vk_begin(vk,&command));VkClearValue clear={.color={.float32={0,0,0,1}}};
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=render,.framebuffer=framebuffer,
        .renderArea={.extent={pixels->width,pixels->height}},.clearValueCount=1,.pClearValues=&clear};
    vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
    VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,
        .subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1}};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
    VkBufferImageCopy copy={.imageSubresource={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.layerCount=1},.imageExtent={pixels->width,pixels->height,1}};
    vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer.handle,1,&copy);
    VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buffer.handle,.size=VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
    result=dm_vk_submit(vk,command);command=VK_NULL_HANDLE;if(result!=VK_SUCCESS)goto done;
    TRY(dm_vk_buffer_sync(vk,&buffer,0));memcpy(pixels->bytes,buffer.mapped,(size_t)size);
done:
    if(command)vkFreeCommandBuffers(vk->device,vk->pool,1,&command);dm_vk_buffer_destroy(vk,&buffer);
    if(pipeline)vkDestroyPipeline(vk->device,pipeline,NULL);if(layout)vkDestroyPipelineLayout(vk->device,layout,NULL);
    if(framebuffer)vkDestroyFramebuffer(vk->device,framebuffer,NULL);if(render)vkDestroyRenderPass(vk->device,render,NULL);
    if(vertex)vkDestroyShaderModule(vk->device,vertex,NULL);if(fragment)vkDestroyShaderModule(vk->device,fragment,NULL);
    if(view)vkDestroyImageView(vk->device,view,NULL);if(image)vkDestroyImage(vk->device,image,NULL);if(memory)vkFreeMemory(vk->device,memory,NULL);
    return result;
}

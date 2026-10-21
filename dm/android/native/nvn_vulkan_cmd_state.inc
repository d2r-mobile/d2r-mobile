// Vulkan 命令状态在每个 command buffer 开始时清空；相同状态无需反复发送驱动命令。
// Clear Vulkan command state at each command-buffer begin; identical state needs no repeated driver commands.
typedef struct graphics_command_state {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSet sets[2];
    uint32_t dynamic_offsets[256];unsigned dynamic_count;
    VkViewport viewport;
    VkRect2D scissor;
    float blend[4],bias_units,bias_factor;
    uint32_t stencil_compare[2],stencil_write[2],stencil_reference[2];
    VkBuffer vertices[16],index;
    VkDeviceSize offsets[16],index_offset;
    VkIndexType index_type;
    uint32_t valid;
} graphics_command_state;
static graphics_command_state graphics_commands;
static uint64_t graphics_command_emitted,graphics_command_reused;
static void graphics_command_reset(void) {memset(&graphics_commands,0,sizeof graphics_commands);}
static void graphics_command_apply(VkPipeline pipeline,VkPipelineLayout layout,const VkDescriptorSet sets[2],
    const uint32_t *dynamic_offsets,unsigned dynamic_count,
    const VkViewport *viewport,const VkRect2D *scissor,const float blend[4],float units,float factor,const VkStencilOpState faces[2]) {
    graphics_command_state *last=&graphics_commands;
    if(last->pipeline!=pipeline){vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);last->pipeline=pipeline;graphics_command_emitted++;}else graphics_command_reused++;
    // 动态偏移同属绑定状态：只比集合不看偏移，复用的就是上一次的位置。
    // Dynamic offsets are part of the binding state: comparing sets alone would reuse the previous positions.
    if(last->layout!=layout||memcmp(last->sets,sets,sizeof last->sets)||last->dynamic_count!=dynamic_count||memcmp(last->dynamic_offsets,dynamic_offsets,dynamic_count*sizeof *dynamic_offsets)){vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,2,sets,dynamic_count,dynamic_offsets);last->layout=layout;memcpy(last->sets,sets,sizeof last->sets);last->dynamic_count=dynamic_count;memcpy(last->dynamic_offsets,dynamic_offsets,dynamic_count*sizeof *dynamic_offsets);graphics_command_emitted++;}else graphics_command_reused++;
    if(!(last->valid&1)||memcmp(&last->viewport,viewport,sizeof *viewport)){vkCmdSetViewport(command,0,1,viewport);last->viewport=*viewport;graphics_command_emitted++;}else graphics_command_reused++;
    if(!(last->valid&2)||memcmp(&last->scissor,scissor,sizeof *scissor)){vkCmdSetScissor(command,0,1,scissor);last->scissor=*scissor;graphics_command_emitted++;}else graphics_command_reused++;
    if(!(last->valid&4)||memcmp(last->blend,blend,sizeof last->blend)){vkCmdSetBlendConstants(command,blend);memcpy(last->blend,blend,sizeof last->blend);graphics_command_emitted++;}else graphics_command_reused++;
    if(!(last->valid&8)||last->bias_units!=units||last->bias_factor!=factor){vkCmdSetDepthBias(command,units,0,factor);last->bias_units=units;last->bias_factor=factor;graphics_command_emitted++;}else graphics_command_reused++;
    for(unsigned face=0;face<2;face++){
        VkStencilFaceFlags mask=face?VK_STENCIL_FACE_BACK_BIT:VK_STENCIL_FACE_FRONT_BIT;
        uint32_t compare_bit=16u<<face,write_bit=64u<<face,reference_bit=256u<<face;
        if(!(last->valid&compare_bit)||last->stencil_compare[face]!=faces[face].compareMask){vkCmdSetStencilCompareMask(command,mask,faces[face].compareMask);last->stencil_compare[face]=faces[face].compareMask;graphics_command_emitted++;}else graphics_command_reused++;
        if(!(last->valid&write_bit)||last->stencil_write[face]!=faces[face].writeMask){vkCmdSetStencilWriteMask(command,mask,faces[face].writeMask);last->stencil_write[face]=faces[face].writeMask;graphics_command_emitted++;}else graphics_command_reused++;
        if(!(last->valid&reference_bit)||last->stencil_reference[face]!=faces[face].reference){vkCmdSetStencilReference(command,mask,faces[face].reference);last->stencil_reference[face]=faces[face].reference;graphics_command_emitted++;}else graphics_command_reused++;
    }
    last->valid=1023;
}
static void graphics_command_vertices(unsigned slot,VkBuffer buffer,VkDeviceSize offset) {
    if(slot>=16)die("Vulkan command vertex slot");graphics_command_state *last=&graphics_commands;
    if(last->vertices[slot]!=buffer||last->offsets[slot]!=offset){vkCmdBindVertexBuffers(command,slot,1,&buffer,&offset);last->vertices[slot]=buffer;last->offsets[slot]=offset;graphics_command_emitted++;}else graphics_command_reused++;
}
static void graphics_command_index(VkBuffer buffer,VkDeviceSize offset,VkIndexType type) {
    graphics_command_state *last=&graphics_commands;
    if(last->index!=buffer||last->index_offset!=offset||last->index_type!=type){vkCmdBindIndexBuffer(command,buffer,offset,type);last->index=buffer;last->index_offset=offset;last->index_type=type;graphics_command_emitted++;}else graphics_command_reused++;
}

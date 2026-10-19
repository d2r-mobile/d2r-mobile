// GPU 消费者复用先前输出；需要客体 CPU 数据的索引、uniform 和贴图解排布仍走真正完成等待。
// GPU consumers reuse prior outputs; indices, uniforms and texture deswizzling that require guest CPU data still wait for real completion.
static uint64_t forward_read_bytes,forward_seed_bytes;
#include "nvn_vulkan_forward_compose.inc"
static VkDescriptorBufferInfo forward_readonly_buffer_mode(uintptr_t address,uint64_t bytes,uint64_t alignment,VkPipelineStageFlags stage,VkAccessFlags access,int gpu_only) {
    forward_slice slice;forward_result result=forward_find(address,bytes,alignment,forward_scope_marker,&slice);
    if(snapshot_diag_source_current){snapshot_diag_forward=result;snapshot_diag_current=snapshot_diag_source(snapshot_diag_source_current,address,bytes);if(snapshot_diag_current)snapshot_diag_current->forward[result]++;}
    if(result==FORWARD_FULL){
        forward_read_barrier(&slice,stage,access);forward_hold(&slice);forward_read_bytes+=bytes;return (VkDescriptorBufferInfo){slice.buffer,slice.offset,bytes};
    }
    if(result!=FORWARD_NONE&&forward_compose_possible(address,bytes,forward_scope_marker))return forward_compose_buffer(address,bytes,stage,access,forward_scope_marker);
    vk_buffer buffer=gpu_only?gpu_snapshot_buffer(address,bytes):snapshot_buffer(address,bytes);return (VkDescriptorBufferInfo){buffer.buffer,0,bytes};
}
static VkDescriptorBufferInfo forward_readonly_buffer(uintptr_t address,uint64_t bytes,uint64_t alignment,VkPipelineStageFlags stage,VkAccessFlags access) {return forward_readonly_buffer_mode(address,bytes,alignment,stage,access,0);}
// 仅给已确认的只读 shader 路径附来源；原 forward 查询与消费同步仍各执行一次。
// Tag only the confirmed read-only shader paths; original forward lookup and consumer synchronization still execute once each.
static VkDescriptorBufferInfo forward_readonly_shader_buffer(const shader_entry *shader,const shader_binding *binding,unsigned element,unsigned slot,unsigned handle,unsigned role,uint64_t bound_bytes,uintptr_t address,uint64_t bytes,uint64_t alignment,VkPipelineStageFlags stage,VkAccessFlags access) {
    int gpu_only=(role==1||role==2)&&bytes>65536;
    if(!snapshot_diag_frame||bytes<=65536)return forward_readonly_buffer_mode(address,bytes,alignment,stage,access,gpu_only);
    snapshot_source source={shader->key,shader->stage,shader->set,binding->binding,element,binding->kind,binding->flags,binding->cb_slot,binding->cb_offset,slot,handle,role,bound_bytes};
    const snapshot_source *previous_source=snapshot_diag_source_current;snapshot_diag_row *previous_row=snapshot_diag_current;forward_result previous_forward=snapshot_diag_forward;
    snapshot_diag_source_current=&source;snapshot_diag_current=NULL;
    VkDescriptorBufferInfo result=forward_readonly_buffer_mode(address,bytes,alignment,stage,access,gpu_only);
    snapshot_diag_source_current=previous_source;snapshot_diag_current=previous_row;snapshot_diag_forward=previous_forward;return result;
}
static void forward_seed(vk_buffer *output,uintptr_t address,uint64_t bytes,VkPipelineStageFlags stage) {
    forward_slice slice;forward_result result=(bytes&3)?FORWARD_NONE:forward_find(address,bytes,4,forward_scope_marker,&slice);
    if(result==FORWARD_FULL){
        graphics_pass_end();forward_read_barrier(&slice,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);forward_hold(&slice);
        VkBufferCopy copy={.srcOffset=slice.offset,.size=bytes};vkCmdCopyBuffer(command,slice.buffer,output->buffer,1,&copy);forward_seed_bytes+=bytes;
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output->buffer,.size=bytes};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,stage,0,0,NULL,1,&barrier,0,NULL);return;
    }
    if(result!=FORWARD_NONE&&forward_compose_possible(address,bytes,forward_scope_marker)){
        forward_compose_into(*output,address,bytes,stage,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,forward_scope_marker);return;
    }
    if(snapshot_read(address,output->mapped,bytes))die("Vulkan storage initial contents unreadable");
}
static void forward_record_outputs(post_op *const *scope,VkPipelineStageFlags stage) {
    for(post_op *post=*scope;post;post=post->copy_next)if(!post->source.buffer&&post->destination&&post->size)
        forward_mark_gpu_recorded(post,stage,VK_ACCESS_SHADER_WRITE_BIT,1);
}

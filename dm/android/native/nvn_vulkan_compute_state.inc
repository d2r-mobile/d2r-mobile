typedef struct compute_command_state {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSet set;
    uint32_t offsets[256];
    unsigned count,valid;
} compute_command_state;
static compute_command_state compute_commands;
static void compute_command_reset(void) {memset(&compute_commands,0,sizeof compute_commands);}
static void compute_command_apply(VkPipeline pipeline,VkPipelineLayout layout,VkDescriptorSet set,
                                  unsigned count,const uint32_t *offsets) {
    if(count>256)die("Vulkan compute dynamic offset capacity");
    compute_command_state *last=&compute_commands;
    if(!last->valid||last->pipeline!=pipeline)
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    if(!last->valid||last->layout!=layout||last->set!=set||last->count!=count||
       (count&&memcmp(last->offsets,offsets,count*sizeof *offsets))){
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,count,offsets);
        if(count)memcpy(last->offsets,offsets,count*sizeof *offsets);
    }
    last->pipeline=pipeline;last->layout=layout;last->set=set;last->count=count;last->valid=1;
}

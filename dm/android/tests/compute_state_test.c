#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uintptr_t VkPipeline, VkPipelineLayout, VkDescriptorSet;
enum { VK_PIPELINE_BIND_POINT_COMPUTE=1 };
static uintptr_t command=1;
static unsigned pipeline_calls,set_calls;
static VkPipeline bound_pipeline;
static VkPipelineLayout bound_layout;
static VkDescriptorSet bound_set;
static uint32_t bound_offsets[256];
static unsigned bound_count;
#define die(...) abort()
static void vkCmdBindPipeline(uintptr_t cmd,unsigned point,VkPipeline pipeline) {
    assert(cmd==command&&point==VK_PIPELINE_BIND_POINT_COMPUTE);
    pipeline_calls++;bound_pipeline=pipeline;
}
static void vkCmdBindDescriptorSets(uintptr_t cmd,unsigned point,VkPipelineLayout layout,unsigned first,
                                    unsigned sets,const VkDescriptorSet *set,unsigned count,const uint32_t *offsets) {
    assert(cmd==command&&point==VK_PIPELINE_BIND_POINT_COMPUTE&&first==0&&sets==1&&count<=256);
    set_calls++;bound_layout=layout;bound_set=*set;bound_count=count;
    if(count)memcpy(bound_offsets,offsets,count*sizeof *offsets);
}
#include "../native/nvn_vulkan_compute_state.inc"
static void check(unsigned pipelines,unsigned sets,VkPipeline pipeline,VkPipelineLayout layout,
                  VkDescriptorSet set,unsigned count,const uint32_t *offsets) {
    assert(pipeline_calls==pipelines&&set_calls==sets);
    assert(bound_pipeline==pipeline&&bound_layout==layout&&bound_set==set&&bound_count==count);
    assert(!count||!memcmp(bound_offsets,offsets,count*sizeof *offsets));
}
int main(void) {
    uint32_t offsets[256]={64,128};
    compute_command_reset();compute_command_apply(11,21,31,2,offsets);
    check(1,1,11,21,31,2,offsets);
    for(unsigned i=0;i<100;i++)compute_command_apply(11,21,31,2,offsets);
    check(1,1,11,21,31,2,offsets);
    offsets[1]=256;compute_command_apply(11,21,31,2,offsets);
    check(1,2,11,21,31,2,offsets);
    compute_command_apply(12,21,31,2,offsets);check(2,2,12,21,31,2,offsets);
    compute_command_apply(12,22,31,2,offsets);check(2,3,12,22,31,2,offsets);
    compute_command_apply(12,22,32,2,offsets);check(2,4,12,22,32,2,offsets);
    compute_command_apply(12,22,32,1,offsets);check(2,5,12,22,32,1,offsets);
    compute_command_apply(91,92,93,0,NULL);check(3,6,91,92,93,0,NULL);
    compute_command_apply(91,92,94,0,NULL);check(3,7,91,92,94,0,NULL);
    compute_command_apply(12,22,32,1,offsets);check(4,8,12,22,32,1,offsets);
    compute_command_apply(12,22,32,256,offsets);check(4,9,12,22,32,256,offsets);
    offsets[255]=512;compute_command_apply(12,22,32,256,offsets);check(4,10,12,22,32,256,offsets);
    command=2;compute_command_reset();compute_command_apply(12,22,32,256,offsets);
    check(5,11,12,22,32,256,offsets);
    puts("compute_state_test PASS: repeated binds, offsets, layout/set changes, counter interleave, command reset");
}

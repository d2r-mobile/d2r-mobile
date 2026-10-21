#include "nvn_vulkan_batch_policy.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    dm_vk_batch_policy policy;
    assert(dm_vk_batch_policy_parse(NULL,NULL,&policy));
    assert(policy.slots==3&&policy.draw_limit==256);
    const char *slots[]={"3","6","8"},*draws[]={"256","512","1024"};
    const unsigned slot_values[]={3,6,8},draw_values[]={256,512,1024};
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++){
        assert(dm_vk_batch_policy_parse(slots[i],draws[j],&policy));
        assert(policy.slots==slot_values[i]);
        assert(policy.draw_limit==draw_values[j]);
        assert(!dm_vk_batch_should_flush(policy,policy.draw_limit-1,(32u<<20)-1));
        assert(dm_vk_batch_should_flush(policy,policy.draw_limit,0));
        assert(dm_vk_batch_should_flush(policy,0,32u<<20));
    }
    const char *bad[]={"0","1","9","-1"," 6","6x","99999999999999999999"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){
        assert(!dm_vk_batch_policy_parse(bad[i],"512",&policy));
        assert(policy.slots==3&&policy.draw_limit==256);
        assert(!dm_vk_batch_policy_parse("8",bad[i],&policy));
        assert(policy.slots==3&&policy.draw_limit==256);
    }
    puts("PASS: bounded batch slots/draw limits, invalid-setting fallback and unchanged temporary-memory cap");
}

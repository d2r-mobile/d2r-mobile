#pragma once
#include <stdint.h>
#include <string.h>

#ifndef DM_VK_BATCH_SLOTS
#define DM_VK_BATCH_SLOTS 8
#endif
#if DM_VK_BATCH_SLOTS < 3 || DM_VK_BATCH_SLOTS > 8
#error DM_VK_BATCH_SLOTS must be between 3 and 8
#endif

typedef struct { unsigned slots,draw_limit; } dm_vk_batch_policy;
static inline int dm_vk_batch_policy_parse(const char *slots,const char *draws,dm_vk_batch_policy *out) {
    *out=(dm_vk_batch_policy){3,256};
    dm_vk_batch_policy value=*out;
    if(slots&&*slots){
        if(!strcmp(slots,"3"))value.slots=3;
        else if(!strcmp(slots,"6"))value.slots=6;
        else if(!strcmp(slots,"8"))value.slots=8;
        else return 0;
    }
    if(draws&&*draws){
        if(!strcmp(draws,"256"))value.draw_limit=256;
        else if(!strcmp(draws,"512"))value.draw_limit=512;
        else if(!strcmp(draws,"1024"))value.draw_limit=1024;
        else return 0;
    }
    if(value.slots>DM_VK_BATCH_SLOTS)return 0;
    *out=value;return 1;
}
static inline int dm_vk_batch_should_flush(dm_vk_batch_policy policy,unsigned draws,uint64_t bytes) {
    return draws>=policy.draw_limit||bytes>=(32u<<20);
}

// 有界 SPIR-V 模块读取；依据 Vulkan SPIR-V 头与原计算测试的 1 MiB 上限，不承担着色器完整验证。
// Bounded SPIR-V module loading; based on the Vulkan SPIR-V header and existing 1 MiB limit, not full shader validation.
#include "vk_host.h"
#include <stdio.h>
#include <stdlib.h>
VkResult dm_vk_shader_load(dm_vk*vk,const char*path,VkShaderModule*out){
    if(!vk||!vk->device||!path||!out)return VK_ERROR_INITIALIZATION_FAILED;
    FILE*f=fopen(path,"rb");if(!f)return VK_ERROR_INITIALIZATION_FAILED;
    long length=-1;if(!fseek(f,0,SEEK_END))length=ftell(f);
    if(length<20||length>1024*1024||length%4||fseek(f,0,SEEK_SET)){fclose(f);return VK_ERROR_INITIALIZATION_FAILED;}
    uint32_t*code=malloc((size_t)length);if(!code){fclose(f);return VK_ERROR_OUT_OF_HOST_MEMORY;}
    int valid=fread(code,1,(size_t)length,f)==(size_t)length&&code[0]==0x07230203;
    if(fclose(f))valid=0;VkResult result=VK_ERROR_INITIALIZATION_FAILED;
    if(valid){VkShaderModuleCreateInfo info={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)length,.pCode=code};
        result=vkCreateShaderModule(vk->device,&info,NULL,out);}
    free(code);return result;
}

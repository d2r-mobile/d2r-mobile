// 可选Turnip加载和统一Vulkan分发。默认系统驱动；不更改其他应用的配置。
// Optional Turnip loading and unified Vulkan dispatch. System by default; never change another app's configuration.
#define _GNU_SOURCE
#include "gpu_driver.h"
#include <adrenotools/driver.h>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/api-level.h>
#include <android/log.h>
#endif

typedef void *(*adreno_open_fn)(int,int,const char *,const char *,const char *,const char *,const char *,void **);
static void driver_log(const char *format,...) {
    char text[1536];va_list args;va_start(args,format);vsnprintf(text,sizeof text,format,args);va_end(args);
    fprintf(stderr,"[vulkan-loader] %s\n",text);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO,"D2RAndroid","[vulkan-loader] %s",text);
#endif
}
static int driver_error(dm_vk_driver *d,const char *format,...) {
    va_list args;va_start(args,format);vsnprintf(d->error,sizeof d->error,format,args);va_end(args);return 0;
}
const char *dm_vk_driver_error(const dm_vk_driver *d) { return d->error[0]?d->error:"Vulkan loader initialization failed"; }
static int copy_text(dm_vk_driver *d,char *destination,size_t size,const char *source) {
    if(strlen(source)>=size)return driver_error(d,"driver configuration value is too long");
    memcpy(destination,source,strlen(source)+1);return 1;
}
static char *trim(char *text) {
    while(isspace((unsigned char)*text))text++;char *end=text+strlen(text);
    while(end>text&&isspace((unsigned char)end[-1]))*--end=0;return text;
}
static int parse_subgroup_size(dm_vk_driver *d,const char *text) {
    d->subgroup_policy_required=1;
    errno=0;char *end;unsigned long size=strtoul(text,&end,10);
    if(errno||end==text||*end||size<32||size>128||(size&(size-1)))
        return driver_error(d,"subgroup_size must be a power of two from32 through128");
    d->requested_subgroup_size=(uint32_t)size;return 1;
}
static void reset_subgroup_policy(dm_vk_driver *d) {
    free(d->policy_device_extensions);d->policy_device_extensions=NULL;
    d->effective_shader_subgroup_size=0;d->required_subgroup_stages=0;
    d->subgroup_policy_supported=0;d->subgroup_policy_prepared=0;d->subgroup_policy_enabled=0;
    memset(&d->subgroup_features,0,sizeof d->subgroup_features);
}
static int configure(dm_vk_driver *d) {
    // 严格环境标志先解析，配置语法错误也不能悄悄退回系统。
    // Parse strict environment policy first so malformed configuration cannot silently fall back.
    const char *required=getenv("DM_VK_DRIVER_REQUIRE");
    if(required){d->require_custom=strcmp(required,"0")!=0;if(strcmp(required,"0")&&strcmp(required,"1"))return driver_error(d,"DM_VK_DRIVER_REQUIRE must be 0 or 1");}
    const char *subgroup=getenv("DM_VK_SUBGROUP_SIZE");
    if(subgroup&&!parse_subgroup_size(d,subgroup))return 0;
    char path[1200];const char *config=getenv("DM_VK_DRIVER_CONFIG"),*temporary=getenv("DM_TEMP");
    driver_log("driver configuration lookup: DM_VK_DRIVER_CONFIG=%s DM_TEMP=%s",config?config:"(unset)",temporary?temporary:"(unset)");
    if(!config&&temporary){if(snprintf(path,sizeof path,"%s/gpu-driver.conf",temporary)>=(int)sizeof path)return driver_error(d,"driver configuration path is too long");config=path;}
    if(config){FILE *file=fopen(config,"r");if(!file){if(errno!=ENOENT)return driver_error(d,"cannot open driver configuration: %s",strerror(errno));
            // 配置缺失会让严格请求静默退回系统驱动；必须记下实际尝试的路径与来源。
            // A missing configuration would silently drop a strict request back to the system driver, so record the
            // path actually tried and where it came from.
            else driver_log("driver configuration not found: %s (DM_VK_DRIVER_CONFIG=%s DM_TEMP=%s)",config,getenv("DM_VK_DRIVER_CONFIG")?getenv("DM_VK_DRIVER_CONFIG"):"(unset)",temporary?temporary:"(unset)");}
        else{d->requested_custom=1;char line[2048],first_error[512]={0};int ok=1,driver_keys=0,subgroup_keys=0;
            // 即使前面的行有错，也读完整policy；后置require_custom=1不能被忽略。
            // Read the complete policy even after an earlier error; do not ignore a later require_custom=1.
            while(fgets(line,sizeof line,file)){
                int line_ok=1;
                if(!strchr(line,'\n')&&!feof(file)){line_ok=driver_error(d,"driver configuration line is too long");int c;while((c=fgetc(file))!=EOF&&c!='\n'){} }
                else{
                    char *key=trim(line);if(!*key||*key=='#')continue;char *value=strchr(key,'=');
                    if(!value)line_ok=driver_error(d,"driver configuration requires key=value");
                    else{*value++=0;key=trim(key);value=trim(value);
                        if(!strcmp(key,"directory")){driver_keys=1;line_ok=copy_text(d,d->driver_directory,sizeof d->driver_directory,value);}
                        else if(!strcmp(key,"library")){driver_keys=1;line_ok=copy_text(d,d->driver_name,sizeof d->driver_name,value);}
                        else if(!strcmp(key,"require_custom")){driver_keys=1;if(strcmp(value,"0")&&strcmp(value,"1"))line_ok=driver_error(d,"require_custom must be 0 or 1");else if(!required)d->require_custom=*value=='1';}
                        else if(!strcmp(key,"subgroup_size")){
                            d->subgroup_policy_required=1;
                            if(subgroup_keys++)line_ok=driver_error(d,"duplicate subgroup_size configuration");
                            else{uint32_t environment_size=d->requested_subgroup_size;line_ok=parse_subgroup_size(d,value);if(subgroup)d->requested_subgroup_size=environment_size;}
                        }
                        else line_ok=driver_error(d,"unknown driver configuration key: %s",key);
                    }
                }
                if(!line_ok){if(ok)snprintf(first_error,sizeof first_error,"%s",d->error);ok=0;}
            }
            if(ferror(file)){if(ok)snprintf(first_error,sizeof first_error,"driver configuration read failed");ok=0;}fclose(file);
            if(!ok){snprintf(d->error,sizeof d->error,"%s",first_error);return 0;}
            if(subgroup_keys&&!driver_keys)d->requested_custom=0;
        }
    }
    const char *directory=getenv("DM_VK_DRIVER_DIR"),*name=getenv("DM_VK_DRIVER_NAME");
    if(directory){d->requested_custom=*directory!=0;if(!copy_text(d,d->driver_directory,sizeof d->driver_directory,directory))return 0;}
    if(name&&!copy_text(d,d->driver_name,sizeof d->driver_name,name))return 0;
    if(!d->requested_custom){if(d->require_custom)return driver_error(d,"required custom driver directory is missing");return 1;}
    if(!d->driver_directory[0])return driver_error(d,"custom driver directory is missing");
    if(!d->driver_name[0])strcpy(d->driver_name,"vulkan.purple.so");
    if(strchr(d->driver_name,'/')||strchr(d->driver_name,'\\')||!strcmp(d->driver_name,".")||!strcmp(d->driver_name,".."))return driver_error(d,"custom driver library must be a basename");
    char resolved[PATH_MAX];if(!realpath(d->driver_directory,resolved))return driver_error(d,"custom driver directory: %s",strerror(errno));
    if(!strncmp(resolved,"/sdcard",7)||!strncmp(resolved,"/storage",8)||!strncmp(resolved,"/mnt",4))return driver_error(d,"custom driver must be in internal executable storage");
    if(snprintf(d->driver_directory,sizeof d->driver_directory,"%s/",resolved)>=(int)sizeof d->driver_directory)return driver_error(d,"custom driver directory is too long");
    const char *hooks=getenv("DM_ANDROID_LIBDIR");if(!hooks||!*hooks)return driver_error(d,"DM_ANDROID_LIBDIR is required for custom driver hooks");
    if(!realpath(hooks,resolved))return driver_error(d,"hook library directory: %s",strerror(errno));
    if(snprintf(d->hook_directory,sizeof d->hook_directory,"%s/",resolved)>=(int)sizeof d->hook_directory)return driver_error(d,"hook directory is too long");
    return 1;
}
static int load_globals(dm_vk_driver *d) {
    dlerror();d->vkGetInstanceProcAddr=(PFN_vkGetInstanceProcAddr)dlsym(d->library,"vkGetInstanceProcAddr");
    if(!d->vkGetInstanceProcAddr)return driver_error(d,"selected loader has no vkGetInstanceProcAddr");
#define DM_VK_LOAD(name) do{d->name=(PFN_##name)d->vkGetInstanceProcAddr(VK_NULL_HANDLE,#name);if(!d->name)return driver_error(d,"selected loader has no %s",#name);}while(0);
    DM_VK_GLOBAL_FUNCTIONS(DM_VK_LOAD)
#undef DM_VK_LOAD
    return 1;
}
static int open_system(dm_vk_driver *d) {
    reset_subgroup_policy(d);
    d->using_custom=0;
    d->vkGetInstanceProcAddr=NULL;d->actual_driver_id=0;d->subgroup_size=0;
#define DM_VK_CLEAR(name) d->name=NULL;
    DM_VK_GLOBAL_FUNCTIONS(DM_VK_CLEAR)
    DM_VK_INSTANCE_FUNCTIONS(DM_VK_CLEAR)
    DM_VK_ANDROID_INSTANCE_FUNCTIONS(DM_VK_CLEAR)
    DM_VK_DEVICE_FUNCTIONS(DM_VK_CLEAR)
#undef DM_VK_CLEAR
#ifdef __ANDROID__
    // 只用所选handle的GIPA，不调用链接到全局系统loader的Vk入口。
    // Use only the selected handle's GIPA, never a globally linked system Vulkan entry point.
    adreno_open_fn open=d->adrenotools?(adreno_open_fn)dlsym(d->adrenotools,"adrenotools_open_libvulkan"):NULL;
    if(open)d->library=open(RTLD_NOW,0,NULL,d->hook_directory,NULL,NULL,NULL,NULL);
    if(!d->library)d->library=dlopen("libvulkan.so",RTLD_NOW|RTLD_LOCAL);
#else
    d->library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);
#endif
    if(!d->library){const char *error=dlerror();return driver_error(d,"system Vulkan loader: %s",error?error:"open failed");}
    if(!load_globals(d))return 0;
    driver_log("requested=%s selected=system",d->requested_custom?"Turnip":"system");return 1;
}
int dm_vk_driver_open(dm_vk_driver *d) {
    memset(d,0,sizeof *d);
    if(!configure(d)){
        driver_log("custom initialization failed: %s",dm_vk_driver_error(d));
        if(d->require_custom||d->subgroup_policy_required)return 0;return open_system(d);
    }
    if(!d->requested_custom)return open_system(d);
#ifdef __ANDROID__
    if(android_get_device_api_level()<30||access("/dev/kgsl-3d0",F_OK))driver_error(d,"Turnip T30 requires API30+ and KGSL");
    else{
        char path[1200];snprintf(path,sizeof path,"%slibadrenotools.so",d->hook_directory);d->adrenotools=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        if(!d->adrenotools){const char *error=dlerror();driver_error(d,"adrenotools: %s",error?error:"open failed");}
        else{
            adreno_open_fn open=(adreno_open_fn)dlsym(d->adrenotools,"adrenotools_open_libvulkan");
            if(!open)driver_error(d,"adrenotools_open_libvulkan is missing");
            else{d->library=open(RTLD_NOW,ADRENOTOOLS_DRIVER_CUSTOM,NULL,d->hook_directory,d->driver_directory,d->driver_name,NULL,NULL);
                if(!d->library)driver_error(d,"adrenotools custom loader open failed");
                else if(load_globals(d)){d->using_custom=1;driver_log("requested=Turnip library=%s%s; identity pending",d->driver_directory,d->driver_name);return 1;}
            }
        }
    }
#else
    driver_error(d,"custom Adreno loading is Android-only");
#endif
    char reason[sizeof d->error];snprintf(reason,sizeof reason,"%s",dm_vk_driver_error(d));driver_log("custom initialization failed: %s",reason);
    if(d->library){dlclose(d->library);d->library=NULL;}
    if(d->require_custom||d->subgroup_policy_required)return 0;
    return open_system(d);
}
static int is_wsi_function(const char *name) {
    return strstr(name,"Surface")!=NULL||strstr(name,"Swapchain")!=NULL||!strcmp(name,"vkAcquireNextImageKHR")||!strcmp(name,"vkQueuePresentKHR");
}
int dm_vk_driver_load_instance(dm_vk_driver *d,VkInstance instance,int needs_surface) {
    d->needs_surface=needs_surface;
    // destroy先取，后续缺函数时仍可由调用者安全释放新instance。
    // Resolve destroy first so the caller can safely release the new instance if another function is missing.
    d->vkDestroyInstance=(PFN_vkDestroyInstance)d->vkGetInstanceProcAddr(instance,"vkDestroyInstance");
    if(!d->vkDestroyInstance)return driver_error(d,"selected instance has no vkDestroyInstance");
#define DM_VK_LOAD(name) do{d->name=(PFN_##name)d->vkGetInstanceProcAddr(instance,#name);if(!d->name&&(d->needs_surface||!is_wsi_function(#name)))return driver_error(d,"selected instance has no %s",#name);}while(0);
    DM_VK_INSTANCE_FUNCTIONS(DM_VK_LOAD)
    DM_VK_ANDROID_INSTANCE_FUNCTIONS(DM_VK_LOAD)
#undef DM_VK_LOAD
    return 1;
}
int dm_vk_driver_load_device(dm_vk_driver *d,VkDevice device) {
    d->vkDeviceWaitIdle=NULL;
    d->vkDestroyDevice=(PFN_vkDestroyDevice)d->vkGetDeviceProcAddr(device,"vkDestroyDevice");
    if(!d->vkDestroyDevice)return driver_error(d,"selected device has no vkDestroyDevice");
    d->vkDeviceWaitIdle=(PFN_vkDeviceWaitIdle)d->vkGetDeviceProcAddr(device,"vkDeviceWaitIdle");
    if(!d->vkDeviceWaitIdle)return driver_error(d,"selected device has no vkDeviceWaitIdle");
#define DM_VK_LOAD(name) do{d->name=(PFN_##name)d->vkGetDeviceProcAddr(device,#name);if(!d->name&&(d->needs_surface||!is_wsi_function(#name)))return driver_error(d,"selected device has no %s",#name);}while(0);
    DM_VK_DEVICE_FUNCTIONS(DM_VK_LOAD)
#undef DM_VK_LOAD
    if(d->requested_subgroup_size){
        if(!d->subgroup_policy_prepared)return driver_error(d,"explicit subgroup device policy was not applied");
        d->subgroup_policy_enabled=1;
        driver_log("device explicitly enabled subgroupSizeControl; physical_default=%u shader_selected=%u",d->subgroup_size,d->effective_shader_subgroup_size);
    }
    return 1;
}
static int has_device_extension(dm_vk_driver *d,VkPhysicalDevice physical,const char *name) {
    uint32_t count=0;
    if(d->vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL)!=VK_SUCCESS||count>4096)return 0;
    VkExtensionProperties *extensions=count?calloc(count,sizeof *extensions):NULL;if(count&&!extensions)return 0;
    int found=0;
    if(d->vkEnumerateDeviceExtensionProperties(physical,NULL,&count,extensions)==VK_SUCCESS)
        for(uint32_t i=0;i<count;i++)if(!strcmp(extensions[i].extensionName,name)){found=1;break;}
    free(extensions);return found;
}
static int validate_subgroup_policy(dm_vk_driver *d,VkPhysicalDevice physical) {
    d->effective_shader_subgroup_size=d->subgroup_size;
    if(!d->requested_subgroup_size)return 1;
    // 调用者仍用API1.1，因而明确启用EXT路径，不偷用未启用的1.3核心功能。
    // Callers still use API1.1: explicitly enable the EXT path rather than unenabled1.3 core functionality.
    if(!has_device_extension(d,physical,VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME))
        return driver_error(d,"explicit subgroup policy requires VK_EXT_subgroup_size_control for API1.1");
    VkPhysicalDeviceSubgroupSizeControlProperties sizes={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
    VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&sizes};
    d->vkGetPhysicalDeviceProperties2(physical,&properties);
    VkPhysicalDeviceSubgroupSizeControlFeatures features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
    VkPhysicalDeviceFeatures2 all={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&features};
    d->vkGetPhysicalDeviceFeatures2(physical,&all);
    const VkShaderStageFlags required=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT;
    driver_log("subgroup policy requested=%u physical_default=%u support=%u range=%u..%u required_stages=%#x",
        d->requested_subgroup_size,d->subgroup_size,features.subgroupSizeControl,sizes.minSubgroupSize,sizes.maxSubgroupSize,sizes.requiredSubgroupSizeStages);
    if(!features.subgroupSizeControl||d->requested_subgroup_size<sizes.minSubgroupSize||d->requested_subgroup_size>sizes.maxSubgroupSize||
       (sizes.requiredSubgroupSizeStages&required)!=required)
        return driver_error(d,"actual GPU cannot explicitly select subgroup%u for every supported NVN shader stage",d->requested_subgroup_size);
    d->required_subgroup_stages=sizes.requiredSubgroupSizeStages;d->subgroup_policy_supported=1;
    d->effective_shader_subgroup_size=d->requested_subgroup_size;return 1;
}
int dm_vk_driver_prepare_device(dm_vk_driver *d,VkDeviceCreateInfo *create) {
    if(!d->requested_subgroup_size)return 1;
    if(!d->subgroup_policy_supported)return driver_error(d,"explicit subgroup policy was not validated");
    if(d->subgroup_policy_prepared)return driver_error(d,"explicit subgroup device policy applied more than once");
    if(create->enabledExtensionCount>4096||(create->enabledExtensionCount&&!create->ppEnabledExtensionNames))
        return driver_error(d,"invalid existing device extension list");
    for(const VkBaseInStructure *item=create->pNext;item;item=item->pNext)
        if(item->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES)
            return driver_error(d,"existing device chain already contains subgroup-size control");
    uint32_t count=create->enabledExtensionCount;int present=0;
    for(uint32_t i=0;i<count;i++)if(!strcmp(create->ppEnabledExtensionNames[i],VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME))present=1;
    d->policy_device_extensions=calloc((size_t)count+1,sizeof *d->policy_device_extensions);
    if(!d->policy_device_extensions)return driver_error(d,"subgroup device extension allocation failed");
    if(count)memcpy(d->policy_device_extensions,create->ppEnabledExtensionNames,count*sizeof *d->policy_device_extensions);
    if(!present)d->policy_device_extensions[count++]=VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME;
    d->subgroup_features=(VkPhysicalDeviceSubgroupSizeControlFeatures){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES,
        .pNext=(void*)create->pNext,.subgroupSizeControl=VK_TRUE,.computeFullSubgroups=VK_FALSE};
    create->enabledExtensionCount=count;create->ppEnabledExtensionNames=d->policy_device_extensions;create->pNext=&d->subgroup_features;
    d->subgroup_policy_prepared=1;
    driver_log("device policy adds EXT subgroup control; requested=%u computeFullSubgroups=0 existing chains preserved",d->requested_subgroup_size);
    return 1;
}
int dm_vk_driver_prepare_shader_stage(dm_vk_driver *d,VkPipelineShaderStageCreateInfo *stage,VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *required) {
    if(!d->requested_subgroup_size)return 1;
    if(!d->subgroup_policy_enabled||!d->subgroup_policy_prepared||d->effective_shader_subgroup_size!=d->requested_subgroup_size)
        return driver_error(d,"explicit subgroup policy was not enabled on this device");
    if(!(d->required_subgroup_stages&stage->stage))return driver_error(d,"explicit subgroup policy is unsupported by stage%#x",stage->stage);
    if(stage->flags&(VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT|VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT))
        return driver_error(d,"production subgroup policy requires a fixed size and allows partial guest workgroups");
    for(const VkBaseInStructure *item=stage->pNext;item;item=item->pNext)
        if(item->sType==VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO)
            return driver_error(d,"existing stage chain already contains a required subgroup size");
    *required=(VkPipelineShaderStageRequiredSubgroupSizeCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
        .pNext=(void*)stage->pNext,.requiredSubgroupSize=d->requested_subgroup_size};
    stage->pNext=required;return 1;
}
static int has_driver_properties(dm_vk_driver *d,VkPhysicalDevice physical,uint32_t api) {
    if(api>=VK_API_VERSION_1_2)return 1;uint32_t count=0;
    if(d->vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL)!=VK_SUCCESS||count>4096)return 0;
    VkExtensionProperties *extensions=count?calloc(count,sizeof *extensions):NULL;if(count&&!extensions)return 0;
    int supported=0;if(d->vkEnumerateDeviceExtensionProperties(physical,NULL,&count,extensions)==VK_SUCCESS)
        for(uint32_t i=0;i<count;i++)if(!strcmp(extensions[i].extensionName,VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))supported=1;
    free(extensions);return supported;
}
static int profile_matches(dm_vk_driver *d,const char *directory) {
    char path[1200];if(snprintf(path,sizeof path,"%s/host-subgroup-size.txt",directory)>=(int)sizeof path)return driver_error(d,"shader profile path is too long");
    FILE *file=fopen(path,"r");if(!file)return driver_error(d,"custom game requires shader subgroup profile: %s",strerror(errno));
    char text[32];size_t n=fread(text,1,sizeof text-1,file);int extra=fgetc(file),error=ferror(file);fclose(file);text[n]=0;
    errno=0;char *end;unsigned long size=strtoul(text,&end,10);while(isspace((unsigned char)*end))end++;
    if(error||extra!=EOF||errno||end==text||*end||(size!=32&&size!=64&&size!=128))return driver_error(d,"invalid shader subgroup profile");
    if(size!=d->effective_shader_subgroup_size)return driver_error(d,"shader host subgroup %lu differs from selected shader%u (physical default%u); rebuild the cache",size,d->effective_shader_subgroup_size,d->subgroup_size);
    driver_log("shader host subgroup=%lu matches selected shader size; physical_default=%u explicit_request=%u",size,d->subgroup_size,d->requested_subgroup_size);return 1;
}
int dm_vk_driver_validate(dm_vk_driver *d,VkPhysicalDevice physical,const char *shader_directory) {
    VkPhysicalDeviceProperties base;d->vkGetPhysicalDeviceProperties(physical,&base);
    int supported=has_driver_properties(d,physical,base.apiVersion);
    VkPhysicalDeviceDriverProperties identity={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceSubgroupProperties subgroup={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,.pNext=supported?&identity:NULL};
    VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&subgroup};
    d->vkGetPhysicalDeviceProperties2(physical,&properties);d->actual_driver_id=supported?identity.driverID:0;d->subgroup_size=subgroup.subgroupSize;
    driver_log("actual device=%s driver_id=%u name=%s info=%s API=%u.%u subgroup=%u stages=%#x operations=%#x quad_all=%u",
        base.deviceName,d->actual_driver_id,supported?identity.driverName:"unknown",supported?identity.driverInfo:"unknown",VK_VERSION_MAJOR(base.apiVersion),VK_VERSION_MINOR(base.apiVersion),subgroup.subgroupSize,subgroup.supportedStages,subgroup.supportedOperations,subgroup.quadOperationsInAllStages);
    if(!validate_subgroup_policy(d,physical))return 0;
    if(!d->using_custom){if(d->requested_subgroup_size&&shader_directory)return profile_matches(d,shader_directory);return 1;}
    if(base.apiVersion<VK_API_VERSION_1_1)return driver_error(d,"custom driver requires Vulkan1.1+");
    if(!supported||identity.driverID!=VK_DRIVER_ID_MESA_TURNIP)return driver_error(d,"requested Turnip was not loaded; actual driver_id=%u",d->actual_driver_id);
    if(shader_directory&&!profile_matches(d,shader_directory))return 0;
    driver_log("requested=Turnip selected=Turnip identity verified");return 1;
}
int dm_vk_driver_fallback(dm_vk_driver *d,const char *reason) {
    if(!d->using_custom)return 0;
    char message[512];snprintf(message,sizeof message,"%s",reason?reason:dm_vk_driver_error(d));driver_log("custom initialization failed: %s",message);
    if(d->require_custom||d->subgroup_policy_required){driver_error(d,"required driver/subgroup policy failed: %s",message);return 0;}
    if(d->library){dlclose(d->library);d->library=NULL;}return open_system(d);
}
void dm_vk_driver_close(dm_vk_driver *d) {
    reset_subgroup_policy(d);if(d->library)dlclose(d->library);if(d->adrenotools)dlclose(d->adrenotools);memset(d,0,sizeof *d);
}

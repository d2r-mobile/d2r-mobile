// Separate process: a bad vendor library must not take down the launcher.
#include "gpu_driver.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    setenv("DM_VK_DRIVER_CONFIG", argv[1], 1);
    setenv("DM_ANDROID_LIBDIR", argv[2], 1);
    dm_vk_driver d = {0}; VkInstance instance = VK_NULL_HANDLE;
    int ok = dm_vk_driver_open(&d);
    if (ok) {
        VkApplicationInfo app = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName="D2R driver compatibility", .apiVersion=VK_API_VERSION_1_1};
        VkInstanceCreateInfo info = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
        VkResult r = d.vkCreateInstance(&info,NULL,&instance);
        if (r != VK_SUCCESS) { snprintf(d.error,sizeof d.error,"vkCreateInstance failed (%d)",r); ok=0; }
    }
    if (ok) ok=dm_vk_driver_load_instance(&d,instance,0);
    if (ok) {
        uint32_t n=1; VkPhysicalDevice physical=VK_NULL_HANDLE;
        VkResult r=d.vkEnumeratePhysicalDevices(instance,&n,&physical);
        if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || !n) {
            snprintf(d.error,sizeof d.error,"No Vulkan physical device"); ok=0;
        } else ok=dm_vk_driver_validate(&d,physical,NULL);
    }
    if (!ok) fprintf(stderr,"%s\n",dm_vk_driver_error(&d));
    if (instance && d.vkDestroyInstance) d.vkDestroyInstance(instance,NULL);
    dm_vk_driver_close(&d);
    return ok?0:1;
}

// #13 多级上传中途失败注入：vkCreateImageView 在图像/内存/暂存缓冲已创建后失败，必须返回错误、out 字节不变，
// 且本地创建的图像与内存各回收一次（不多不少）。
// #13 mid-pipeline upload failure injection: vkCreateImageView fails after the image, memory and staging buffer exist;
// the call must fail, leave out byte-identical, and release the locally created image and memory exactly once.
#include "../vulkan/vk_host.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static int creates, allocs, view_attempts, image_destroys, memory_frees, view_destroys;

static PFN_vkCreateImage real_create_image;
static PFN_vkAllocateMemory real_allocate_memory;
static PFN_vkDestroyImage real_destroy_image;
static PFN_vkDestroyImageView real_destroy_image_view;
static PFN_vkFreeMemory real_free_memory;

static void resolve(void) {
    real_create_image = (PFN_vkCreateImage)dlsym(RTLD_DEFAULT, "vkCreateImage");
    real_allocate_memory = (PFN_vkAllocateMemory)dlsym(RTLD_DEFAULT, "vkAllocateMemory");
    real_destroy_image = (PFN_vkDestroyImage)dlsym(RTLD_DEFAULT, "vkDestroyImage");
    real_destroy_image_view = (PFN_vkDestroyImageView)dlsym(RTLD_DEFAULT, "vkDestroyImageView");
    real_free_memory = (PFN_vkFreeMemory)dlsym(RTLD_DEFAULT, "vkFreeMemory");
    if (!real_create_image || !real_allocate_memory || !real_destroy_image || !real_destroy_image_view ||
        !real_free_memory) { fprintf(stderr, "missing loader symbols\n"); exit(2); }
}

VkResult dm_test_vkCreateImage(VkDevice device, const VkImageCreateInfo *info,
                               const VkAllocationCallbacks *allocator, VkImage *image) {
    creates++;
    return real_create_image(device, info, allocator, image);
}

VkResult dm_test_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info,
                                  const VkAllocationCallbacks *allocator, VkDeviceMemory *memory) {
    allocs++;
    return real_allocate_memory(device, info, allocator, memory);
}

VkResult dm_test_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *info,
                                   const VkAllocationCallbacks *allocator, VkImageView *view) {
    (void)device; (void)info; (void)allocator;
    view_attempts++;
    *view = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;   // 真实已建资源之后失败 | fail after real resources exist
}

void dm_test_vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *allocator) {
    image_destroys++;
    real_destroy_image(device, image, allocator);
}

void dm_test_vkDestroyImageView(VkDevice device, VkImageView view, const VkAllocationCallbacks *allocator) {
    view_destroys++;
    real_destroy_image_view(device, view, allocator);
}

void dm_test_vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *allocator) {
    memory_frees++;
    real_free_memory(device, memory, allocator);
}

int main(void) {
    resolve();
    dm_vk vk;
    VkResult r = dm_vk_create(&vk);
    if (r != VK_SUCCESS) { fprintf(stderr, "no Vulkan device\n"); return 1; }

    uint8_t level0[4 * 4 * 4], level1[2 * 2 * 4], level2[4];
    memset(level0, 0x11, sizeof level0);
    memset(level1, 0x22, sizeof level1);
    memset(level2, 0x33, sizeof level2);
    dm_vk_mip_level levels[3] = {{level0, sizeof level0, 0}, {level1, sizeof level1, 0}, {level2, sizeof level2, 0}};

    dm_vk_texture sentinel;
    memset(&sentinel, 0x5a, sizeof sentinel);
    unsigned char before[sizeof sentinel];
    memcpy(before, &sentinel, sizeof before);

    VkResult upload = dm_vk_texture_upload_mips(&vk, 4, 4, 3, levels, &sentinel);
    CHECK(upload != VK_SUCCESS);
    CHECK(view_attempts == 1);
    CHECK(creates == 1);
    CHECK(allocs == 1);
    CHECK(image_destroys == 1);
    CHECK(memory_frees == 1);
    CHECK(view_destroys == 0);                       // 视图从未成功创建 | the view was never created
    CHECK(!memcmp(before, &sentinel, sizeof before)); // out 字节不变 | out byte-identical

    dm_vk_destroy(&vk);
    printf("vulkan_texture_mips_createfail: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

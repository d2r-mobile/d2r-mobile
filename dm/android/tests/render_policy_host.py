from pathlib import Path
import re
import subprocess
import sys
import tempfile


def function(source, name):
    match = re.search(r"^static [^\n]+\b" + name + r"\([^\n]*\) \{", source, re.M)
    if match is None:
        raise RuntimeError("missing production function: " + name)
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


PREFIX = r'''
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define die(...) abort()
#define LOG(...) ((void)0)
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define NVN_TEXTURE_TARGET_BUFFER 9
#define NVN_TEXTURE_TARGET_3D 3
#define NVN_TEXTURE_TARGET_1D 1
#define NVN_TEXTURE_TARGET_1D_ARRAY 11
#define NVN_TEXTURE_TARGET_CUBEMAP 5
#define NVN_TEXTURE_TARGET_CUBEMAP_ARRAY 15
#define NVN_TEXTURE_FLAGS_IMAGE 4
typedef struct {int target,samples,format;unsigned width,height,depth,levels,flags;} desc;
typedef struct {desc d;struct {unsigned layers;} L;uint64_t serial;} tex_t;
typedef struct cached_image_view {VkImageViewType type;VkImageSubresourceRange range;VkImageView view;struct cached_image_view *next;} cached_image_view;
typedef struct image_entry {
 uint64_t serial;tex_t texture;VkImage image;VkImageView view;VkDeviceMemory memory;VkFormat format;VkImageType image_type;VkImageAspectFlags aspect;
 int populated;VkPipelineStageFlags last_stage;VkAccessFlags last_access;cached_image_view *views;VkImageUsageFlags usage;
 VkImage retired_image;VkImageView retired_view;VkDeviceMemory retired_memory;cached_image_view *retired_views;
 struct image_entry *next,*hash_next;
} image_entry;
static image_entry *image_list,*image_index[4096];
static VkDevice device;static VkPhysicalDevice physical;static VkCommandBuffer command;
static VkPhysicalDeviceMemoryProperties memory_properties;
static int narrow_image_candidate=1,attachment_clear_candidate=1;
static unsigned creates,copies,view_destroys,image_destroys,memory_frees,pass_begins,pass_ends;
static uint64_t render_passes;
static uint64_t attachment_clear_count,narrow_image_count,storage_promotion_count;
static VkImageUsageFlags usage_seen;static VkImageCopy copies_seen[16];
static VkAttachmentDescription attachment_seen;static VkClearValue clear_seen;
static VkImageSubresourceRange view_range;
static VkFormat vk_format(int x){return (VkFormat)x;}
static void texture_diag_unsupported(unsigned x){(void)x;abort();}
static void graphics_pass_end(void){}
static void image_access(image_entry *e,VkPipelineStageFlags s,VkAccessFlags a){e->last_stage=s;e->last_access=a;}
static void image_gpu_written(image_entry *e){e->populated=1;}
static void graphics_cache_forget_image(image_entry *e){(void)e;}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice p,VkFormat f,VkFormatProperties *o){(void)p;(void)f;memset(o,0,sizeof *o);o->optimalTilingFeatures=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice d,const VkImageCreateInfo *c,const VkAllocationCallbacks *a,VkImage *o){(void)d;(void)a;usage_seen=c->usage;*o=(VkImage)(uintptr_t)++creates;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice d,VkImage i,VkMemoryRequirements *r){(void)d;(void)i;*r=(VkMemoryRequirements){.size=4096,.memoryTypeBits=1};}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d,const VkMemoryAllocateInfo *i,const VkAllocationCallbacks *a,VkDeviceMemory *o){(void)d;(void)i;(void)a;*o=(VkDeviceMemory)(uintptr_t)creates;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice d,VkImage i,VkDeviceMemory m,VkDeviceSize s){(void)d;(void)i;(void)m;(void)s;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice d,const VkImageViewCreateInfo *i,const VkAllocationCallbacks *a,VkImageView *o){(void)d;(void)i;(void)a;*o=(VkImageView)(uintptr_t)creates;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c,VkPipelineStageFlags s,VkPipelineStageFlags t,VkDependencyFlags f,uint32_t n,const VkMemoryBarrier *m,uint32_t b,const VkBufferMemoryBarrier *v,uint32_t k,const VkImageMemoryBarrier *i){(void)c;(void)s;(void)t;(void)f;(void)n;(void)m;(void)b;(void)v;assert(k==1&&i->newLayout==VK_IMAGE_LAYOUT_GENERAL);}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer c,VkImage s,VkImageLayout sl,VkImage d,VkImageLayout dl,uint32_t n,const VkImageCopy *r){(void)c;assert(s!=d&&sl==VK_IMAGE_LAYOUT_GENERAL&&dl==sl&&n==1);copies_seen[copies++]=*r;}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice d,VkImageView v,const VkAllocationCallbacks *a){(void)d;(void)v;(void)a;view_destroys++;}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice d,VkImage i,const VkAllocationCallbacks *a){(void)d;(void)i;(void)a;image_destroys++;}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a){(void)d;(void)m;(void)a;memory_frees++;}
typedef struct {unsigned unused;} graphics_pass_key;
typedef struct {VkRenderPass pass;} graphics_pass_entry;
typedef struct {struct {unsigned width,height;} key;VkFramebuffer frame;} graphics_frame_entry;
static graphics_pass_key graphics_pass_key_make(unsigned n,unsigned colors,const VkAttachmentDescription *a,const VkAttachmentReference *r,VkAttachmentReference d){(void)colors;(void)r;(void)d;assert(n==1);attachment_seen=*a;return (graphics_pass_key){0};}
static graphics_pass_entry *graphics_pass_get(graphics_pass_key k,const VkAttachmentDescription *a,const VkAttachmentReference *r,VkAttachmentReference d,int fatal){(void)k;(void)a;(void)r;(void)d;(void)fatal;static graphics_pass_entry p;return &p;}
static graphics_frame_entry *graphics_frame_get(graphics_pass_entry *p,const VkImageView *v,image_entry *const *e,uint32_t w,uint32_t h){(void)p;(void)v;(void)e;static graphics_frame_entry f;f.key.width=w;f.key.height=h;return &f;}
static VkImageView image_view_get(image_entry *e,VkImageViewType t,VkImageSubresourceRange r){assert(t==VK_IMAGE_VIEW_TYPE_2D);view_range=r;return e->view;}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c,const VkRenderPassBeginInfo *i,VkSubpassContents s){(void)c;(void)s;assert(i->clearValueCount==1);clear_seen=*i->pClearValues;pass_begins++;}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c){(void)c;pass_ends++;}
'''

CHECKS = r'''
int main(void){
 memory_properties.memoryTypeCount=1;memory_properties.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
 tex_t t={.serial=1,.d={.target=2,.format=VK_FORMAT_R8G8B8A8_UNORM,.width=16,.height=8,.depth=1,.levels=3},.L={3}};
 image_entry *e=image_get(&t);assert(!(usage_seen&VK_IMAGE_USAGE_STORAGE_BIT));e->populated=1;
 VkImage old=e->image;image_require_storage(e);assert(e->image!=old&&e->retired_image==old&&(e->usage&VK_IMAGE_USAGE_STORAGE_BIT));
 assert(copies==3&&copies_seen[0].extent.width==16&&copies_seen[2].extent.width==4&&copies_seen[2].extent.height==2);
 for(unsigned i=0;i<3;i++)assert(copies_seen[i].srcSubresource.layerCount==3&&copies_seen[i].dstSubresource.layerCount==3&&copies_seen[i].srcSubresource.mipLevel==i);
 assert(!image_destroys&&!view_destroys&&!memory_frees);image_require_storage(e);assert(copies==3&&creates==2);
 image_release_serial(1);assert(!image_list&&!image_index[1]&&image_destroys==2&&view_destroys==2&&memory_frees==2);
 t.serial=2;t.d.flags=NVN_TEXTURE_FLAGS_IMAGE;e=image_get(&t);assert(e->usage&VK_IMAGE_USAGE_STORAGE_BIT);image_require_storage(e);assert(copies==3);image_release_serial(2);
 t.serial=3;t.d.flags=0;t.d.target=NVN_TEXTURE_TARGET_3D;t.d.depth=8;t.L.layers=1;e=image_get(&t);e->populated=1;image_require_storage(e);
 assert(copies==6&&copies_seen[5].extent.depth==2&&copies_seen[5].srcSubresource.layerCount==1);image_release_serial(3);
 t.serial=4;t.d.target=2;t.d.levels=1;t.d.format=0x35;e=image_get(&t);
 VkClearValue v={.depthStencil={0.25f,17}};VkImageSubresourceRange r={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
 assert(graphics_clear_attachment(e,r,&v));assert(attachment_seen.loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR&&attachment_seen.stencilLoadOp==VK_ATTACHMENT_LOAD_OP_LOAD);
 assert(attachment_seen.storeOp==VK_ATTACHMENT_STORE_OP_STORE&&attachment_seen.stencilStoreOp==VK_ATTACHMENT_STORE_OP_STORE);
 assert(view_range.aspectMask==(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT)&&clear_seen.depthStencil.depth==0.25f);
 r.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;assert(graphics_clear_attachment(e,r,&v));assert(attachment_seen.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&attachment_seen.stencilLoadOp==VK_ATTACHMENT_LOAD_OP_CLEAR);
 assert(pass_begins==2&&pass_ends==2);attachment_clear_candidate=0;assert(!graphics_clear_attachment(e,r,&v));assert(pass_begins==2);image_release_serial(4);
 narrow_image_candidate=0;t.serial=5;e=image_get(&t);assert(e->usage&VK_IMAGE_USAGE_STORAGE_BIT);image_release_serial(5);
 puts("render policy: mip/layer preservation, one-way promotion, deferred destruction, depth/stencil preservation PASS");
}
'''


def main():
    root = Path(__file__).resolve().parents[1]
    texture = (root / "native/nvn_vulkan_tex.inc").read_text()
    passes = (root / "native/nvn_vulkan_pass.inc").read_text()
    definitions = "\n".join(function(texture, n) for n in (
        "dimension", "image_allocate", "image_get", "image_require_storage", "image_release_serial"))
    code = PREFIX + definitions + function(passes, "graphics_clear_attachment") + CHECKS
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "test.c"
        source.write_text(code)
        binary = Path(directory) / "test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-I", sys.argv[1], str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()

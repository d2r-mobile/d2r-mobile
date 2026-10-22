"""Exercise production retirement and prologue command assembly with audited Vulkan calls."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def function(source, name):
    match = re.search(r"^(?:static )?[^\n]+\b" + name + r"\([^\n]*\) \{", source, re.M)
    if match is None:
        raise RuntimeError(name)
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


PREFIX = r'''
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#define die(...) abort()
#define VK_CHECK(call) assert((call)==VK_SUCCESS)
typedef struct cached_image_view {VkImageView view;struct cached_image_view *next;} cached_image_view;
typedef struct image_entry {uint64_t serial,release_ticket,allocation_bytes;VkImage image,retired_image;VkImageView view,retired_view;VkDeviceMemory memory,retired_memory;cached_image_view *views,*retired_views;struct image_entry *next,*hash_next;} image_entry;
static image_entry *image_list,*image_retired,*image_index[4096];
static uint64_t image_retired_bytes,image_deferred_releases;
static _Atomic uint64_t submitted,completed;
static VkDevice device;
static unsigned destroyed,forgotten,freed,views;
static int pending_images;
static void graphics_cache_forget_image(image_entry *e){assert(e->release_ticket<=atomic_load(&completed));assert(!pending_images);pending_images=1+(e->retired_image!=VK_NULL_HANDLE);forgotten++;}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice d,VkImageView v,const VkAllocationCallbacks *a){(void)d;(void)v;(void)a;assert(pending_images>0);views++;}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice d,VkImage i,const VkAllocationCallbacks *a){(void)d;(void)i;(void)a;assert(pending_images>0);pending_images--;destroyed++;}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a){(void)d;(void)m;(void)a;freed++;assert(freed==destroyed);}
typedef struct {VkCommandBuffer uploads[8],command;unsigned chunk_index;int upload_recording;} async_batch;
static async_batch *async_current;
static VkCommandPool commands;
static int recording=1;
static unsigned allocations,resets,begins,ends,barriers;
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice d,const VkCommandBufferAllocateInfo *i,VkCommandBuffer *out){(void)d;assert(i->commandBufferCount==1);*out=(VkCommandBuffer)(uintptr_t)(100+allocations++);return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer c,VkCommandBufferResetFlags f){assert(c);assert(!f);resets++;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer c,const VkCommandBufferBeginInfo *b){assert(c&&b->flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);begins++;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer c){assert(c);ends++;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c,VkPipelineStageFlags s,VkPipelineStageFlags t,VkDependencyFlags f,uint32_t n,const VkMemoryBarrier *m,uint32_t b,const VkBufferMemoryBarrier *v,uint32_t k,const VkImageMemoryBarrier *i){(void)f;(void)v;(void)i;assert(c&&s==VK_PIPELINE_STAGE_TRANSFER_BIT&&t==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);assert(n==1&&!b&&!k&&m->srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);assert(m->dstAccessMask==(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT));barriers++;}
'''
MAIN = r'''
static void add_image(uint64_t serial){image_entry *e=calloc(1,sizeof *e);assert(e);e->serial=serial;e->allocation_bytes=4096;e->next=image_list;image_list=e;e->hash_next=image_index[serial&4095];image_index[serial&4095]=e;}
int main(void){
 add_image(1);add_image(4097);submitted=2;image_release_serial(1);
 assert(image_index[1]->serial==4097&&!image_index[1]->hash_next&&image_retired_bytes==4096);
 image_retire_completed(0);assert(!destroyed);completed=1;image_retire_completed(1);assert(!destroyed);
 submitted=3;image_release_serial(4097);assert(!image_list&&!image_index[1]&&image_retired_bytes==8192);
 completed=2;image_retire_completed(2);assert(destroyed==1&&image_retired_bytes==4096);
 completed=3;image_retire_completed(3);assert(destroyed==2&&freed==2&&views==2&&!image_retired_bytes&&!image_retired);
 image_release_serial(1);image_retire_completed(3);assert(destroyed==2&&image_deferred_releases==2);
 add_image(2);image_list->retired_image=(VkImage)(uintptr_t)999;image_list->retired_view=(VkImageView)(uintptr_t)998;image_list->retired_memory=(VkDeviceMemory)(uintptr_t)997;
 image_list->retired_views=calloc(1,sizeof(cached_image_view));assert(image_list->retired_views);image_list->allocation_bytes=8192;
 submitted=4;image_release_serial(2);image_retire_completed(3);assert(destroyed==2&&freed==2&&views==2&&image_retired_bytes==8192);
 completed=4;image_retire_completed(4);assert(destroyed==4&&freed==4&&views==5&&forgotten==3&&!pending_images&&!image_retired_bytes&&!image_retired&&image_deferred_releases==3);
 async_batch batch={.command=(VkCommandBuffer)(uintptr_t)1};async_current=&batch;VkCommandBuffer out[2];
 assert(async_submission_commands(&batch,out)==1&&out[0]==batch.command&&!barriers);
 VkCommandBuffer first=async_upload_command();assert(async_upload_command()==first&&allocations==1&&resets==1);
 assert(async_submission_commands(&batch,out)==2&&out[0]==first&&out[1]==batch.command&&ends==1&&barriers==1);
 batch.chunk_index=1;VkCommandBuffer second=async_upload_command();assert(second!=first);
 assert(async_submission_commands(&batch,out)==2&&out[0]==second&&out[1]==batch.command);
 batch.chunk_index=0;assert(async_upload_command()==first&&allocations==2&&resets==3);
 assert(async_submission_commands(&batch,out)==2&&ends==3&&begins==3);
 puts("PASS production fence retirement, collision unlink, framebuffer/view lifetime and per-chunk prologue ordering");
}
'''


def main():
    native = Path(__file__).resolve().parents[1] / "native"
    tex = (native / "nvn_vulkan_tex.inc").read_text()
    async_source = (native / "nvn_vulkan_async.inc").read_text()
    functions = [function(tex, name) for name in ("image_destroy", "image_retire_completed", "image_release_serial")]
    functions += [function(async_source, name) for name in ("async_upload_command", "async_submission_commands")]
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "test.c"
        source.write_text(PREFIX + "\n".join(functions) + MAIN)
        executable = Path(directory) / "test"
        subprocess.run([sys.argv[2] if len(sys.argv)>2 else "clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-I", sys.argv[1], str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()

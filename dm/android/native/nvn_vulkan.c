// NVN 命令执行的 Vulkan 后端。沿用上游 C 接口名以复用记录层。
// Vulkan NVN command executor. Retain upstream C interface names to reuse the recorder.
#include "../../host/dm.h"
#include "../../gfx/nvn/nvn_obj.h"
#include "../../gfx/nvn/nvn_mtl.h"
#include "../../gfx/nvn/nvn_dirty.h"
#include "../../gfx/tex/nvn_deswizzle.h"
#include "host_posix.h"
#include "legacy_digest.h"
#include "debug_stats.h"
#include "performance_hint.h"
#include "pending_ranges.h"
#ifdef __ANDROID__
#define VK_USE_PLATFORM_ANDROID_KHR
#include <android/native_window.h>
#define DM_VK_DISPATCH_CONTEXT vk_api
#include "gpu_driver.h"
static dm_vk_driver vk_api;
#endif
#include <vulkan/vulkan.h>
#include <stdatomic.h>
#include <errno.h>
#include <dlfcn.h>

#define VK_CHECK(expr) do { VkResult result_ = (expr); if (result_ != VK_SUCCESS) die("Vulkan %s failed: %d", #expr, result_); } while (0)
typedef struct { _Atomic unsigned references; uint64_t activation; } buffer_owner;
typedef struct { VkBuffer buffer; VkDeviceMemory memory; void *mapped; uint64_t size, capacity; buffer_owner *owner; } vk_buffer;
typedef struct cached_buffer { vk_buffer buffer; struct cached_buffer *next; } cached_buffer;
static cached_buffer *buffer_cache;
static pthread_mutex_t buffer_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t buffer_cache_bytes, buffer_allocations, buffer_reuses, snapshot_bytes, active_buffer_bytes, buffer_activation;
// 只读探针：新建一个缓冲（vkCreateBuffer + 分配显存 + 绑定 + 映射）累计花掉的墙钟与线程 CPU。
// 用途是**先证实**"重场面下每帧上百次新建"到底吃掉多少帧时间，再决定怎么优化；只多取两次时钟，不改变行为。
// Read-only probe: wall clock and thread CPU accumulated inside creating a buffer (vkCreateBuffer + allocate +
// bind + map). Its purpose is to establish, before any optimisation, how much of a frame the hundred-plus
// creations in a heavy scene actually cost. Two extra clock reads on that path; nothing else changes.
static uint64_t buffer_create_wall_ns, buffer_create_cpu_ns;
typedef struct read_snapshot { uintptr_t address; uint64_t stamp, used,incarnation,last_acquired; unsigned gpu_only; vk_buffer buffer; struct read_snapshot *next,*prev,*hash_next; } read_snapshot;
static read_snapshot *read_snapshots,*read_snapshot_tail,*read_index[8192];
static uint64_t read_cache_bytes, read_cache_clock, read_cache_hits, read_cache_misses, immutable_buffer_bytes;
static void read_cache_remove(uintptr_t address, uint64_t size);
static void snapshot_diag_poll(void);
static void gpu_snapshot_report(void);
static void pso_warm_start(void);
static unsigned batch_draws;
static void descriptor_reset(void);
static void batch_resources_select(unsigned slot);
static void forward_reset(void);
static uint64_t timestamp_delta(uint64_t begin,uint64_t end,unsigned bits);
typedef struct image_entry image_entry;
static void graphics_pass_end(void);
static int graphics_image_is_attachment(const image_entry *image);
static void graphics_cache_forget_image(image_entry *image);
static void dependencies_prepare_range(uintptr_t address,uint64_t size);
static void dependencies_prepare_texture(const tex_t *texture);
static void dependencies_prepare_copy_texture(const p_copy_b2t *copy);
static void graphics_probe_draw(const p_draw *draw,const p_draw_idx *indexed);
static void graphics_command_reset(void);
static void graphics_probe_dispatch(const p_dispatch *dispatch);
static void graphics_probe_query_batch(uint64_t ticket,const uint64_t *counts,uint32_t count);
static pthread_mutex_t debug_lock=PTHREAD_MUTEX_INITIALIZER;
static dm_android_debug_stats debug_stats;
void dm_android_debug_get(dm_android_debug_stats *out) {pthread_mutex_lock(&debug_lock);*out=debug_stats;pthread_mutex_unlock(&debug_lock);}
#pragma weak dm_nvn_sub_cpu_ns
#pragma weak dm_android_performance_wsi_calls
extern void dm_android_window_frame_period(double period);
#pragma weak dm_android_window_frame_period
// 独立 GPU 夹具没有 HLE heap；未解析时保持原安全读取，真实游戏须链接并记录实际命中。
// Standalone GPU fixtures have no HLE heap; unresolved symbols retain safe reads, while games must link and report real hits.
extern int dm_android_guest_heap_copy_owned(uintptr_t address,void *destination,size_t size);
#pragma weak dm_android_guest_heap_copy_owned
// 独立 GPU 夹具可不链接真实脏页模块；日志明确 API 是否存在。
// Standalone GPU fixtures may omit the real dirty tracker; logs explicitly report API availability.
#pragma weak nvn_dirty_diagnose_write_marks
// 旧独立夹具缺少事务与宿主 guard 时只走原安全全量，不把替身当作可信增量。
// Old standalone fixtures without transactions/host guards use safe full reads, never trusted incremental substitutes.
#pragma weak nvn_dirty_snapshot_begin
#pragma weak nvn_dirty_snapshot_end
#pragma weak nvn_dirty_snapshot_abort
#pragma weak dm_guest_snapshot_try_begin
#pragma weak dm_guest_snapshot_end
#pragma weak dm_guest_host_write_begin
#pragma weak dm_guest_host_write_end
static int snapshot_heap_api_available(void) {
#ifdef __ANDROID__
    return dm_android_guest_heap_copy_owned!=NULL;
#else
    return 0;
#endif
}
typedef struct pool_range { const uint8_t *base; uint64_t size; struct pool_range *next; } pool_range;
typedef struct post_op {
    int kind; volatile uint32_t *event; uint32_t mode, value;
    uint64_t order;int range_registered,counter_gpu_encoded,counter_timestamp_encoded;
    void *destination; uint64_t size; vk_buffer source, output;
    VkRenderPass pass; VkFramebuffer frame; VkPipeline pipeline;
    VkPipelineLayout pipeline_layout; VkDescriptorPool descriptors;
    VkImageView image_view;
    VkBufferView buffer_view;
    VkPipelineStageFlags gpu_stage;VkAccessFlags gpu_access,gpu_synchronized[32];
    struct post_op *next,*copy_next;
} post_op;
static int counter_gpu_record(post_op *post);
static VkInstance instance;
static VkPhysicalDevice physical;
static VkDevice device;
static VkQueue queue;
static VkCommandPool commands;
static VkCommandBuffer command;
static VkFence gpu_fence;
static VkPipelineCache pipeline_cache;
static VkQueryPool draw_queries;
static VkQueryPool batch_times;
static uint32_t gpu_timestamp_bits;
static double gpu_timestamp_period,gpu_total_ns,fence_wait_total_ns;
// queue_lock 保护累计值；worker 发布与前台重叠，不是额外的帧耗时。
// queue_lock protects totals; worker publication overlaps foreground work, not extra frame time.
static uint64_t stability_ring_wait_ns,stability_worker_publish_ns;
static uint32_t used_queries;
static uint64_t query_counts[1024];
static VkPhysicalDeviceMemoryProperties memory_properties;
static VkPhysicalDeviceFeatures gpu_features;
static VkPhysicalDeviceLimits gpu_limits;
static uint32_t gpu_subgroup_size;
static VkSurfaceKHR surface;
static VkSwapchainKHR swapchain;
static VkExtent2D screen_extent;
static VkImage *screen_images;
static uint8_t *screen_initialized;
static VkSemaphore screen_acquired, screen_rendered;
static VkSemaphore *screen_present_wait;
static uint64_t screen_acquire_ticket;
static int screen_submission;
static uint32_t screen_count;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pools_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t completion_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t completion_condition = PTHREAD_COND_INITIALIZER;
static pool_range *pools;
static post_op *post_first, **post_tail;
static post_op *post_copy_first,**post_copy_tail;
static int pending_host_sync;
static uint64_t post_order;
static post_op *const *forward_scope_marker;
static _Atomic uint64_t submitted, completed, pool_adds;
static uint64_t copy_bytes,forward_copy_bytes;
static uint64_t samples_passed, presents;
static int recording;
static struct {
    p_set_rt rt; const prog_t *program;
    p_bind_buf ubo[6][16], ssbo[6][16];
    uint64_t textures[6][128], samplers[6][128], images[6][32];
    va_st attributes[16]; vs_st streams[16]; NVNbufferRange vertices[16];
    color_st color; chmask_st mask; blend_st blend[8]; ms_st multisample; poly_st polygon; ds_st depth;
    p_scissor scissor; p_viewports viewports; p_depth_range depth_range;
    VkStencilOpState stencil_faces[2]; p_poly_offset polygon_offset; p_blend_color blend_color;
    const dpool_t *texture_pool, *sampler_pool; int64_t copy_stride;
} state;
uint64_t dm_android_nvn_time(void) { return dm_android_clock_ns(CLOCK_MONOTONIC); }
int dm_nvn_ruler_truth(void) { return 0; }

#include "nvn_vulkan_pipeline_cache.inc"
#include "nvn_vulkan_cpu_phases.inc"

#ifdef DM_VK_REFERENCE_GATES
#include "nvn_vulkan_test_gate.inc"
#endif

#ifdef __ANDROID__
// 仅初始化失败时释放尚未提交的上下文，再完整退回系统loader；活资源不切换驱动。
// Release an unsubmitted context only on initialization failure, then fully fall back; never switch a live driver.
static int initialize_driver_fallback(const char *reason) {
    if(!vk_api.using_custom)return 0;
    if(device){if(!vk_api.vkDestroyDevice)return 0;vkDestroyDevice(device,NULL);device=VK_NULL_HANDLE;}
    if(surface){if(!vk_api.vkDestroySurfaceKHR)return 0;vkDestroySurfaceKHR(instance,surface,NULL);surface=VK_NULL_HANDLE;}
    if(instance){if(!vk_api.vkDestroyInstance)return 0;vkDestroyInstance(instance,NULL);instance=VK_NULL_HANDLE;}
    physical=VK_NULL_HANDLE;queue=VK_NULL_HANDLE;
    return dm_vk_driver_fallback(&vk_api,reason);
}
#endif

// 交换链与配套信号量/图像的建立。单独成函数，好让"从后台回来"能用新的 ANativeWindow 重建它：
// 安卓在 Activity 停止时必然销毁窗口，而窗口贴图只在启动时建一次（dm_uiscreen.c），所以重返前台必须重建。
// 只能在图形挂起期间调用 —— dm_gfx_suspend(1) 会把游戏线程挡在 NVN 入口之外，否则会与出图线程争交换链。
// Swapchain plus its semaphores and images, as its own function so returning from the background can rebuild it
// from a new ANativeWindow: Android always destroys the window when the activity stops, and the window texture is
// created only once at startup (dm_uiscreen.c). Call it only while graphics are suspended — dm_gfx_suspend(1)
// parks the game thread outside NVN — otherwise it races the presenting thread.
static void screen_create_swapchain(struct ANativeWindow *window) {
    if (!surface || !device) return;
    VkSurfaceCapabilitiesKHR cap; VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &cap));
    if (!(cap.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) die("Vulkan surface has no transfer destination support");
    uint32_t nf; VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &nf, NULL)); VkSurfaceFormatKHR *formats = calloc(nf, sizeof *formats); if (!formats) die("Vulkan surface format allocation"); VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &nf, formats));
    VkSurfaceFormatKHR format = formats[0]; for (unsigned i = 0; i < nf; ++i) if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) { format = formats[i]; break; } free(formats);
    screen_extent = cap.currentExtent;
#ifdef __ANDROID__
    if (screen_extent.width == UINT32_MAX && window) screen_extent = (VkExtent2D){ANativeWindow_getWidth(window), ANativeWindow_getHeight(window)};
#endif
    uint32_t images = cap.minImageCount + 1; if (cap.maxImageCount && images > cap.maxImageCount) images = cap.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; if (!(cap.supportedCompositeAlpha & alpha)) alpha = cap.supportedCompositeAlpha & -cap.supportedCompositeAlpha;
    // 最终 blit 输出是窗口坐标下的正向图像，没有预旋转；由合成器处理屏幕方向。
    // The final blit produces an upright window-space image, not a pre-rotated image; let the compositor handle display orientation.
    VkSurfaceTransformFlagBitsKHR transform = (cap.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : cap.currentTransform;
    LOG("[vulkan-surface-transform] supported=%#x current=%#x selected=%#x extent=%ux%u upright_blit=1",cap.supportedTransforms,cap.currentTransform,transform,screen_extent.width,screen_extent.height);
    VkSwapchainCreateInfoKHR ci = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface, .minImageCount = images, .imageFormat = format.format, .imageColorSpace = format.colorSpace, .imageExtent = screen_extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = transform, .compositeAlpha = alpha, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
    VK_CHECK(vkCreateSwapchainKHR(device, &ci, NULL, &swapchain)); VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &screen_count, NULL)); screen_images = calloc(screen_count, sizeof *screen_images); screen_initialized = calloc(screen_count, 1); if (!screen_images || !screen_initialized) die("Vulkan swapchain image allocation"); VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &screen_count, screen_images));
    // present 消费完成由同一 image 的再次 acquire/wait 保证，不能用提交 fence 推断。
    // Reacquiring/waiting on the same image proves its present wait consumed; a submission fence does not.
    screen_present_wait=calloc(screen_count,sizeof *screen_present_wait);if(!screen_present_wait)die("Vulkan present semaphore allocation");
    VkSemaphoreCreateInfo sem = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VK_CHECK(vkCreateSemaphore(device, &sem, NULL, &screen_acquired));
    for(unsigned i=0;i<screen_count;i++)VK_CHECK(vkCreateSemaphore(device,&sem,NULL,&screen_present_wait[i]));
    LOG("[vulkan] Android game surface %ux%u images=%u", screen_extent.width, screen_extent.height, screen_count);
}

// 释放表面与交换链。销毁前先等队列空闲：调用点已经挂起图形，这里再把在途提交排空，
// 免得销毁的对象还被队列引用。surface 与交换链同生同死 —— 它绑在那一块 ANativeWindow 上。
// Release the surface and swapchain. Wait for the queue first: the caller has suspended graphics, and this drains
// whatever submission is still in flight so nothing is destroyed while the queue references it. The surface dies
// with the swapchain because it is bound to that particular ANativeWindow.
static void screen_release(void) {
    if (!device) return;
    pthread_mutex_lock(&queue_lock); VK_CHECK(vkQueueWaitIdle(queue)); pthread_mutex_unlock(&queue_lock);
    if (swapchain) { vkDestroySwapchainKHR(device, swapchain, NULL); swapchain = VK_NULL_HANDLE; }
    if (screen_acquired) { vkDestroySemaphore(device, screen_acquired, NULL); screen_acquired = VK_NULL_HANDLE; }
    if (screen_present_wait) { for (uint32_t i = 0; i < screen_count; i++) if (screen_present_wait[i]) vkDestroySemaphore(device, screen_present_wait[i], NULL); free(screen_present_wait); screen_present_wait = NULL; }
    free(screen_images); screen_images = NULL; free(screen_initialized); screen_initialized = NULL;
    screen_count = 0; screen_acquire_ticket = 0; screen_extent = (VkExtent2D){0, 0};
    if (surface) { vkDestroySurfaceKHR(instance, surface, NULL); surface = VK_NULL_HANDLE; }
}

#ifdef __ANDROID__
// 单独构建的 GPU 夹具（build_gpu_fixtures.py）只链到本文件，不含宿主图形层，所以这里允许 dm_gfx_suspend 缺失：
// 没有它就不挂起 —— 夹具里也没有并发出图，不存在要防的竞争。
// The standalone GPU fixtures (build_gpu_fixtures.py) link only this file and not the host graphics layer, so
// dm_gfx_suspend is allowed to be absent: without it we simply do not suspend, and the fixtures have no concurrent
// presenting to race against anyway.
__attribute__((weak)) void dm_gfx_suspend(int on);
// 窗口销毁 / 新建时由前端调用。窗口销毁不再等于退出进程：安卓在 Activity 停止时必然销毁窗口，
// 保留进程才能在回到前台时接着玩；真正的退出走 destroyed() 那条路。
// Called by the front end when the window goes away or appears. A destroyed window no longer means the process
// ends: Android always destroys it when the activity stops, and keeping the process is what lets play resume on
// return. A real exit goes through the destroyed() path.
void mtl_screen_detach(void) {
    // 这里**不**调 mtl_init：那会在回调线程（UI 线程）上把整个 Vulkan 后端初始化起来。
    // 后端只由游戏线程在第一次出图时初始化，所以后端还没活就直接返回 —— 那时也没有交换链要释放。
    // mtl_init is deliberately not called here: it would initialise the whole Vulkan backend on the callback
    // thread (the UI thread). The backend is only ever initialised by the game thread on its first present, so
    // when it is not live yet, return — there is no swapchain to release then either.
    if (!instance || !device) return;
    // 自己再挂起一次：window_destroyed 与 paused 的先后顺序不保证，挂起是幂等的，先挂起来才不会与出图线程抢。
    // Suspend here as well: the order of window_destroyed and paused is not guaranteed, suspending is idempotent,
    // and doing it first is what keeps this from racing the presenting thread.
    if (dm_gfx_suspend) dm_gfx_suspend(1);
    screen_release();
    LOG("[android] 窗口销毁：表面与交换链已释放，进程保留");
}
void mtl_screen_attach(struct ANativeWindow *window) {
    // 同上：不在回调线程上初始化后端；后端还没活时，第一次出图会自己建好表面与交换链。
    // Same as above: never initialise the backend on the callback thread; when it is not live yet, the first
    // present builds the surface and swapchain itself.
    if (!instance || !device || !window) return;
    // 先挂起再动交换链，与 detach 同理：安卓给窗口的时机在 onResume **之后**（真机日志实测的顺序是
    // 图形恢复 → 窗口重建），也就是轮到重建时游戏线程已经放开了，正可能在某次出图里读 screen_images /
    // swapchain。挂起会把新进入的出图挡住、并等在途那次退出（gfx_enter/gfx_leave），这里才是唯一安全的点。
    // 后面由前端按"当前是否在前台"决定何时放开 —— 只能由它决定，因为这个回调可能早于或晚于 onResume。
    // Suspend before touching the swapchain, for the same reason as in detach: Android hands the window over
    // *after* onResume (measured on device: 图形恢复 then 窗口重建), so by the time this runs the game thread is
    // already free and may be inside a present reading screen_images / swapchain. Suspending blocks new entries
    // and waits out the one in flight (gfx_enter/gfx_leave), which is what makes this point safe. Releasing it is
    // left to the front end, which alone knows whether the app is in the foreground: this callback can arrive
    // either side of onResume.
    if (dm_gfx_suspend) dm_gfx_suspend(1);
    screen_release();
    VkAndroidSurfaceCreateInfoKHR si = {.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = window};
    VK_CHECK(vkCreateAndroidSurfaceKHR(instance, &si, NULL, &surface));
    screen_create_swapchain(window);
    LOG("[android] 窗口重建：新的表面与交换链已就绪");
}
#endif

static void initialize(void) {
    // 与原 NVN 后端使用相同默认状态；显式指令随后覆盖，不能改写合法深度范围。
    // Match the original NVN defaults; explicit commands override them without rewriting legal depth ranges.
    state.mask.mask=UINT32_MAX;state.depth_range=(p_depth_range){0,1};
    state.color=(color_st){0,NVN_LOGIC_OP_COPY,NVN_ALPHA_FUNC_ALWAYS};
    state.polygon=(poly_st){NVN_FACE_NONE,NVN_FRONT_FACE_CCW,NVN_POLYGON_MODE_FILL,0};
    state.depth=(ds_st){0,1,NVN_DEPTH_FUNC_LESS,0,NVN_STENCIL_FUNC_ALWAYS,NVN_STENCIL_FUNC_ALWAYS,1,1,1,1,1,1};
    for(unsigned i=0;i<8;i++)state.blend[i]=(blend_st){i,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ZERO,NVN_BLEND_FUNC_ONE,NVN_BLEND_FUNC_ZERO,NVN_BLEND_EQUATION_ADD,NVN_BLEND_EQUATION_ADD};
    for(unsigned i=0;i<2;i++)state.stencil_faces[i].compareMask=state.stencil_faces[i].writeMask=255;
#ifdef __ANDROID__
    if(!dm_vk_driver_open(&vk_api))die("Vulkan loader: %s",dm_vk_driver_error(&vk_api));
driver_retry:;
#ifdef DM_VK_REFERENCE_GATES
    // 新物理设备必须重新判断timeline；不能沿用失败custom设备的特性/PFN。
    // Recheck timeline for the new physical device; never retain failed custom-device features/PFNs.
    reference_timeline_supported=0;reference_signal_semaphore=NULL;reference_counter_value=NULL;
#endif
#endif
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "D2R NVN Vulkan", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
#ifdef __ANDROID__
    void *front_metal_layer(void); ANativeWindow *window = front_metal_layer();
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    if (window) { info.enabledExtensionCount = 2; info.ppEnabledExtensionNames = extensions; }
#endif
    VkResult create_result=vkCreateInstance(&info,NULL,&instance);
#ifdef __ANDROID__
    if(create_result!=VK_SUCCESS&&initialize_driver_fallback("vkCreateInstance failed"))goto driver_retry;
#endif
    VK_CHECK(create_result);
#ifdef __ANDROID__
    if(!dm_vk_driver_load_instance(&vk_api,instance,window!=NULL)){if(initialize_driver_fallback(dm_vk_driver_error(&vk_api)))goto driver_retry;die("Vulkan instance dispatch: %s",dm_vk_driver_error(&vk_api));}
    if (window) { VkAndroidSurfaceCreateInfoKHR si = {.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = window};
        create_result=vkCreateAndroidSurfaceKHR(instance,&si,NULL,&surface);
        if(create_result!=VK_SUCCESS&&initialize_driver_fallback("vkCreateAndroidSurfaceKHR failed"))goto driver_retry;VK_CHECK(create_result);
    }
#endif
    uint32_t n = 0; VK_CHECK(vkEnumeratePhysicalDevices(instance, &n, NULL));
    if (!n) {
#ifdef __ANDROID__
        if(initialize_driver_fallback("custom Vulkan has no physical device"))goto driver_retry;
#endif
        die("Vulkan has no physical device");
    }
    VkPhysicalDevice *devices = calloc(n, sizeof *devices); if (!devices) die("Vulkan device list allocation");
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &n, devices));
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < n && family == UINT32_MAX; ++i) {
        uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &nq, NULL);
        VkQueueFamilyProperties *q = calloc(nq, sizeof *q); if (!q) die("Vulkan queue list allocation");
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &nq, q);
        for (uint32_t j = 0; j < nq; ++j)
            if ((q[j].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) { VkBool32 supported = VK_TRUE; if (surface) VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j, surface, &supported)); if (supported) { physical = devices[i]; family = j; gpu_timestamp_bits=q[j].timestampValidBits; break; } }
        free(q);
    }
    free(devices); if (!physical) {
#ifdef __ANDROID__
        if(initialize_driver_fallback("custom Vulkan has no graphics/compute/present queue"))goto driver_retry;
#endif
        die("Vulkan requires a graphics and compute queue");
    }
#ifdef __ANDROID__
    const char *shader_profile_directory=getenv("DM_VK_SHADERS");
#ifdef DM_VK_REFERENCE_GATES
    // 独立fixture用原生GLSL，不含离线Maxwell warp适配；其真实GPU断言仍全部执行。
    // Fixtures use native GLSL without offline Maxwell warp adaptation; retain all real GPU assertions.
    shader_profile_directory=NULL;
#else
    if(vk_api.using_custom&&!shader_profile_directory)die("Custom game driver requires DM_VK_SHADERS and its subgroup profile");
#endif
    if(!dm_vk_driver_validate(&vk_api,physical,shader_profile_directory)){
        if(initialize_driver_fallback(dm_vk_driver_error(&vk_api)))goto driver_retry;
        die("Vulkan driver validation: %s",dm_vk_driver_error(&vk_api));
    }
#endif
    VkPhysicalDeviceProperties properties; VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceSubgroupProperties subgroup={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&subgroup};
    vkGetPhysicalDeviceProperties2(physical,&properties2);properties=properties2.properties;gpu_limits=properties.limits;gpu_subgroup_size=subgroup.subgroupSize;
#ifdef __ANDROID__
    // 缓存按明确选择并已验证支持的shader大小检查；真实物理缺省仍单独记录。
    // Check caches against the explicitly selected and supported shader size; preserve actual physical defaults separately.
    gpu_subgroup_size=vk_api.effective_shader_subgroup_size;
#endif
    vkGetPhysicalDeviceFeatures(physical, &features); gpu_features = features;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    for(uint32_t i=0;i<memory_properties.memoryTypeCount;i++)LOG("[vulkan] memory type %u flags=%#x heap=%u",i,memory_properties.memoryTypes[i].propertyFlags,memory_properties.memoryTypes[i].heapIndex);
    LOG("Android NVN Vulkan: %s API %u.%u BC=%u ASTC=%u", properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), features.textureCompressionBC, features.textureCompressionASTC_LDR);
    LOG("[vulkan] subgroup size=%u stages=%#x operations=%#x quad_all=%u",subgroup.subgroupSize,subgroup.supportedStages,subgroup.supportedOperations,subgroup.quadOperationsInAllStages);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceFeatures enable = {.textureCompressionBC = features.textureCompressionBC, .textureCompressionASTC_LDR = features.textureCompressionASTC_LDR, .independentBlend = features.independentBlend, .shaderImageGatherExtended = features.shaderImageGatherExtended, .fragmentStoresAndAtomics = features.fragmentStoresAndAtomics, .vertexPipelineStoresAndAtomics = features.vertexPipelineStoresAndAtomics, .shaderStorageImageReadWithoutFormat = features.shaderStorageImageReadWithoutFormat, .shaderStorageImageWriteWithoutFormat = features.shaderStorageImageWriteWithoutFormat, .occlusionQueryPrecise = features.occlusionQueryPrecise};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .pEnabledFeatures = &enable};
    const char *swap_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (surface) { di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = &swap_extension; }
    #ifdef DM_VK_REFERENCE_GATES
    reference_gate_device_setup(&di);
#endif
#ifdef __ANDROID__
    if(!dm_vk_driver_prepare_device(&vk_api,&di))die("Vulkan subgroup device policy: %s",dm_vk_driver_error(&vk_api));
#endif
    create_result=vkCreateDevice(physical,&di,NULL,&device);
#ifdef __ANDROID__
    if(create_result!=VK_SUCCESS&&initialize_driver_fallback("vkCreateDevice failed"))goto driver_retry;
#endif
    VK_CHECK(create_result);
#ifdef __ANDROID__
    if(!dm_vk_driver_load_device(&vk_api,device)){if(initialize_driver_fallback(dm_vk_driver_error(&vk_api)))goto driver_retry;die("Vulkan device dispatch: %s",dm_vk_driver_error(&vk_api));}
#endif
    vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family};
    VK_CHECK(vkCreateCommandPool(device, &ci, NULL, &commands));
    VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commands, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &command));
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VK_CHECK(vkCreateFence(device, &fi, NULL, &gpu_fence));
    pipeline_cache_initialize(&properties);
    VkQueryPoolCreateInfo qci={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_OCCLUSION,.queryCount=1024};VK_CHECK(vkCreateQueryPool(device,&qci,NULL,&draw_queries));
    if(gpu_timestamp_bits&&properties.limits.timestampPeriod>0){
        gpu_timestamp_period=properties.limits.timestampPeriod;qci.queryType=VK_QUERY_TYPE_TIMESTAMP;qci.queryCount=2;
        VK_CHECK(vkCreateQueryPool(device,&qci,NULL,&batch_times));
        LOG("[vulkan] GPU timestamps bits=%u period=%.6f ns",gpu_timestamp_bits,gpu_timestamp_period);
    }
    if (surface) {
#ifdef __ANDROID__
        screen_create_swapchain(window);
#else
        screen_create_swapchain(NULL);
#endif
    }
    // 最后启动后台管线预热：读本机积累与随包种子清单，把首次驱动编译从游戏内挪到启动/读取画面（nvn_vulkan_warm.inc）。
    // Start background pipeline warm-up last: read the local accumulation and the shipped seed manifest, moving first driver compilations from gameplay to start-up / the loading screen (nvn_vulkan_warm.inc).
    pso_warm_start();
}
void mtl_init(void) { pthread_once(&once, initialize); }

static vk_buffer buffer_create(uint64_t size) {
    if (!size || size > (1ull << 32)) die("Invalid Vulkan buffer size %llu", (unsigned long long)size);
    uint64_t capacity = 4096; while (capacity < size) capacity *= 2;
    pthread_mutex_lock(&buffer_cache_lock); cached_buffer **link = &buffer_cache;
    // 每次新建或缓存复用都有唯一版本，句柄/owner 地址重用不会命中旧数据的派生缓存。
    // Give each allocation or cache reuse a unique activation so reused handles/owners cannot match stale derived data.
    uint64_t activation=++buffer_activation;if(!activation)die("Vulkan buffer activation overflow");
    while (*link && (*link)->buffer.capacity != capacity) link = &(*link)->next;
    if (*link) { cached_buffer *entry = *link; *link = entry->next; vk_buffer b = entry->buffer;
        buffer_cache_bytes -= capacity; buffer_reuses++; active_buffer_bytes += capacity;
        atomic_store(&b.owner->references,1);b.owner->activation=activation;pthread_mutex_unlock(&buffer_cache_lock); free(entry); b.size = size; return b; }
    buffer_allocations++; active_buffer_bytes += capacity; pthread_mutex_unlock(&buffer_cache_lock);
    vk_buffer b = {.size = size, .capacity = capacity};b.owner=calloc(1,sizeof *b.owner);if(!b.owner)die("Vulkan buffer ownership allocation");atomic_init(&b.owner->references,1);b.owner->activation=activation;
    uint64_t probe_wall = dm_android_clock_ns(CLOCK_MONOTONIC), probe_cpu = dm_android_clock_ns(CLOCK_THREAD_CPUTIME_ID);
    VkBufferCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = capacity, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vkCreateBuffer(device, &ci, NULL, &b.buffer));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, b.buffer, &req);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (memory_properties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { if(type==UINT32_MAX)type=i;if(memory_properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT){type=i;break;} }
    if (type == UINT32_MAX) die("Vulkan has no coherent host-visible buffer memory");
    VkMemoryAllocateInfo mi = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type};
    VK_CHECK(vkAllocateMemory(device, &mi, NULL, &b.memory)); VK_CHECK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
    VK_CHECK(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped));
    buffer_create_wall_ns += dm_android_clock_ns(CLOCK_MONOTONIC) - probe_wall;
    buffer_create_cpu_ns += dm_android_clock_ns(CLOCK_THREAD_CPUTIME_ID) - probe_cpu;
    return b;
}
// 跨批消费者持有同一 backing 的引用，到最后一个真正完成的使用者才允许缓存复用。
// Cross-batch consumers retain the same backing until the last genuinely completed user permits cache reuse.
static vk_buffer buffer_borrow(vk_buffer buffer) {
    if(!buffer.buffer||!buffer.owner)die("Vulkan buffer borrow without backing");
    unsigned references=atomic_fetch_add(&buffer.owner->references,1);if(!references||references==UINT32_MAX)die("Vulkan buffer reference overflow");return buffer;
}
static void buffer_destroy(vk_buffer *b) {
    if (!b->buffer) return;
    if(!b->owner)die("Vulkan buffer missing ownership");unsigned references=atomic_fetch_sub(&b->owner->references,1);if(!references)die("Vulkan buffer ownership underflow");
    if(references>1){memset(b,0,sizeof *b);return;}
    // GPU 完成后才归还快照；缓存总量限制在 64 MiB，不复用仍在执行的缓冲。
    // Return snapshots only after GPU completion; cap the cache at 64 MiB and never reuse in-flight buffers.
    pthread_mutex_lock(&buffer_cache_lock); active_buffer_bytes -= b->capacity;
    if (b->capacity <= (8u << 20) && buffer_cache_bytes + b->capacity <= (64u << 20)) {
        cached_buffer *entry = malloc(sizeof *entry); if (!entry) die("Vulkan buffer cache allocation");
        entry->buffer = *b; entry->next = buffer_cache; buffer_cache = entry; buffer_cache_bytes += b->capacity;
        pthread_mutex_unlock(&buffer_cache_lock); memset(b, 0, sizeof *b); return;
    }
    pthread_mutex_unlock(&buffer_cache_lock);
    vkUnmapMemory(device, b->memory); vkDestroyBuffer(device, b->buffer, NULL); vkFreeMemory(device, b->memory, NULL);free(b->owner); memset(b, 0, sizeof *b);
}
void mtl_pool_add(const void *mem, uint64_t size) {
    mtl_init(); if (!mem || !size || size > UINTPTR_MAX - (uintptr_t)mem) die("Invalid NVN pool");
    if (getenv("DM_VK_TRACE")) { Dl_info info; dladdr((void *)mtl_pool_add, &info); LOG("[vulkan] pool=%p+%llu host=%p thread=%lu stack=%p", mem, (unsigned long long)size, info.dli_fbase, (unsigned long)pthread_self(), __builtin_frame_address(0)); }
    pool_range *p = calloc(1, sizeof *p); if (!p) die("NVN pool allocation");
    p->base = mem; p->size = size; pthread_mutex_lock(&pools_lock); p->next = pools; pools = p; pthread_mutex_unlock(&pools_lock); atomic_fetch_add(&pool_adds, 1);
}
void mtl_pool_del(const void *mem) {
    mtl_finish();pthread_mutex_lock(&queue_lock);
    pthread_mutex_lock(&pools_lock); pool_range **link = &pools;
    while (*link && (*link)->base != mem) link = &(*link)->next;
    if (*link) { pool_range *p = *link; *link = p->next; read_cache_remove((uintptr_t)p->base,p->size); free(p); } pthread_mutex_unlock(&pools_lock);
    pthread_mutex_unlock(&queue_lock);
}
int mtl_addr(uint64_t addr, uint64_t len, void **buf, uint64_t *off) {
    int found = 0; pthread_mutex_lock(&pools_lock);
    for (pool_range *p = pools; p; p = p->next) { uint64_t base = (uintptr_t)p->base;
        if (addr >= base && addr - base <= p->size && len <= p->size - (addr - base)) { if (buf) *buf = (void *)p->base; if (off) *off = addr - base; found = 1; break; } }
    pthread_mutex_unlock(&pools_lock); return found;
}
static post_op *post_new(int kind) {
    if (!recording) die("NVN operation outside a submission");
    if(post_order==DM_PENDING_ORDER_ALL-1)die("Vulkan post order overflow");
    post_op *p = calloc(1, sizeof *p); if (!p) die("NVN completion allocation");
    p->kind = kind;p->order=++post_order; *post_tail = p; post_tail = &p->next;
    if(kind==0){*post_copy_tail=p;post_copy_tail=&p->copy_next;}
    if(kind==1||kind==2)pending_host_sync=1;
    return p;
}

#include "nvn_vulkan_async.inc"
#include "nvn_vulkan_guest_publish.inc"
#include "nvn_vulkan_forward.inc"

void mtl_sub_begin(void) {
    mtl_init(); pthread_mutex_lock(&queue_lock); if (recording) die("Nested NVN submission");
    snapshot_diag_poll();
    unsigned slot=async_begin_locked();batch_resources_select(slot);forward_reset();forward_scope_marker=NULL;pending_index_report();
    VK_CHECK(vkResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_CHECK(vkBeginCommandBuffer(command, &bi)); recording = 1;graphics_command_reset(); post_first = NULL; post_tail = &post_first;post_copy_first=NULL;post_copy_tail=&post_copy_first;pending_host_sync=0;post_order=0; batch_draws = 0;used_queries=0;vkCmdResetQueryPool(command,draw_queries,0,1024);
    if(batch_times){vkCmdResetQueryPool(command,batch_times,0,2);vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,batch_times,0);}
    async_current->chunk_started=dm_android_clock_ns(CLOCK_MONOTONIC);
}
// 按队列公布的有效位处理计数器回绕；不用 CPU 时钟冒充 GPU 测量。
// Handle counter wrap using the queue's advertised bits; never substitute a CPU clock for GPU measurements.
static uint64_t timestamp_delta(uint64_t begin,uint64_t end,unsigned bits) {
    return (end-begin)&(bits>=64?UINT64_MAX:bits?((1ull<<bits)-1):0);
}
static void snapshot_prepare(void) {
    // 普通复制有已知 CPU 输入，可转发；着色器写回与事件/计数器须先真正完成。
    // Ordinary copies have known CPU inputs and can be forwarded; shader writebacks and events/counters must complete first.
    int flush = batch_draws >= 256 || async_batch_temporary_bytes() >= (32u << 20);
    if (flush) { mtl_sub_commit(); mtl_sub_begin(); }
}
typedef struct owned_snapshot_counters {
    uint64_t calls,large_calls,large_bytes,known_calls,known_bytes,heap_calls,heap_bytes,heap_valid_wall_ns,heap_clock_unavailable;
    uint64_t safe_calls,safe_requested_bytes,safe_errors,small_fallback,absent_fallback,refused_fallback;
} owned_snapshot_counters;
static owned_snapshot_counters owned_snapshot_stats;
// 队列锁下每 300 帧采一整帧，最多 12 帧；固定表与退役环不分配内存、不读取 guest 内容。
// Sample one whole frame every 300 frames under the queue lock, at most 12 frames; fixed tables/ring never allocate or read guest contents.
enum { SNAP_DIAG_HIT,SNAP_DIAG_NO_ENTRY,SNAP_DIAG_DIRTY,SNAP_DIAG_LIMIT,SNAP_DIAG_POOL,SNAP_DIAG_UNTRACKED,SNAP_DIAG_PENDING,SNAP_DIAG_LRU,SNAP_DIAG_REASONS };
typedef struct snapshot_source {
    const char *key; uint32_t stage,set,binding,element,kind,flags,cb_slot,cb_offset,slot,handle,role;
    uint64_t bound_bytes;
} snapshot_source;
typedef struct snapshot_diag_row {
    snapshot_source source; char key[17]; uintptr_t address; uint64_t size,calls,forward[7],reasons[SNAP_DIAG_REASONS],history[SNAP_DIAG_REASONS];
    uint64_t old_entries,old_stamp,new_stamp,heap_calls,heap_bytes,heap_wall_ns,heap_unavailable,known_calls,safe_calls,safe_bytes;
    uint64_t dirty_samples,dirty_skipped,dirty_stamp; nvn_dirty_write_marked_stats marks;
    uint64_t gpu_delta_calls,gpu_delta_bytes,gpu_clone_bytes,gpu_trusted_full;
} snapshot_diag_row;
typedef struct snapshot_diag_retirement { uintptr_t address; uint64_t size; unsigned reason; } snapshot_diag_retirement;
static snapshot_diag_row snapshot_diag_rows[32],*snapshot_diag_current;
static const snapshot_source *snapshot_diag_source_current;
static forward_result snapshot_diag_forward;
static snapshot_diag_retirement snapshot_diag_retirements[64];
static uint64_t snapshot_diag_frame,snapshot_diag_overflow_calls,snapshot_diag_overflow_bytes;
static unsigned snapshot_diag_count,snapshot_diag_retire_cursor;
static const char *snapshot_diag_role(unsigned role) {return role==1?"readonly_ssbo":role==2?"compute_ubo_pointer":"readonly_texel";}
static void snapshot_diag_poll(void) {
    int saved_errno=errno;uint64_t next=presents+1;
    if(snapshot_diag_frame&&snapshot_diag_frame!=next){
        LOG("[vulkan-snapshot-sample] frame=%llu rows=%u overflow_calls=%llu overflow_bytes=%llu dirty_api=%u scope=shader_readonly_over64K write_marks_are_not_content_diff=1",
            (unsigned long long)snapshot_diag_frame,snapshot_diag_count,(unsigned long long)snapshot_diag_overflow_calls,(unsigned long long)snapshot_diag_overflow_bytes,(unsigned)(nvn_dirty_diagnose_write_marks!=NULL));
        for(unsigned i=0;i<snapshot_diag_count;i++){snapshot_diag_row *r=&snapshot_diag_rows[i];snapshot_source *s=&r->source;
            LOG("[vulkan-snapshot-source] frame=%llu row=%u shader=%s stage=%u set=%u binding=%u element=%u kind=%u flags=%#x cb_slot=%u cb_offset=%u nvn_slot=%u handle=%u role=%s address=%#llx bytes=%llu bound_bytes=%llu calls=%llu forward_none/full/partial/unrecorded/alignment/bounds/host_effect=%llu/%llu/%llu/%llu/%llu/%llu/%llu cache_hit=%llu miss_no_entry/dirty/limit/pool/untracked/pending=%llu/%llu/%llu/%llu/%llu/%llu absent_last_retire_dirty/limit/pool/untracked/pending/lru=%llu/%llu/%llu/%llu/%llu/%llu old_entries=%llu last_old_stamp=%llu last_new_stamp=%llu heap_calls/bytes/wall_ns/unavailable=%llu/%llu/%llu/%llu known_calls=%llu safe_calls/bytes=%llu/%llu",
                (unsigned long long)snapshot_diag_frame,i,r->key,s->stage,s->set,s->binding,s->element,s->kind,s->flags,s->cb_slot,s->cb_offset,s->slot,s->handle,snapshot_diag_role(s->role),
                (unsigned long long)r->address,(unsigned long long)r->size,(unsigned long long)s->bound_bytes,(unsigned long long)r->calls,
                (unsigned long long)r->forward[0],(unsigned long long)r->forward[1],(unsigned long long)r->forward[2],(unsigned long long)r->forward[3],(unsigned long long)r->forward[4],(unsigned long long)r->forward[5],(unsigned long long)r->forward[6],
                (unsigned long long)r->reasons[SNAP_DIAG_HIT],(unsigned long long)r->reasons[SNAP_DIAG_NO_ENTRY],(unsigned long long)r->reasons[SNAP_DIAG_DIRTY],(unsigned long long)r->reasons[SNAP_DIAG_LIMIT],(unsigned long long)r->reasons[SNAP_DIAG_POOL],(unsigned long long)r->reasons[SNAP_DIAG_UNTRACKED],(unsigned long long)r->reasons[SNAP_DIAG_PENDING],
                (unsigned long long)r->history[SNAP_DIAG_DIRTY],(unsigned long long)r->history[SNAP_DIAG_LIMIT],(unsigned long long)r->history[SNAP_DIAG_POOL],(unsigned long long)r->history[SNAP_DIAG_UNTRACKED],(unsigned long long)r->history[SNAP_DIAG_PENDING],(unsigned long long)r->history[SNAP_DIAG_LRU],
                (unsigned long long)r->old_entries,(unsigned long long)r->old_stamp,(unsigned long long)r->new_stamp,(unsigned long long)r->heap_calls,(unsigned long long)r->heap_bytes,(unsigned long long)r->heap_wall_ns,(unsigned long long)r->heap_unavailable,(unsigned long long)r->known_calls,(unsigned long long)r->safe_calls,(unsigned long long)r->safe_bytes);
            if(r->gpu_delta_calls||r->gpu_trusted_full)LOG("[vulkan-snapshot-delta-source] frame=%llu row=%u shader=%s binding=%u address=%#llx bytes=%llu delta_calls=%llu candidate_bytes=%llu GPU_clone_bytes=%llu trusted_full=%llu content_diff=unknown",(unsigned long long)snapshot_diag_frame,i,r->key,s->binding,(unsigned long long)r->address,(unsigned long long)r->size,(unsigned long long)r->gpu_delta_calls,(unsigned long long)r->gpu_delta_bytes,(unsigned long long)r->gpu_clone_bytes,(unsigned long long)r->gpu_trusted_full);
            if(r->dirty_samples||r->dirty_skipped){nvn_dirty_write_marked_stats *m=&r->marks;
                LOG("[vulkan-snapshot-write-marks] frame=%llu row=%u shader=%s binding=%u address=%#llx bytes=%llu samples=%llu skipped=%llu status=%u page_size=%llu total_pages=%llu write_marked_pages=%llu write_marked_bytes=%llu write_marked_runs=%llu state_none/prot/busy=%llu/%llu/%llu epoch_start/end=%llu/%llu old_stamp=%llu content_diff=unknown",
                    (unsigned long long)snapshot_diag_frame,i,r->key,s->binding,(unsigned long long)r->address,(unsigned long long)r->size,(unsigned long long)r->dirty_samples,(unsigned long long)r->dirty_skipped,(unsigned)m->status,
                    (unsigned long long)m->page_size,(unsigned long long)m->total_pages,(unsigned long long)m->write_marked_pages,(unsigned long long)m->write_marked_bytes,(unsigned long long)m->write_marked_runs,(unsigned long long)m->none_pages,(unsigned long long)m->prot_pages,(unsigned long long)m->busy_pages,(unsigned long long)m->epoch_start,(unsigned long long)m->epoch_end,(unsigned long long)r->dirty_stamp);
            }
        }
        snapshot_diag_frame=0;snapshot_diag_count=0;snapshot_diag_overflow_calls=0;snapshot_diag_overflow_bytes=0;memset(snapshot_diag_rows,0,sizeof snapshot_diag_rows);
    }
    if(!snapshot_diag_frame&&next<=3600&&next%300==0)snapshot_diag_frame=next;
    errno=saved_errno;
}
static snapshot_diag_row *snapshot_diag_source(const snapshot_source *source,uintptr_t address,uint64_t size) {
    if(!snapshot_diag_frame||size<=65536)return NULL;
    int saved_errno=errno;
    for(unsigned i=0;i<snapshot_diag_count;i++){snapshot_diag_row *r=&snapshot_diag_rows[i];const snapshot_source *s=&r->source;
        if(r->address==address&&r->size==size&&s->binding==source->binding&&s->element==source->element&&s->stage==source->stage&&s->set==source->set&&s->role==source->role&&s->slot==source->slot&&s->handle==source->handle&&s->bound_bytes==source->bound_bytes&&!strcmp(r->key,source->key)){r->calls++;errno=saved_errno;return r;}}
    if(snapshot_diag_count==32){snapshot_diag_overflow_calls++;snapshot_diag_overflow_bytes+=size;errno=saved_errno;return NULL;}
    snapshot_diag_row *r=&snapshot_diag_rows[snapshot_diag_count++];r->source=*source;memcpy(r->key,source->key,17);r->source.key=r->key;r->address=address;r->size=size;r->calls=1;errno=saved_errno;return r;
}
static snapshot_diag_row *snapshot_diag_miss_row(uintptr_t address,uint64_t size) {
    if(snapshot_diag_current||!snapshot_diag_source_current||!snapshot_diag_frame||size<=65536)return snapshot_diag_current;
    int saved_errno=errno;
    // 表满时实际 miss 可取代纯命中行；被替换的观测同样计入 overflow，不隐藏容量限制。
    // An actual miss may replace a hit-only row when full; replaced observations count as overflow so limits remain visible.
    for(unsigned i=0;i<snapshot_diag_count;i++){snapshot_diag_row *r=&snapshot_diag_rows[i];uint64_t misses=0;for(unsigned j=1;j<SNAP_DIAG_REASONS;j++)misses+=r->reasons[j];
        if(!misses){snapshot_diag_overflow_calls+=r->calls;snapshot_diag_overflow_bytes+=r->calls*r->size;memset(r,0,sizeof *r);r->source=*snapshot_diag_source_current;memcpy(r->key,r->source.key,17);r->source.key=r->key;r->address=address;r->size=size;r->calls=1;r->forward[snapshot_diag_forward]++;snapshot_diag_current=r;
            // 此调用先前因满表计入过 overflow；现在已获得真实来源行。
            // This call previously overflowed the full table; it now has an actual source row.
            snapshot_diag_overflow_calls--;snapshot_diag_overflow_bytes-=size;errno=saved_errno;return r;}}
    errno=saved_errno;return NULL;
}
static void snapshot_diag_retired(const read_snapshot *entry,unsigned reason) {
    if(entry->buffer.size<=65536)return;
    snapshot_diag_retirements[snapshot_diag_retire_cursor++%64]=(snapshot_diag_retirement){entry->address,entry->buffer.size,reason};
}
static void snapshot_diag_absent(snapshot_diag_row *row) {
    if(!row)return;
    // 仅报告环中真实执行过的最近退役；没有记录意味着历史未知，不能冒称首次创建。
    // Report only the latest retirement actually recorded in the ring; no record means unknown history, not proven first creation.
    unsigned count=snapshot_diag_retire_cursor<64?snapshot_diag_retire_cursor:64;
    for(unsigned i=0;i<count;i++){const snapshot_diag_retirement *r=&snapshot_diag_retirements[(snapshot_diag_retire_cursor-1-i)%64];if(r->address==row->address&&r->size==row->size){row->history[r->reason]++;break;}}
}
static void snapshot_diag_dirty(snapshot_diag_row *row,uint64_t stamp) {
    if(!row)return;
    if(row->dirty_samples||!nvn_dirty_diagnose_write_marks){row->dirty_skipped++;return;}
    int saved_errno=errno;nvn_dirty_diagnose_write_marks((void*)row->address,(size_t)row->size,stamp,&row->marks);errno=saved_errno;row->dirty_samples++;row->dirty_stamp=stamp;
}
static void snapshot_read_account(uint64_t size) {
    // 原快照计数仍是最终数据量；分别报告省掉的客体读取与重复覆盖，不能冒充 GPU 上传归零。
    // Keep logical snapshot byte accounting; report avoided guest reads/redundant overlays separately, never as eliminated GPU uploads.
    static uint64_t metric_frame,metric_hits,metric_reads,metric_overlays,metric_final;
    if(presents&&presents%60==0&&presents!=metric_frame){uint64_t frames=presents-metric_frame;
        LOG("[vulkan-known-copy] frame=%llu hits/frame=%.1f guest_read_saved=%.2f MiB/frame overlay_write_saved=%.2f MiB/frame final_copy=%.2f MiB/frame",(unsigned long long)presents,
            (snapshot_known_copy_hits-metric_hits)/(double)frames,(snapshot_known_copy_read_saved-metric_reads)/(frames*1048576.),
            (snapshot_known_copy_overlay_saved-metric_overlays)/(frames*1048576.),(snapshot_known_copy_final_bytes-metric_final)/(frames*1048576.));
        metric_frame=presents;metric_hits=snapshot_known_copy_hits;metric_reads=snapshot_known_copy_read_saved;metric_overlays=snapshot_known_copy_overlay_saved;metric_final=snapshot_known_copy_final_bytes;
    }
    static uint64_t owned_frame;static owned_snapshot_counters previous;
    if(presents&&presents%60==0&&presents!=owned_frame){uint64_t frames=presents-owned_frame;
        const owned_snapshot_counters *s=&owned_snapshot_stats;
        LOG("[vulkan-owned-snapshot] frame=%llu span=%llu heap_api=%u calls=%.1f/frame large_read_candidate=%.1f/frame large_candidate_bytes=%.3f MiB/frame heap_fast=%.1f/frame heap_bytes=%.3f MiB/frame heap_valid_wall=%.3f ms/frame heap_wall_status=%s known_copy=%.1f/frame known_bytes=%.3f MiB/frame safe_read=%.1f/frame safe_requested=%.3f MiB/frame safe_errors=%llu fallback_small/absent/refused=%.1f/%.1f/%.1f per_frame heap_clock_unavailable=%llu (CPU snapshot bytes, not GPU upload)",
            (unsigned long long)presents,(unsigned long long)frames,(unsigned)snapshot_heap_api_available(),
            (s->calls-previous.calls)/(double)frames,(s->large_calls-previous.large_calls)/(double)frames,
            (s->large_bytes-previous.large_bytes)/(frames*1048576.),(s->heap_calls-previous.heap_calls)/(double)frames,
            (s->heap_bytes-previous.heap_bytes)/(frames*1048576.),(s->heap_valid_wall_ns-previous.heap_valid_wall_ns)/(frames*1e6),
            s->heap_clock_unavailable==previous.heap_clock_unavailable?"complete":"partial",
            (s->known_calls-previous.known_calls)/(double)frames,(s->known_bytes-previous.known_bytes)/(frames*1048576.),
            (s->safe_calls-previous.safe_calls)/(double)frames,(s->safe_requested_bytes-previous.safe_requested_bytes)/(frames*1048576.),
            (unsigned long long)(s->safe_errors-previous.safe_errors),(s->small_fallback-previous.small_fallback)/(double)frames,
            (s->absent_fallback-previous.absent_fallback)/(double)frames,(s->refused_fallback-previous.refused_fallback)/(double)frames,
            (unsigned long long)(s->heap_clock_unavailable-previous.heap_clock_unavailable));
        owned_frame=presents;previous=*s;
    }
    snapshot_bytes += size;
}
static int snapshot_read(uintptr_t address, void *destination, uint64_t size) {
    if (size > UINTPTR_MAX - address) return -1;
    // 按命令顺序覆盖尚未发布的复制结果；客体内存仍到提交完成时才改变。
    // Overlay pending copy results in command order; guest memory still changes only when submission completes.
    if(!async_read_known_full_copy(address,destination,size)){
        if(dm_android_read_memory(address,destination,size))return -1;
        async_overlay_copies(address,destination,size);
    }
    snapshot_read_account(size);return 0;
}
static uint64_t snapshot_diagnostic_clock(void) {
    // 诊断时钟不能改变 helper 或原安全读取观察到的 errno。
    // Diagnostic clocks must not change errno observed by the helper or the original safe reader.
    int saved_errno=errno;uint64_t now=dm_android_clock_ns(CLOCK_MONOTONIC);errno=saved_errno;return now;
}
static int snapshot_read_owned(uintptr_t address,const vk_buffer *destination,uint64_t size) {
    // 只接 buffer_create 刚生成的完整 CPU 目标：零偏移、独占 owner，不能写缓存借用或栈 / shadow。
    // Accept only a complete fresh CPU target from buffer_create: zero offset and exclusive owner, never a cache borrow or stack/shadow.
    if(!destination||!destination->buffer||!destination->memory||!destination->mapped||!destination->owner||
       !destination->owner->activation||atomic_load(&destination->owner->references)!=1||!size||destination->size!=size||
       size>destination->capacity||size>SIZE_MAX||size>UINTPTR_MAX-address||size>UINTPTR_MAX-(uintptr_t)destination->mapped)return -1;
    owned_snapshot_stats.calls++;
    if(size>65536){owned_snapshot_stats.large_calls++;owned_snapshot_stats.large_bytes+=size;}
    if(async_read_known_full_copy(address,destination->mapped,size)){
        owned_snapshot_stats.known_calls++;owned_snapshot_stats.known_bytes+=size;
    }else{
        int copied=0;
        if(size<=65536)owned_snapshot_stats.small_fallback++;
        else if(!snapshot_heap_api_available())owned_snapshot_stats.absent_fallback++;
        else{
            uint64_t started=snapshot_diagnostic_clock();
            copied=dm_android_guest_heap_copy_owned(address,destination->mapped,(size_t)size);
            uint64_t ended=snapshot_diagnostic_clock();
            if(copied!=0&&copied!=1)die("Vulkan owned heap copy contract: result=%d",copied);
            if(copied){owned_snapshot_stats.heap_calls++;owned_snapshot_stats.heap_bytes+=size;
                if(started&&ended>=started)owned_snapshot_stats.heap_valid_wall_ns+=ended-started;else owned_snapshot_stats.heap_clock_unavailable++;
            }else owned_snapshot_stats.refused_fallback++;
        }
        if(!copied){owned_snapshot_stats.safe_calls++;owned_snapshot_stats.safe_requested_bytes+=size;
            if(dm_android_read_memory(address,destination->mapped,size)){owned_snapshot_stats.safe_errors++;return -1;}}
        // heap 直拷仍是当前 guest 基线；保留所有在途 / 当前批已知复制的原顺序覆盖。
        // A heap copy is still the current guest baseline; retain ordered overlays from every pending/current known copy.
        async_overlay_copies(address,destination->mapped,size);
    }
    snapshot_read_account(size);return 0;
}
static int snapshot_pending(uintptr_t address, uint64_t size) {
    return async_copy_pending(address,size);
}
// 哈希索引加双向 LRU：命中/退役不扫描整个缓存，销池仍按真实地址区间失效。
// Hash indexing plus a doubly linked LRU: hits/retirement avoid full-cache scans; pool removal still invalidates actual address ranges.
static unsigned read_bucket(uintptr_t address,uint64_t size) {
    uint64_t key=(address>>4)^(size*0x9e3779b97f4a7c15ull);key^=key>>33;key*=0xff51afd7ed558ccdull;key^=key>>33;return key&8191;
}
static unsigned read_snapshot_bucket(const read_snapshot *entry) {return read_bucket(entry->address,entry->buffer.size)^(entry->gpu_only?4096:0);}
static void read_lru_front(read_snapshot *entry) {
    if(read_snapshots==entry)return;
    if(entry->prev)entry->prev->next=entry->next;
    if(entry->next)entry->next->prev=entry->prev;
    if(read_snapshot_tail==entry)read_snapshot_tail=entry->prev;
    entry->prev=NULL;entry->next=read_snapshots;if(read_snapshots)read_snapshots->prev=entry;else read_snapshot_tail=entry;read_snapshots=entry;
}
static void read_cache_retire(read_snapshot *entry) {
    unsigned bucket=read_snapshot_bucket(entry);read_snapshot **link=&read_index[bucket];while(*link&&*link!=entry)link=&(*link)->hash_next;
    if(!*link)die("Vulkan read cache index corruption");*link=entry->hash_next;
    if(entry->prev)entry->prev->next=entry->next;else read_snapshots=entry->next;
    if(entry->next)entry->next->prev=entry->prev;else read_snapshot_tail=entry->prev;
    read_cache_bytes-=entry->buffer.capacity;if(recording)post_new(3)->source=entry->buffer;else buffer_destroy(&entry->buffer);free(entry);
}
static void read_cache_remove(uintptr_t address,uint64_t size) {
    for(read_snapshot *entry=read_snapshots,*next;entry;entry=next){next=entry->next;if(address<entry->address+entry->buffer.size&&entry->address<address+size){snapshot_diag_retired(entry,SNAP_DIAG_POOL);read_cache_retire(entry);}}
}
static vk_buffer snapshot_buffer(uintptr_t address,uint64_t size) {
    // 只缓存登记池中的只读快照；脏页、复制覆盖和销池均使其失效。
    // Cache only read-only snapshots in registered pools; dirty pages, pending copies and pool destruction invalidate them.
    // 保留原短路次序与每项查询次数；未执行的后续 gate 不能被诊断当作拒绝原因。
    // Preserve original short-circuit order and query counts; diagnostics never infer rejection from unexecuted later gates.
    int limit_ok,pool_ok=-1,tracked_ok=-1,pending=-1;
    int cache=(limit_ok=size<=(64u<<20))&&(pool_ok=mtl_addr(address,size,NULL,NULL))&&(tracked_ok=nvn_dirty_tracked((void*)address,size))&&!(pending=snapshot_pending(address,size));
    unsigned reason=!limit_ok?SNAP_DIAG_LIMIT:!pool_ok?SNAP_DIAG_POOL:!tracked_ok?SNAP_DIAG_UNTRACKED:pending>0?SNAP_DIAG_PENDING:SNAP_DIAG_NO_ENTRY;
    snapshot_diag_row *row=snapshot_diag_current;int old_entry=0;
    unsigned bucket=read_bucket(address,size);
    for(read_snapshot *entry=read_index[bucket];entry;entry=entry->hash_next)if(!entry->gpu_only&&entry->address==address&&entry->buffer.size==size){
        int dirty=cache?nvn_dirty_since((void*)address,size,entry->stamp):0;old_entry=1;
        if(cache&&!dirty){if(row){row->reasons[SNAP_DIAG_HIT]++;row->old_entries++;row->old_stamp=entry->stamp;}entry->used=++read_cache_clock;read_lru_front(entry);read_cache_hits++;return entry->buffer;}
        row=snapshot_diag_miss_row(address,size);if(cache)reason=SNAP_DIAG_DIRTY;
        if(row){row->old_entries++;row->old_stamp=entry->stamp;if(cache&&dirty)snapshot_diag_dirty(row,entry->stamp);}
        snapshot_diag_retired(entry,reason);read_cache_retire(entry);break;
    }
    // 先保护再取代数/读取，避免读取后到保护前的客体写入漏记。
    // Protect before taking the generation/reading, avoiding untracked guest writes between a read and protection.
    row=snapshot_diag_miss_row(address,size);if(row){row->reasons[reason]++;if(!old_entry)snapshot_diag_absent(row);}
    read_cache_misses++;if(cache)nvn_dirty_protect((void*)address,size);uint64_t stamp=nvn_dirty_now();vk_buffer buffer=buffer_create(size);
    owned_snapshot_counters before;if(row){row->new_stamp=stamp;before=owned_snapshot_stats;}
    if(snapshot_read_owned(address,&buffer,size))die("Vulkan read snapshot failed");
    if(row){row->heap_calls+=owned_snapshot_stats.heap_calls-before.heap_calls;row->heap_bytes+=owned_snapshot_stats.heap_bytes-before.heap_bytes;row->heap_wall_ns+=owned_snapshot_stats.heap_valid_wall_ns-before.heap_valid_wall_ns;row->heap_unavailable+=owned_snapshot_stats.heap_clock_unavailable-before.heap_clock_unavailable;row->known_calls+=owned_snapshot_stats.known_calls-before.known_calls;row->safe_calls+=owned_snapshot_stats.safe_calls-before.safe_calls;row->safe_bytes+=owned_snapshot_stats.safe_requested_bytes-before.safe_requested_bytes;}
    if(!cache){post_new(3)->source=buffer;return buffer;}
    while(read_cache_bytes+buffer.capacity>(256u<<20)){snapshot_diag_retired(read_snapshot_tail,SNAP_DIAG_LRU);read_cache_retire(read_snapshot_tail);}
    read_snapshot *entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan read snapshot allocation");entry->address=address;entry->stamp=stamp;entry->used=++read_cache_clock;entry->buffer=buffer;
    entry->hash_next=read_index[bucket];read_index[bucket]=entry;read_lru_front(entry);read_cache_bytes+=buffer.capacity;return buffer;
}
#include "nvn_vulkan_delta.inc"
#include "nvn_vulkan_index.inc"

void mtl_sub_copy(uint64_t src, uint64_t dst, uint64_t n) {
    if (!n) return;
    if (!mtl_addr(dst, n, NULL, NULL)) die("NVN copy destination outside pools: %#llx + %llu", (unsigned long long)dst, (unsigned long long)n);
    snapshot_prepare();
    forward_slice slice;
    if (!(n&3) && forward_find(src,n,4,NULL,&slice)==FORWARD_FULL && !slice.host_written) {
        // 复制 GPU 结果的独立版本；客体写回仍在真实完成后发布，旧消费者保留旧 backing。
        // Copy an independent GPU-result version; guest writeback still follows real completion and earlier consumers retain their old backing.
        graphics_pass_end();forward_read_barrier(&slice,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);forward_hold(&slice);
        post_op *p=post_new(0);p->destination=(void*)(uintptr_t)dst;p->size=n;p->output=buffer_create(n);post_register_range(p);
        VkBufferCopy copy={.srcOffset=slice.offset,.size=n};vkCmdCopyBuffer(command,slice.buffer,p->output.buffer,1,&copy);
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=p->output.buffer,.size=n};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);
        forward_mark_gpu_recorded(p,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);forward_copy_bytes+=n;copy_bytes+=n;return;
    }
    dependencies_prepare_range(src,n);
    post_op *p = post_new(0); p->size = n;
    p->source = buffer_create(n); p->output = p->source;
    if (snapshot_read(src, p->source.mapped, n)) die("NVN copy source unreadable: %#llx", (unsigned long long)src);
    p->destination = (void *)(uintptr_t)dst;
    post_register_range(p);
    // 已知 CPU 输入保留快照并到 GPU 完成后发布；不做临时缓冲之间的冗余复制。
    // Retain known CPU input until GPU completion publishes it; avoid redundant temporary-buffer copies.
    copy_bytes += n;
}
void mtl_sub_event(volatile uint32_t *val, uint32_t mode, uint32_t value) { post_op *p = post_new(1); p->event = val; p->mode = mode; p->value = value;post_register_range(p); }
void mtl_sub_counter(uint64_t addr, uint32_t type) { graphics_pass_end();post_op *p = post_new(2); p->destination = (void *)(uintptr_t)addr; p->value = type;p->size=16;post_register_range(p);counter_gpu_record(p); }
uint64_t mtl_sub_commit(void) {
    if (!recording) die("NVN commit without begin");
    graphics_pass_end();
    if(batch_times)vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,batch_times,1);
    async_record_query_readback_locked();
    VK_CHECK(vkEndCommandBuffer(command));
    uint64_t fence=async_commit_locked();pthread_mutex_unlock(&queue_lock);return fence;
}

uint64_t mtl_last_fence(void) { return atomic_load(&submitted); }
int mtl_wait(uint64_t fence, uint64_t timeout) {
    uint64_t wait_started=dm_android_clock_ns(CLOCK_MONOTONIC);
    pthread_mutex_lock(&completion_lock); int ready = atomic_load(&completed) >= fence,blocked=!ready&&timeout;
    struct timespec until; clock_gettime(CLOCK_REALTIME, &until); uint64_t seconds = timeout / 1000000000ull, ns = until.tv_nsec + timeout % 1000000000ull;
    if (timeout != UINT64_MAX) { until.tv_sec += seconds + ns / 1000000000ull; until.tv_nsec = ns % 1000000000ull; }
    while (!ready && timeout) { int e = timeout == UINT64_MAX ? pthread_cond_wait(&completion_condition, &completion_lock) : pthread_cond_timedwait(&completion_condition, &completion_lock, &until);
        ready = atomic_load(&completed) >= fence; if (e == ETIMEDOUT) break; if (e) die("NVN fence condition: %d", e); }
    pthread_mutex_unlock(&completion_lock);
    if(blocked){uint64_t waited=dm_android_clock_ns(CLOCK_MONOTONIC)-wait_started;pthread_mutex_lock(&queue_lock);fence_wait_total_ns+=waited;pthread_mutex_unlock(&queue_lock);}return ready;
}
void mtl_finish(void) { mtl_wait(mtl_last_fence(), UINT64_MAX); }
void mtl_drain(void) { mtl_init();mtl_finish(); pthread_mutex_lock(&queue_lock); VK_CHECK(vkQueueWaitIdle(queue)); pthread_mutex_unlock(&queue_lock);pipeline_disk_stop_join(); }

#include "nvn_vulkan_tex.inc"
#include "nvn_vulkan_shader.inc"
#include "nvn_vulkan_counter_gpu.inc"
#include "nvn_vulkan_batch_resources.inc"
#include "nvn_vulkan_probe.inc"

static void execute(const uint8_t *p, unsigned depth) {
    if (depth > 32) die("NVN command call recursion");
    for (uint64_t count = 0; count < 10000000; ++count) {
        op_hdr h; memcpy(&h, p, sizeof h); const uint8_t *a = p + sizeof h;
        if (h.size < 8 || (h.size & 7) || h.op >= OP_COUNT_) die("Malformed NVN packet op=%u size=%u", h.op, h.size);
        uint64_t v[3] = {0}; size_t payload = h.size - 8; memcpy(v, a, payload < sizeof v ? payload : sizeof v);
        switch (h.op) {
            case OP_END: return;
            case OP_BEGIN: break;
            case OP_JUMP: p = (const uint8_t *)(uintptr_t)v[0]; continue;
            case OP_CALL: execute((const uint8_t *)(uintptr_t)v[0], depth + 1); break;
            case OP_SIGNAL_EVENT: { uint32_t mode, value; memcpy(&mode, a + 8, 4); memcpy(&value, a + 12, 4); mtl_sub_event((void *)(uintptr_t)v[0], mode, value); break; }
            case OP_REPORT_COUNTER: mtl_sub_counter(v[1], v[0]); break;
            case OP_COPY_B2B: mtl_sub_copy(v[0], v[1], v[2]); break;
            case OP_UPDATE_UB: if (v[1] > payload - 16) die("Short NVN uniform packet"); mtl_sub_copy((uintptr_t)(a + 16), v[0], v[1]); break;
            case OP_BARRIER: { graphics_pass_end();VkMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT}; vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, NULL, 0, NULL); break; }
            case OP_SET_TEX_POOL: state.texture_pool = ((const p_set_pool *)a)->pool; break;
            case OP_SET_SAMP_POOL: state.sampler_pool = ((const p_set_pool *)a)->pool; break;
            case OP_BIND_PROGRAM: state.program = ((const p_bind_program *)a)->prog; break;
            case OP_SET_RT: state.rt = *(const p_set_rt *)a; break;
            case OP_CLEAR_COLOR: clear_color((const p_clear_color *)a); break;
            case OP_CLEAR_DS: clear_depth((const p_clear_ds *)a); break;
            case OP_COPY_B2T: copy_buffer_texture((const p_copy_b2t *)a); break;
            case OP_CLEAR_TEX: clear_texture((const p_clear_tex *)a); break;
            case OP_COPY_T2T: copy_texture_texture((const p_copy_t2t *)a); break;
            case OP_SCISSOR: state.scissor = *(const p_scissor *)a; break;
            case OP_VIEWPORTS: { const p_viewports *v = (const void *)a; if (v->first < 0 || v->n < 0 || v->first + v->n > 16) die("NVN viewport bounds"); for (int i = 0; i < v->n; ++i) memcpy(state.viewports.v[v->first + i], v->v[i], sizeof v->v[i]); state.viewports.n = v->first + v->n; break; }
            case OP_DEPTH_RANGE: state.depth_range = *(const p_depth_range *)a; break;
            case OP_BIND_STATE: { const p_bind_state *b = (const void *)a;
                switch (b->kind) {
                    case 0: { blend_st blend; memcpy(&blend, b->bytes, sizeof blend); state.blend[blend.target] = blend; break; }
                    case 1: memcpy(&state.color, b->bytes, sizeof state.color); break;
                    case 2: memcpy(&state.mask, b->bytes, sizeof state.mask); break;
                    case 3: memcpy(&state.multisample, b->bytes, sizeof state.multisample); break;
                    case 4: memcpy(&state.polygon, b->bytes, sizeof state.polygon); break;
                    case 5: memcpy(&state.depth, b->bytes, sizeof state.depth); break;
                    default: die("NVN state kind %d", b->kind);
                } break; }
            case OP_BIND_UBO: case OP_BIND_SSBO: { const p_bind_buf *b = (const void *)a; if (b->stage < 0 || b->stage >= 6 || b->index < 0 || b->index >= 16) die("NVN buffer binding bounds");
                p_bind_buf *dst=h.op==OP_BIND_UBO?&state.ubo[b->stage][b->index]:&state.ssbo[b->stage][b->index];
                // c0 只依赖绑定的地址/大小；compute 还包含 UBO 地址表。
                // c0 depends only on bound addresses/sizes; compute also includes the UBO table.
                if((h.op==OP_BIND_SSBO||b->stage==5)&&(dst->addr!=b->addr||dst->size!=b->size))uniform_drivers[b->stage][0].state_valid=0;
                *dst=*b;break; }
            case OP_BIND_TEX: case OP_BIND_SAMP: case OP_BIND_IMG: { const p_bind_handles *b = (const void *)a; int max = h.op == OP_BIND_IMG ? 32 : 128; if (b->stage < 0 || b->stage >= 6 || b->first < 0 || b->n < 0 || b->first + b->n > max) die("NVN handle binding bounds"); uint64_t *dst = h.op == OP_BIND_IMG ? state.images[b->stage] : h.op == OP_BIND_TEX ? state.textures[b->stage] : state.samplers[b->stage];
                // driver 表仅编码低32位，image 表只编码前8项；实际资源绑定仍使用完整 state。
                // Driver tables encode low 32 bits and only eight image slots; resource binding still uses full state.
                int limit=h.op==OP_BIND_IMG?8:128;
                if(uniform_drivers[b->stage][1].state_valid)for(int i=0;i<b->n&&b->first+i<limit;i++)if((uint32_t)dst[b->first+i]!=(uint32_t)b->h[i]){uniform_drivers[b->stage][1].state_valid=0;break;}
                memcpy(dst + b->first, b->h, b->n * sizeof *dst); break; }
            case OP_BIND_VATTR: { const p_bind_vattr *b = (const void *)a; if (b->first < 0 || b->n < 0 || b->first + b->n > 16) die("NVN vertex attribute bounds"); memcpy(state.attributes + b->first, b->st, b->n * sizeof *state.attributes); break; }
            case OP_BIND_VSTREAM: { const p_bind_vstream *b = (const void *)a; if (b->first < 0 || b->n < 0 || b->first + b->n > 16) die("NVN vertex stream bounds"); memcpy(state.streams + b->first, b->st, b->n * sizeof *state.streams); break; }
            case OP_BIND_VBUF: { const p_bind_vbuf *b = (const void *)a; if (b->first < 0 || b->n < 0 || b->first + b->n > 16) die("NVN vertex buffer bounds"); memcpy(state.vertices + b->first, b->r, b->n * sizeof *state.vertices); break; }
            case OP_COPY_STRIDE: state.copy_stride = ((const p_copy_stride *)a)->stride; break;
            case OP_BLEND_COLOR: state.blend_color = *(const p_blend_color *)a; break;
            case OP_POLY_OFFSET: state.polygon_offset = *(const p_poly_offset *)a; break;
            case OP_STENCIL: { const p_stencil *s = (const void *)a; for (int i = 0; i < 2; ++i) if (s->face & (1 << i)) { if (s->which == 0) state.stencil_faces[i].compareMask = s->value_mask; else if (s->which == 1) state.stencil_faces[i].writeMask = s->mask; else state.stencil_faces[i].reference = s->ref; } break; }
            case OP_RESET_COUNTER: if (v[0] == NVN_COUNTER_TYPE_SAMPLES_PASSED) {graphics_pass_end();post_new(4);} break;
            case OP_DISPATCH: dispatch_compute((const p_dispatch *)a); break;
            case OP_DRAW: draw_graphics((const p_draw *)a, NULL); break;
            case OP_DRAW_IDX: draw_graphics(NULL, (const p_draw_idx *)a); break;
            default: die("Vulkan NVN packet not implemented: op=%u size=%u (draw=%u indexed=%u dispatch=%u)", h.op, h.size, OP_DRAW, OP_DRAW_IDX, OP_DISPATCH);
        }
        p += h.size;
    }
    die("NVN command traversal exceeds limit");
}
void mtl_exec(const uint8_t *handle) { execute(handle, 0); }
void mtl_exec_rest(const uint64_t *handles, int n) { (void)handles; (void)n; }
void mtl_sub_index(const uint64_t *handles, int n) { (void)handles; (void)n; }

// 未启用的优化和性能采样返回零/未知；实际绘制和读回入口绝不伪造成功。
// Disabled optimizations and performance samples return zero/unknown; drawing and readback never fake success.
void bf_want_note(const char *key) { (void)key; }
void mtl_res_snapshot(mtl_res_count *out) { memset(out, 0, sizeof *out); pthread_mutex_lock(&pools_lock); for (pool_range *p = pools; p; p = p->next) out->pools++; pthread_mutex_unlock(&pools_lock); out->pool_adds = atomic_load(&pool_adds); }
void mtl_res_counts(uint32_t tick, const char *why) { mtl_res_count c; mtl_res_snapshot(&c); LOG("[vulkan] tick=%u %s pools=%u adds=%llu copied=%llu", tick, why, c.pools, (unsigned long long)c.pool_adds, (unsigned long long)copy_bytes); }
void mtl_prog_release(prog_t *p) {pthread_mutex_lock(&queue_lock);program_remove(p);pthread_mutex_unlock(&queue_lock);}
void mtl_xv_window_tex(uint64_t serial) { (void)serial; }
typedef struct frame_stability_sample { uint64_t frame,ns[8]; } frame_stability_sample;
static _Atomic uint64_t present_texture_wait_calls,present_texture_wait_blocks,present_texture_wait_ns;
static int stability_compare(const void *a,const void *b) {
    uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);
}
static void frame_stability_record(uint64_t frame,uint64_t prev_acquire,uint64_t acquire,uint64_t present,uint64_t present_lock) {
    // 只在出图边界取时钟；固定120帧窗口保留最慢帧关联，不改变等待或逐draw计时。
    // Sample only present boundaries; fixed 120-frame windows retain worst-frame attribution without changing waits or timing draws.
    static frame_stability_sample samples[120];static unsigned count;
    static uint64_t last_wall,last_cpu,last_ring,last_worker;static pthread_t thread;
    uint64_t ring,worker;pthread_mutex_lock(&queue_lock);ring=stability_ring_wait_ns;worker=stability_worker_publish_ns;pthread_mutex_unlock(&queue_lock);
    uint64_t wall=dm_android_clock_ns(CLOCK_MONOTONIC),cpu=dm_android_clock_ns(CLOCK_THREAD_CPUTIME_ID);pthread_t current=pthread_self();
    if(last_wall&&pthread_equal(thread,current)&&wall>=last_wall&&cpu>=last_cpu){
        samples[count++]=(frame_stability_sample){frame,{wall-last_wall,cpu-last_cpu,prev_acquire,acquire,present,present_lock,ring-last_ring,worker-last_worker}};
    }else count=0;
    thread=current;last_wall=wall;last_cpu=cpu;last_ring=ring;last_worker=worker;
    if(count!=120)return;
    static const char *names[]={"interval","thread_cpu","prev_acquire","acquire","present","present_lock","ring","worker"};
    uint64_t ordered[120];unsigned over33=0,over50=0,worst=0;
    for(unsigned i=0;i<count;i++){over33+=samples[i].ns[0]>33333333;over50+=samples[i].ns[0]>50000000;if(samples[i].ns[0]>samples[worst].ns[0])worst=i;}
    char summary[1536];size_t used=0;
    for(unsigned field=0;field<8;field++){
        uint64_t sum=0;for(unsigned i=0;i<count;i++){ordered[i]=samples[i].ns[field];sum+=ordered[i];}qsort(ordered,count,sizeof *ordered,stability_compare);
        int written=snprintf(summary+used,sizeof summary-used," %s=%.3f/%.3f/%.3f/%.3f",names[field],sum/(count*1e6),ordered[59]/1e6,ordered[113]/1e6,ordered[119]/1e6);
        if(written<0||(size_t)written>=sizeof summary-used)break;used+=(size_t)written;
    }
    LOG("[vulkan-frame-stability] frame=%llu samples=%u over33ms=%u over50ms=%u mean/p50/p95/max_ms:%s",(unsigned long long)frame,count,over33,over50,summary);
    const frame_stability_sample *s=&samples[worst];
    LOG("[vulkan-frame-worst] frame=%llu interval=%.3f thread_cpu=%.3f prev_acquire=%.3f acquire=%.3f present=%.3f present_lock=%.3f ring=%.3f worker=%.3f ms boundary=present_return_not_display worker_overlaps=1 ring_subset_of_fence_wait=1",(unsigned long long)s->frame,s->ns[0]/1e6,s->ns[1]/1e6,s->ns[2]/1e6,s->ns[3]/1e6,s->ns[4]/1e6,s->ns[5]/1e6,s->ns[6]/1e6,s->ns[7]/1e6);
    count=0;
}
uint64_t mtl_present(const tex_t *t, double period, const int crop[4]) {
    uint64_t acquire_ns=0,present_ns=0,present_lock_ns=0;
    uint64_t present_total=dm_cpu_phase_begin();
    // Present 排队显示；CPU 可见完成仍由真实 sync/finish 和 texture 重用等待保证。
    // Present queues display work; real sync/finish and texture-reuse waits retain CPU-visible completion.
    if(recording){uint64_t phase=dm_cpu_phase_begin();mtl_sub_commit();dm_cpu_phase_end(DM_CPU_PHASE_SUBMIT,phase);}
    // 将真实目标周期告知系统显示策略；不改游戏节拍或GPU同步。
    // Inform system display policy of the actual target period without changing pacing or GPU synchronization.
    if(dm_android_window_frame_period)dm_android_window_frame_period(period);
    uint64_t completion_ticket=mtl_last_fence(),previous_acquire=screen_acquire_ticket;
    uint64_t previous_start=dm_cpu_phase_begin();
    if(previous_acquire&&!mtl_wait(previous_acquire,UINT64_MAX))die("Vulkan prior acquire consumer wait failed");
    uint64_t previous_acquire_ns=dm_android_clock_ns(CLOCK_MONOTONIC)-previous_start;
    dm_cpu_phase_end(DM_CPU_PHASE_WAIT_PRIOR,previous_start);
    uint64_t ticket=++presents;graphics_probe_capture(ticket);
    if(!(ticket%60))LOG("[vulkan-prior-acquire] frame=%llu queued=%llu prior_consumer=%llu wait=%.3f ms scope=prior_binary_acquire_consumer current_guest_publication=async",(unsigned long long)ticket,(unsigned long long)completion_ticket,(unsigned long long)previous_acquire,previous_acquire_ns/1e6);
    if (swapchain) {
        // 前次 blit 已消费 acquire token；当前帧通过独立返回的真实票号追踪完成。
        // The prior blit consumed the acquire token; a separate returned GPU ticket tracks this frame's completion.
        if(screen_acquire_ticket&&!mtl_wait(screen_acquire_ticket,0))die("Vulkan acquire token reused before its consumer completed");
        uint64_t acquire_start=dm_android_clock_ns(CLOCK_MONOTONIC);
        uint32_t index; VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, screen_acquired, VK_NULL_HANDLE, &index);
        acquire_ns=dm_android_clock_ns(CLOCK_MONOTONIC)-acquire_start;
        dm_cpu_phase_end(DM_CPU_PHASE_ACQUIRE,acquire_start);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) die("Vulkan acquire: %d", result);
        if(index>=screen_count)die("Vulkan acquire image bounds");screen_rendered=screen_present_wait[index];
        uint64_t blit_phase=dm_cpu_phase_begin();
        mtl_sub_begin(); image_entry *e = image_get(t); image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = screen_initialized[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = screen_images[index], .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        // 首布局转换与 acquire 的 TRANSFER wait 相接，不越过显示引擎对 image 的使用。
        // Connect the first layout transition to the acquire TRANSFER wait, after the presentation engine's image use.
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        VkClearColorValue black = {.float32 = {0,0,0,1}}; vkCmdClearColorImage(command, screen_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &barrier.subresourceRange);
        VkMemoryBarrier clear_order = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &clear_order, 0, NULL, 0, NULL);
        int sx = crop ? crop[0] : 0, sy = crop ? crop[1] : 0, sw = crop && crop[2] > 0 ? crop[2] : t->d.width, sh = crop && crop[3] > 0 ? crop[3] : t->d.height;
        double scale = (double)screen_extent.width / sw; if (scale * sh > screen_extent.height) scale = (double)screen_extent.height / sh;
        int dw = sw * scale, dh = sh * scale, x = (screen_extent.width - dw) / 2, y = (screen_extent.height - dh) / 2;
        VkImageBlit blit = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}, .srcOffsets = {{sx,sy,0},{sx+sw,sy+sh,1}}, .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}, .dstOffsets = {{x,y,0},{x+dw,y+dh,1}}};
        vkCmdBlitImage(command, e->image, VK_IMAGE_LAYOUT_GENERAL, screen_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0; barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        screen_submission = 1; completion_ticket=screen_acquire_ticket=mtl_sub_commit(); screen_submission = 0; screen_initialized[index] = 1;
        dm_cpu_phase_end(DM_CPU_PHASE_BLIT,blit_phase);
        VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &screen_rendered, .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &index};
        uint64_t present_lock_start=dm_android_clock_ns(CLOCK_MONOTONIC);pthread_mutex_lock(&queue_lock);
        uint64_t present_start=dm_android_clock_ns(CLOCK_MONOTONIC);present_lock_ns=present_start-present_lock_start;
        result = vkQueuePresentKHR(queue, &pi);present_ns=dm_android_clock_ns(CLOCK_MONOTONIC)-present_start;
        dm_cpu_phase_end(DM_CPU_PHASE_PRESENT,present_lock_start);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) die("Vulkan present: %d", result); pthread_mutex_unlock(&queue_lock);
        if(ticket<=3||!(ticket%600))LOG("[vulkan-present] frame=%llu image=%u submit=%llu completed=%llu semaphore_per_image=1 queue_idle=0",(unsigned long long)ticket,index,(unsigned long long)screen_acquire_ticket,(unsigned long long)atomic_load(&completed));
        if (ticket <= 3 || ticket == 30) LOG("[vulkan] Android game frame %llu presented", (unsigned long long)ticket);
    }
    const char *capture = getenv("DM_VK_CAPTURE");
    if (capture && (ticket <= 8 || ticket == 30 || ticket == 60)) {
        if (t->d.format != NVN_FORMAT_RGBA8) die("Vulkan capture format %#x pending", t->d.format);
        unsigned w = t->d.width, h = t->d.height; uint8_t *pixels = malloc((size_t)w * h * 4); if (!pixels) die("Vulkan capture allocation");
        NVNcopyRegion r = {0, 0, 0, w, h, 1}; mtl_read_texels(t, 0, 0, &r, pixels, w * 4, 0);
        char path[1024]; snprintf(path, sizeof path, "%s/frame-%05llu.ppm", capture, (unsigned long long)ticket); FILE *file = fopen(path, "wb"); if (!file) die("Vulkan capture open %s", path);
        fprintf(file, "P6\n%u %u\n255\n", w, h); uint64_t nonblack = 0;
        for (uint64_t i = 0; i < (uint64_t)w * h; ++i) { fwrite(pixels + i * 4, 1, 3, file); nonblack += !!(pixels[i * 4] | pixels[i * 4 + 1] | pixels[i * 4 + 2]); }
        fclose(file); free(pixels); LOG("[vulkan] game frame %llu capture=%s nonblack=%llu draws=%llu", (unsigned long long)ticket, path, (unsigned long long)nonblack, (unsigned long long)draw_count);
    }
    dm_cpu_phase_end(DM_CPU_PHASE_PRESENT_TOTAL,present_total);
    static uint64_t perf_time,perf_submit,perf_bytes,perf_allocations,perf_hits,perf_passes,perf_barriers,perf_create_wall,perf_create_cpu;
    static double perf_gpu,perf_wait;
    uint64_t now=dm_android_clock_ns(CLOCK_MONOTONIC);
    pthread_mutex_lock(&queue_lock);snapshot_diag_poll();gpu_snapshot_report();dependencies_report();counter_gpu_report();graphics_pipeline_report();pipeline_disk_poll();double current_gpu=gpu_total_ns,current_wait=fence_wait_total_ns;
    static uint64_t driver_fast_previous,driver_build_previous,driver_bytes_previous,driver_clear_previous;
    if(ticket==1||ticket%60==0){if(ticket!=1){unsigned frames=ticket==60?59:60;
        LOG("[vulkan-driver-table] frame=%llu fast_hits/frame=%.1f builds/frame=%.1f avoided_table_MiB/frame=%.3f avoided_clear_MiB/frame=%.3f",(unsigned long long)ticket,(uniform_driver_fast_hits-driver_fast_previous)/(double)frames,(uniform_driver_builds-driver_build_previous)/(double)frames,(uniform_driver_avoided_bytes-driver_bytes_previous)/(frames*1048576.),(uniform_clear_avoided_bytes-driver_clear_previous)/(frames*1048576.));}
        driver_fast_previous=uniform_driver_fast_hits;driver_build_previous=uniform_driver_builds;driver_bytes_previous=uniform_driver_avoided_bytes;driver_clear_previous=uniform_clear_avoided_bytes;}
    pthread_mutex_unlock(&queue_lock);
    if(ticket==1){perf_time=now;perf_submit=atomic_load(&submitted);perf_bytes=snapshot_bytes;perf_allocations=buffer_allocations;perf_hits=read_cache_hits;perf_create_wall=buffer_create_wall_ns;perf_create_cpu=buffer_create_cpu_ns;}
    else if(ticket%60==0){unsigned frames=ticket==60?59:60;dm_cpu_phase_report(frames);LOG("[vulkan-perf] frame=%llu fps=%.2f submits/frame=%.1f snapshots/frame=%.1f MB allocations/frame=%.1f new_buf_wall_ms/frame=%.3f new_buf_cpu_ms/frame=%.3f vertex_hits=%llu cache=%llu MB",(unsigned long long)ticket,frames*1e9/(now-perf_time),(double)(atomic_load(&submitted)-perf_submit)/frames,(double)(snapshot_bytes-perf_bytes)/(frames*1048576.),(double)(buffer_allocations-perf_allocations)/frames,(double)(buffer_create_wall_ns-perf_create_wall)/(frames*1e6),(double)(buffer_create_cpu_ns-perf_create_cpu)/(frames*1e6),(unsigned long long)(read_cache_hits-perf_hits),(unsigned long long)(read_cache_bytes>>20));perf_time=now;perf_submit=atomic_load(&submitted);perf_bytes=snapshot_bytes;perf_allocations=buffer_allocations;perf_hits=read_cache_hits;perf_create_wall=buffer_create_wall_ns;perf_create_cpu=buffer_create_cpu_ns;}
    if(ticket==1){perf_gpu=current_gpu;perf_wait=current_wait;}
    else if(ticket%60==0){unsigned frames=ticket==60?59:60;LOG("[vulkan-time] frame=%llu GPU=%.2f ms/frame fence_wait=%.2f ms/frame (GPU batch spans; exclude present/vsync)",(unsigned long long)ticket,batch_times?(current_gpu-perf_gpu)/(frames*1e6):-1,(current_wait-perf_wait)/(frames*1e6));perf_gpu=current_gpu;perf_wait=current_wait;}
    // 异常帧日志：每个出图边界取一次本帧的间隔、线程 CPU 与 GPU；只有越线的帧写进单独文件。
    // Abnormal-frame log: take this frame's interval, thread CPU and GPU at every present boundary; only frames
    // over the thresholds are written to their own file.
    {static uint64_t abnormal_wall,abnormal_cpu,abnormal_gpu;
     uint64_t abnormal_now=dm_android_clock_ns(CLOCK_MONOTONIC),abnormal_thread=dm_android_clock_ns(CLOCK_THREAD_CPUTIME_ID);
     if(abnormal_wall&&abnormal_now>=abnormal_wall&&abnormal_thread>=abnormal_cpu)
         dm_abnormal_frame(ticket,abnormal_now-abnormal_wall,abnormal_thread-abnormal_cpu,current_gpu-abnormal_gpu,draw_count,render_passes,texture_uploads,uniform_version_read_bytes,uniform_version_misses,descriptor_version_misses);
     abnormal_wall=abnormal_now;abnormal_cpu=abnormal_thread;abnormal_gpu=current_gpu;}
    if(ticket==1){perf_passes=render_passes;perf_barriers=image_barriers;}
    else if(ticket%60==0){unsigned frames=ticket==60?59:60;LOG("[vulkan-passes] frame=%llu passes/frame=%.1f image_barriers/frame=%.1f UBO_arena=%.1f MB driver_hits=%llu layout/pass/frame objects=%llu/%llu/%llu",(unsigned long long)ticket,(render_passes-perf_passes)/(double)frames,(image_barriers-perf_barriers)/(double)frames,uniform_arena_bytes/1048576.,(unsigned long long)uniform_driver_hits,(unsigned long long)graphics_layout_creations,(unsigned long long)graphics_pass_creations,(unsigned long long)graphics_frame_creations);perf_passes=render_passes;perf_barriers=image_barriers;}
    static uint64_t binding_hits,binding_misses,descriptor_hits,descriptor_misses,binding_bytes,binding_compare,binding_copy,dependency_waits,binding_overwrites,binding_uploads,binding_unregistered;
    if(ticket==1){binding_hits=uniform_version_hits;binding_misses=uniform_version_misses;descriptor_hits=descriptor_version_hits;descriptor_misses=descriptor_version_misses;binding_bytes=uniform_version_read_bytes;binding_compare=uniform_version_compare_bytes;binding_copy=uniform_version_copy_bytes;dependency_waits=dependency_flushes;}
    else if(ticket%60==0){unsigned frames=ticket==60?59:60;LOG("[vulkan-bindings] frame=%llu UBO byte_hits/miss=%.1f/%.1f descriptors hits/miss=%.1f/%.1f UBO_read=%.2f MB/frame UBO_compare=%.2f MB/frame UBO_arena_copy=%.2f MB/frame dependency_wait=%.1f/frame gpu_overwrite=%.2f/frame uploads=%.2f/frame unregistered_tex=%.2f/frame",(unsigned long long)ticket,(uniform_version_hits-binding_hits)/(double)frames,(uniform_version_misses-binding_misses)/(double)frames,(descriptor_version_hits-descriptor_hits)/(double)frames,(descriptor_version_misses-descriptor_misses)/(double)frames,(uniform_version_read_bytes-binding_bytes)/(frames*1048576.),(uniform_version_compare_bytes-binding_compare)/(frames*1048576.),(uniform_version_copy_bytes-binding_copy)/(frames*1048576.),(dependency_flushes-dependency_waits)/(double)frames,(image_gpu_overwrites-binding_overwrites)/(double)frames,(texture_uploads-binding_uploads)/(double)frames,(gpu_unregistered_textures-binding_unregistered)/(double)frames);binding_unregistered=gpu_unregistered_textures;binding_hits=uniform_version_hits;binding_misses=uniform_version_misses;descriptor_hits=descriptor_version_hits;descriptor_misses=descriptor_version_misses;binding_bytes=uniform_version_read_bytes;binding_compare=uniform_version_compare_bytes;binding_copy=uniform_version_copy_bytes;dependency_waits=dependency_flushes;binding_overwrites=image_gpu_overwrites;binding_uploads=texture_uploads;}
    static uniform_read_counters uniform_read_previous;
    if(ticket==1)uniform_read_previous=uniform_read_stats;
    else if(ticket%60==0){unsigned frames=ticket==60?59:60;const uniform_read_counters *s=&uniform_read_stats;
        LOG("[vulkan-ubo-read] frame=%llu heap_api=%u heap_fast=%.1f/frame heap_bytes=%.3f MiB/frame known_copy=%.1f/frame known_bytes=%.3f MiB/frame safe_fallback=%.1f/frame safe_requested=%.3f MiB/frame safe_errors=%llu",(unsigned long long)ticket,(unsigned)snapshot_heap_api_available(),(s->heap_calls-uniform_read_previous.heap_calls)/(double)frames,(s->heap_bytes-uniform_read_previous.heap_bytes)/(frames*1048576.),(s->known_calls-uniform_read_previous.known_calls)/(double)frames,(s->known_bytes-uniform_read_previous.known_bytes)/(frames*1048576.),(s->safe_calls-uniform_read_previous.safe_calls)/(double)frames,(s->safe_requested_bytes-uniform_read_previous.safe_requested_bytes)/(frames*1048576.),(unsigned long long)(s->safe_errors-uniform_read_previous.safe_errors));uniform_read_previous=*s;}
    static uint64_t command_emits,command_skips,gpu_reads,gpu_seeds,gpu_references;
    if(ticket==1){command_emits=graphics_command_emitted;command_skips=graphics_command_reused;gpu_reads=forward_read_bytes;gpu_seeds=forward_seed_bytes;gpu_references=forward_references;}
    else if(ticket%60==0){unsigned frames=ticket==60?59:60;LOG("[vulkan-forward] frame=%llu driver_commands/frame=%.1f reused/frame=%.1f GPU_read=%.2f MB/frame GPU_seed=%.2f MB/frame retained_backings=%.1f/frame",(unsigned long long)ticket,(graphics_command_emitted-command_emits)/(double)frames,(graphics_command_reused-command_skips)/(double)frames,(forward_read_bytes-gpu_reads)/(frames*1048576.),(forward_seed_bytes-gpu_seeds)/(frames*1048576.),(forward_references-gpu_references)/(double)frames);command_emits=graphics_command_emitted;command_skips=graphics_command_reused;gpu_reads=forward_read_bytes;gpu_seeds=forward_seed_bytes;gpu_references=forward_references;}
    static uint64_t index_hits,index_misses,index_scan_bytes;
    if(ticket==1){index_hits=index_maximum_hits;index_misses=index_maximum_misses;index_scan_bytes=index_maximum_scan_bytes;}
    else if(ticket%60==0){unsigned frames=ticket==60?59:60;LOG("[vulkan-indices] frame=%llu maximum_hits/miss=%.1f/%.1f scan=%.2f MB/frame",(unsigned long long)ticket,(index_maximum_hits-index_hits)/(double)frames,(index_maximum_misses-index_misses)/(double)frames,(index_maximum_scan_bytes-index_scan_bytes)/(frames*1048576.));index_hits=index_maximum_hits;index_misses=index_maximum_misses;index_scan_bytes=index_maximum_scan_bytes;}
    static uint64_t texel_hits,texel_creations,texel_reads,texel_forwarded;
    if(ticket==1){texel_hits=texel_view_hits;texel_creations=texel_view_creations;texel_reads=texel_cpu_read_bytes;texel_forwarded=texel_forward_bytes;}
    else if(ticket%60==0){unsigned frames=ticket==60?59:60;LOG("[vulkan-texel] frame=%llu view_hits/new=%.1f/%.1f CPU_read=%.2f MB/frame forwarded=%.2f MB/frame",(unsigned long long)ticket,(texel_view_hits-texel_hits)/(double)frames,(texel_view_creations-texel_creations)/(double)frames,(texel_cpu_read_bytes-texel_reads)/(frames*1048576.),(texel_forward_bytes-texel_forwarded)/(frames*1048576.));texel_hits=texel_view_hits;texel_creations=texel_view_creations;texel_reads=texel_cpu_read_bytes;texel_forwarded=texel_forward_bytes;}
    static uint64_t debug_time,debug_cpu,debug_draws,debug_passes,debug_submits,debug_bytes;
    static double debug_gpu,debug_wait;
    uint64_t cpu=dm_nvn_sub_cpu_ns?dm_nvn_sub_cpu_ns():0,current_submits=atomic_load(&submitted);
    pthread_mutex_lock(&debug_lock);
    if(debug_time){double weight=debug_stats.frame>1?.15:1;
        debug_stats.frame_ms+=weight*((now-debug_time)/1e6-debug_stats.frame_ms);debug_stats.cpu_ms+=weight*((cpu-debug_cpu)/1e6-debug_stats.cpu_ms);
        debug_stats.gpu_ms+=weight*((current_gpu-debug_gpu)/1e6-debug_stats.gpu_ms);debug_stats.wait_ms+=weight*((current_wait-debug_wait)/1e6-debug_stats.wait_ms);
        debug_stats.draws+=weight*(draw_count-debug_draws-debug_stats.draws);debug_stats.submits+=weight*(current_submits-debug_submits-debug_stats.submits);
        debug_stats.passes+=weight*(render_passes-debug_passes-debug_stats.passes);
        debug_stats.snapshot_mb+=weight*((snapshot_bytes-debug_bytes)/1048576.-debug_stats.snapshot_mb);
    }
    debug_stats.frame=ticket;debug_stats.gpu_valid=batch_times!=VK_NULL_HANDLE;pthread_mutex_lock(&buffer_cache_lock);debug_stats.buffer_mb=(active_buffer_bytes+buffer_cache_bytes)/1048576.;pthread_mutex_unlock(&buffer_cache_lock);pthread_mutex_unlock(&debug_lock);
    debug_time=now;debug_cpu=cpu;debug_draws=draw_count;debug_submits=current_submits;debug_bytes=snapshot_bytes;
    debug_passes=render_passes;
    debug_gpu=current_gpu;debug_wait=current_wait;
    if(dm_android_performance_wsi_calls)dm_android_performance_wsi_calls(acquire_ns+present_ns);
    static uint64_t texture_calls_previous,texture_blocks_previous,texture_ns_previous;
    if(ticket==1||ticket%60==0){uint64_t calls=atomic_load(&present_texture_wait_calls),blocks=atomic_load(&present_texture_wait_blocks),ns=atomic_load(&present_texture_wait_ns);
        if(ticket!=1){unsigned frames=ticket==60?59:60;LOG("[vulkan-texture-reuse-wait] frame=%llu calls/frame=%.2f initially_pending/frame=%.2f wall_ms/frame=%.3f completion=GPU_fence_and_guest_publication",(unsigned long long)ticket,(calls-texture_calls_previous)/(double)frames,(blocks-texture_blocks_previous)/(double)frames,(ns-texture_ns_previous)/(frames*1e6));}
        texture_calls_previous=calls;texture_blocks_previous=blocks;texture_ns_previous=ns;}
    frame_stability_record(ticket,previous_acquire_ns,acquire_ns,present_ns,present_lock_ns);
    return completion_ticket;
}
void mtl_present_wait(uint64_t ticket) {
    if(!ticket)return;
    if(ticket>mtl_last_fence())die("Vulkan unknown present completion ticket %llu",(unsigned long long)ticket);
    uint64_t started=dm_android_clock_ns(CLOCK_MONOTONIC);int pending=atomic_load(&completed)<ticket;
    if(!mtl_wait(ticket,UINT64_MAX))die("Vulkan texture reuse wait failed");
    atomic_fetch_add(&present_texture_wait_calls,1);atomic_fetch_add(&present_texture_wait_blocks,pending);
    atomic_fetch_add(&present_texture_wait_ns,dm_android_clock_ns(CLOCK_MONOTONIC)-started);
}
void mtl_present_flush(void) { mtl_finish(); }
void mtl_present_test_stall(uint64_t ns) { (void)ns; }
int mtl_display_hz(void) {
#ifdef __ANDROID__
    // 有真实 surface 且前端已查询屏幕才报告刷新率；无窗口/未链接前端的 fixture 仍为0。
    // Report the queried refresh rate only with a real surface and frontend; headless or frontend-free fixtures remain zero.
    extern int dm_android_display_hz(void) __attribute__((weak));
    if(surface&&dm_android_display_hz)return dm_android_display_hz();
#endif
    return 0;
}
int mtl_has_window(void) { return surface != VK_NULL_HANDLE; }
int mtl_link_active(void) { return 0; }
int mtl_link_wait_tick(uint64_t timeout) { (void)timeout; return 0; }
int mtl_warm_wait(int timeout) {
    // hle_nvn 在第一次进局读取画面处调用：预热没跑完就在这里等（读取画面多停一会儿），进局后不再现编管线。
    // Called by hle_nvn at the first run-entry loading screen: wait for warm-up here (the loading screen stays a bit longer) so pipelines are not compiled on the fly after entering the run.
    if(!atomic_load(&pso_warm_running))return 0;
    uint64_t start=dm_android_clock_ns(CLOCK_MONOTONIC),limit=(uint64_t)(timeout>0?timeout:0)*1000000ull;
    while(atomic_load(&pso_warm_running)&&dm_android_clock_ns(CLOCK_MONOTONIC)-start<limit)usleep(2000);
    return (int)((dm_android_clock_ns(CLOCK_MONOTONIC)-start)/1000000ull)+1;
}
double mtl_presented_time(uint64_t n) { (void)n; return 0; }
void mtl_set_frame(uint64_t frame) { (void)frame; }
void mtl_ub_present(double interval) { (void)interval; }
double mtl_gpu_ms(uint64_t frame) { (void)frame; return -1; }
int mtl_gpu_span(uint64_t frame, uint64_t *t0, uint64_t *t1) { (void)frame; *t0 = *t1 = 0; return 0; }
void xv_test_counts(int *dc, int *viol) { *dc = *viol = 0; }
void mtl_stats(uint64_t *passes, uint64_t *draws) { *passes = render_passes; *draws = draw_count; }
void mtl_args_stats(uint64_t *enc, uint64_t *reuse) { *enc = *reuse = 0; }
void mtl_use_stats(uint64_t *call, uint64_t *skip) { *call = *skip = 0; }
uint64_t mtl_ds_omit(void) { return 0; }
uint64_t mtl_drawable_ns_take(void) { return 0; }
void mtl_bw(uint64_t *load, uint64_t *store, uint64_t *full) { *load = *store = *full = 0; }
uint64_t mtl_udc_saved_bytes(void) { return 0; }
void mtl_pkg_stats(uint64_t *uploads, uint64_t *reuploads) { *uploads = *reuploads = 0; }
uint64_t mtl_pkg_miss(void) { return 0; }
uint64_t mtl_upload_ns_take(void) { return 0; }
uint64_t mtl_upload_gpu_ns_take(void) { return 0; }
void mtl_create_stats(uint64_t *lib, uint64_t *pso) { *lib = resource_created_count[0]; *pso = resource_created_count[1] + resource_created_count[2]; }
void mtl_create_ns(uint64_t *lib, uint64_t *pso) { *lib = resource_created_ns[0]; *pso = resource_created_ns[1] + resource_created_ns[2]; }
uint64_t mtl_upload_bytes(void) { return 0; }
void mtl_alloc_stats(uint64_t *wait, uint64_t *grow, int *n) { *wait = *grow = 0; *n = 1; }
uint32_t mtl_present_qos(void) { return 0; }
int mtl_fps_target(void) { int dm_fps_setting(void); return dm_fps_setting(); }
void mtl_mem_stat(unsigned long long frame, unsigned long long mb, int ok, unsigned long long total, unsigned long long free_size, unsigned long long max) { (void)ok; (void)total; (void)free_size; (void)max; LOG("[vulkan] frame=%llu resident=%llu MB submits=%llu allocations=%llu reuse=%llu snapshots=%llu MB pipelines=%llu/%llu", frame, mb, (unsigned long long)atomic_load(&submitted), (unsigned long long)buffer_allocations, (unsigned long long)buffer_reuses, (unsigned long long)(snapshot_bytes >> 20), (unsigned long long)pipeline_creations, (unsigned long long)pipeline_reuses); }
void mtl_ui_ghost(int on, float px, float py, const float grid[4], float cx, float cy) { (void)on; (void)px; (void)py; (void)grid; (void)cx; (void)cy; }
int mtl_ui_ghost_rect(float r[4]) { (void)r; return 0; }
void dm_gfx_shot_at(long frame, const char *path) { (void)frame; dm_shot_written(path, 0); }

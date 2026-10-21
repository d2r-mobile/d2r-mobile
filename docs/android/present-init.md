# #139 surface/present-aware Vulkan 初始化

## 范围

新增显式 opt-in 的 `dm_vk_create_present()`，保留现有离屏 `dm_vk_create()`/`dm_vk_create_ex()` 行为不变。它不是完整 swapchain 帧循环，只建立后续 Android swapchain 所需的初始化前提。

## 入口与所有权

```c
typedef VkResult (*dm_vk_surface_factory)(VkInstance instance, void *window, VkSurfaceKHR *surface);
typedef struct {
    uint32_t struct_size;
    void *window;
    dm_vk_surface_factory create_surface;
} dm_vk_present_options;
```

- `window` 是借用的平台窗口，不持有、不释放；上下文不保存它。
- Android 上 `create_surface == NULL` 时使用本实例的 `vkCreateAndroidSurfaceKHR`；host 测试可注入工厂 seam，工厂必须使用传入的同一实例。
- 成功时 `surface` 由 `dm_vk` 拥有，`dm_vk_destroy()` 按 `wait idle → command pool → device → surface → instance` 的逆序清理；失败输出保持全零。
- 设备若声明 `VK_KHR_portability_subset`，创建 logical device 时必须与 swapchain 一起启用；未声明的 Android-like 设备不启用。请求 API 版本取 `min(1.1, loader 报告的版本)` 并写入 `dm_vk.api_version`。

## 选择与扩展

- 实例必须提供 `VK_KHR_surface`；Android 默认路径还要求 `VK_KHR_android_surface`。
- 设备必须提供 `VK_KHR_swapchain`，并且存在同时支持 `GRAPHICS|COMPUTE` 与 `vkGetPhysicalDeviceSurfaceSupportKHR == VK_TRUE` 的队列。
- 不盲目选择第一个物理设备；没有满足条件的设备/队列时失败关闭。
- 设备创建启用 `VK_KHR_swapchain`，命令池与队列来自选中的同一 family。

## 测试

`dm/android/tests/vulkan_present_init.c` 在注入 loader/API 边界上运行实际 `dm_vk_create_present()`：

- 两个物理设备，第一个无 swapchain/present、第二个有；验证选择第二个。
- 缺失 surface 扩展、缺失 swapchain、present=false、surface factory 失败/返回空、device 失败、pool 失败。
- 成功和失败清理计数、同一实例 factory 检查、借用 window、失败输出零句柄。
- 原 `dm_vk_create()` 离屏路径由既有回归覆盖；本测试不声称真实窗口/GPU/fence 验收。

host mock 只证明 API/所有权 seam；Android 真实 surface/设备验收仍属于父 #18，未在本切片声称。

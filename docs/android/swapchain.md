# #141 自有 VK_KHR_swapchain 连续帧边界

## 范围

`dm_vk_swapchain_*` 是唯一有界的 WSI 切片：在 #139 的 `dm_vk_create_present()` 上下文上创建真实 `VK_KHR_swapchain`，执行 `acquire → clear command submit → present`，并显式处理重建、同步和失败退役。它不是完整渲染器，不含 NVN 游戏命令、HUD 合成、性能调优或 NativeActivity 接入。

## 协商与所有权

- 通过 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`、`vkGetPhysicalDeviceSurfaceFormatsKHR`、`vkGetPhysicalDeviceSurfacePresentModesKHR` 与 `vkGetPhysicalDeviceFormatProperties` 协商：
  - 仅接受 `VK_PRESENT_MODE_FIFO_KHR`；
  - `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` 必须受支持；
  - 固定 `currentExtent` 直接采用；未定义时把请求尺寸夹到 `min/maxImageExtent`；
  - 优先 `VK_FORMAT_B8G8R8A8_UNORM`，其次 `VK_FORMAT_R8G8B8A8_UNORM`，再退到首个支持 color attachment 的格式；
  - image count 使用 `minImageCount`，并检查 `maxImageCount`。
- 必须由 `dm_vk_create_present()` 协商 `VK_EXT_surface_maintenance1` 与其依赖 `VK_KHR_get_surface_capabilities2`（实例层），并在 Vulkan ≥1.1 下查询/启用设备 `VK_EXT_swapchain_maintenance1` 的 `swapchainMaintenance1`；缺少任一实例依赖、设备扩展、feature 或 Vulkan 1.0 properties2 路径时 maintenance 不支持，swapchain 创建 fail-closed。
- 自己拥有 `VkSwapchainKHR`、render pass、image views、framebuffers、command buffers（池仍由 `dm_vk` 拥有）、一个 acquire semaphore、每 image 一个 present semaphore、每 image 一个 present fence，加一个 submit fence 和一个 acquire fence；`VkImage` 由驱动拥有，仅保存句柄。
- `dm_vk_swapchain_destroy()` 先等待每个 in-flight present fence（以及 submit fence），确认呈现资源可释放后才按 fence/semaphore → command buffers → framebuffers → views → render pass → swapchain 的顺序销毁；任一证明失败则返回错误并保留全部对象，不静默释放。

## 帧同步

- 每帧 acquire 使用 `image_available` semaphore，并把独立 acquire fence 交给 `vkAcquireNextImageKHR`；acquire fence 证明 acquisition operation 完成（包括 semaphore signal），在复用或销毁前等待。
- 同一 image index 再次 acquire 后，先等待该 image 的 present fence，证明上一轮呈现资源（包括 present wait semaphore）可复用，再允许 submit 重新 signal `render_finished[image]`；per-image semaphore + reacquire proof 是 primary guide 的无扩展安全路径。
- 每次 present 通过 `VkSwapchainPresentFenceInfoEXT` 附带 per-image present fence。submit fence 只证明 submit 完成；呈现资源的释放由 maintenance1 present fence 证明（不代表图像已在屏幕显示完毕），绝不以 submit fence 或 `vkQueueWaitIdle` 代替。
- acquire→destroy/recreate 和 acquired→submit 失败都先等待 acquire fence；等待失败时返回错误并保留对象。

## OUT_OF_DATE / SUBOPTIMAL / 重建

- `acquire` 返回 `VK_ERROR_OUT_OF_DATE_KHR` 时不产生图像、释放任何 in-flight 对象，仅置 `needs_recreate`。
- `acquire` 返回 `VK_SUBOPTIMAL_KHR` 时仍返回可用图像并设置 `suboptimal`，调用者可以先 present 再重建。
- `present` 返回 `VK_ERROR_OUT_OF_DATE_KHR` 或 `VK_ERROR_SURFACE_LOST_KHR` 时，规范仍将操作视为已入队；保留原始返回值并置 `needs_recreate`，跟踪并等待 maintenance present fence 后才退役。OUT_OF_DATE 可以重建继续呈现；SURFACE_LOST 还需要调用者重新创建 surface。其它错误保守设置 `retire_blocked`/`retire_error` 并保留对象，不靠 device idle 释放。
- 协议依据：[vkQueuePresentKHR 错误入队语义](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)与 [present fence 资源释放保证](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)。
- `dm_vk_swapchain_recreate()` 先等待所有 in-flight acquire/present/submit fences，再把旧 handle 作为 `oldSwapchain` 创建新链；成功后销毁旧对象。新链创建失败时也按 fail-closed 销毁旧对象，不提供“旧链接管”假象；present 证明失败时不销毁。
- 测试覆盖 acquire 顺序 `0,1,2,0`、present fence 延迟、acquire→destroy/recreate、acquired→submit 失败、acquire fence 等待失败时保留对象，以及缺少实例依赖/feature=false/Vulkan 1.0 的 fail-closed 协商。

## 失败与测试边界

`dm/android/tests/vulkan_swapchain.c` 通过注入 Vulkan API seam 驱动实际生产函数，覆盖 4 次 acquire（`0,1,2,0`）、延迟 presentation、acquire→destroy/recreate、重建、capability/FIFO 拒绝、每类关键创建/提交/呈现失败、OUT_OF_DATE/SUBOPTIMAL、清理顺序与计数。

host seam 不证明真实 Android 窗口、Adreno 设备、GPU fence/present 完成、像素内容或帧率；这些仍需 NativeActivity 接入和手机实测，父 #18 保持开放。

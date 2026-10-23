> 历史检查（2026-10-15）；最新集成和真机结果见 [安卓协作状态](../../../docs/ANDROID_COLLABORATION.zh-CN.md)。
> Historical check (2026-10-15); see the [current Android status](../../../docs/ANDROID_COLLABORATION.md) for integration and device results.

# 安卓真实 Vulkan 后端候选（2026-10-15）

本轮为可编译/链接的生产路径候选，尚未接入整 App，也未真机出图。不能据此报告游戏 FPS、已运行或画面正确。

## 文件与接线

- `nvn_vk.c` 接替 CPU 空执行层，仍实现上游 `nvn_mtl.h` / `nvn_obj.h` 的 `mtl_*` ABI。
- `engine/` 复制只读鸿蒙 gfx50-claude-perf 的真正 NVN→Vulkan 命令回放实现；原始文件 SHA-256 在 `engine/SOURCE.json`。未复制另一份 `xbg_game_nvn.c`，避免替换/重定义上游 NVN ABI。bcdec 附带原有许可证。
- `../../android/vulkan_present.cpp/.h` 独占新增 Android 窗口/实例/设备/交换链。JNI Surface 创建/销毁分别调用 `dm_android_vk_set_window(window)` / `dm_android_vk_set_window(NULL)`；函数内部 retain/release，不在 UI 线程调用 Vulkan。调用方仍须释放自身 `ANativeWindow_fromSurface` 引用。
- 在 `dm_main` 前且窗口有效时调用 `dm_android_vk_initialize(shader_dir,error,bytes)`。目录必须为外部匹配当前 shader control 的真实 `.xgs`，路径内部复制。失败需中止启动；不能退回无图形层伪报成功。
- 运行前实际启用设备支持的 core features、shaderDrawParameters；BC 路径依 `enabled_core.textureCompressionBC`，无 Maleoon 特判。镜像边缘扩展仅实际启用才告知引擎。
- 引擎所需 `nvn_new_serial`、`nvn_tex.c`、`nvn_deswizzle.c` 继续来自宿主现有目标，不能重复定义。

CMake可 `add_subdirectory(../gfx/vulkan ...)` 后将 `dm_vulkan_engine` 链入 dmcore；dmcore仍编 `nvn_vk.c`，新增编 `vulkan_present.cpp`。启用 CXX17；链接 vulkan/android/log/libc++；`engine/xbg_texture.c` 须 `-fsigned-char`。不让宿主强制 C compat 头污染 C++。备用源清单：engine 下全部 `.c/.cpp`（共15个）及 present.cpp；CMakeLists.txt 明列15个源。

## 安卓差异与同步

- 保留现有 Metal ABI 的 ID 全局描述符登记语义：上游 `mtl_reg_tex/mtl_reg_samp` 没有 pool 参数，引擎使用全局登记作为兼容表。没有假称解决上游多同号描述符池歧义；如游戏触发，需要另经授权修上游接口。
- 禁异步翻译，当前游戏命令/对象指针在提交返回前解析完毕。
- 禁 SIGSEGV/mprotect 脏页追踪及外部内存 remap 探针；不接管运行库信号链。
- `OP_SIGNAL_EVENT` 先完成此前 GPU 工作再发布 CPU 事件值；栅栏依据真实 VkFence。有限 timeout 会返回0，不能立即标记完成。计数器和读回继续引擎既有 GPU 完成后路径。
- present 使用真正渲染且已转 TRANSFER_SRC 的图像，离屏图→swapchain blit，FIFO/按比例居中；回调等 blit 和队列完成后成功。Surface丢失等待JNI生命周期替换，后台无窗口不跳帧计数。
- GPU耗时/真实scanout时间尚未接入，`mtl_gpu_ms=-1`、`mtl_presented_time=0`、刷新率未知0；不得用CPU时刻冒充实际上屏时间。

## 本轮证据

命令（不访问设备/游戏资源）：

`python dm/gfx/vulkan/check_build.py --ndk <ANDROID_SDK>/ndk/29.0.14206865 --output <PRIVATE_WORK>/vulkan-validation`

NDK29 clang21 arm64 Android API29：15 engine对象、adapter、present、两个现有格式/解排布依赖均编译成功；使用明确的三项宿主测试替身（log_/die/nvn_new_serial）完成 `--no-undefined` 图形链路共享库链接。完整命令/输出/源码hash在仓外 `work/vulkan-validation/validation.json`，不是整App验证。最后present生命周期修订已单对象重编并重链接。

产物 `libdm_vulkan_link_check.so` SHA256：`5099946a34fbf50baca3899d105acf0fa3f16b924f21f36c7231cf9da55af62c`。

## 尚未验证/接续

整App CMake/JNI接线、匹配着色器和资源导入、设备验证层、真机游戏图像/UV方向/裁剪、原生BC纹理、绘制/计算/遮挡查询、窗口切换和后台恢复、耗时与帧率。当前同步呈现和禁异步翻译是正确性基线，性能需真实热点证据再优化。进程级后端尚无退出后重新初始化接口，重启游戏需重启App进程；不能在设备/客体线程仍运行时销毁GPU对象。本轮未安装、未清数据、未改鸿蒙、未提交推送。

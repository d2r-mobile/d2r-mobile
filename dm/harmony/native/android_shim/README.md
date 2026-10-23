# 安卓头文件替身（仅鸿蒙构建使用） | Android header stand-ins (HarmonyOS build only)

让 `dm/android` 里与平台无关、只碰少量安卓 API 的源文件（如 `vulkan_present.cpp`）**原样**在鸿蒙上编译，安卓侧的修复自动带到鸿蒙，不复制、不分叉。
只映射语义完全对应的接口：`ANativeWindow` → `OHNativeWindow`，安卓 Vulkan 表面 → `VK_OHOS_surface`，`__android_log_*` → hilog。

Lets platform-neutral files in `dm/android` that touch only a few Android APIs (such as `vulkan_present.cpp`) compile **unchanged** on
HarmonyOS, so Android fixes carry over without copying or forking. Only interfaces with matching semantics are mapped:
`ANativeWindow` → `OHNativeWindow`, the Android Vulkan surface → `VK_OHOS_surface`, `__android_log_*` → hilog.

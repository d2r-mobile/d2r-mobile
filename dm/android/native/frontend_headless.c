// 无窗口的真实宿主启动入口；NVN 交给 Vulkan 执行层。
// Real headless host boot entry; delegate NVN to the Vulkan executor.
#include "dm.h"
#include "hle.h"
#include "front.h"
#include <dlfcn.h>
// 无窗口探针没有 GPU 工作和出图计数；截图请求直接报未实现。
// The headless probe has no GPU work or presented frames; screenshot requests explicitly fail.
int dm_thermal_now(void) {
    void *library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) return -1;
    void *(*acquire)(void) = dlsym(library, "AThermal_acquireManager");
    void (*release)(void *) = dlsym(library, "AThermal_releaseManager");
    int (*status)(void *) = dlsym(library, "AThermal_getCurrentThermalStatus");
    int result = -1;
    if (acquire && release && status) {
        void *manager = acquire();
        if (manager) { int value = status(manager); result = value < 0 ? -1 : value == 0 ? 0 : value <= 2 ? 1 : value == 3 ? 2 : 3; release(manager); }
    }
    dlclose(library); return result;
}
static void *guest_thread(void *arg) {
    // 驱动首次加载会使用很深的宿主调用栈，先于 SDK 切换到游戏的小栈初始化。
    // Driver loading uses a deep host call stack; initialize before the SDK switches to the game's small stack.
    void mtl_init(void); mtl_init(); ((void (*)(void))arg)(); return NULL;
}
void front_run(void (*guest)(void)) {
    // 与原前端相同，给游戏独立的 64 MiB 栈；Vulkan 驱动不能挤在 shell 主线程栈上。
    // Match the original frontend's separate 64 MiB guest stack; Vulkan drivers must not share the small shell main stack.
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_t thread; int error = pthread_create(&thread, &attr, guest_thread, (void *)guest); pthread_attr_destroy(&attr);
    if (error) die("Android guest pthread_create: %d", error); pthread_join(thread, NULL);
}
void *front_metal_layer(void) { return NULL; }
uint64_t front_game_res(int docked) { return docked ? ((uint64_t)900 << 32) | 1600 : ((uint64_t)720 << 32) | 1280; }
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max, char *out, int cap) {
    (void)title; (void)sub; (void)guide; (void)initial; (void)max; (void)out; (void)cap; return -1;
}
int main(int argc, char **argv) { int dm_main(int, char **); return dm_main(argc, argv); }

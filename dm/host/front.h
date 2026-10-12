// 宿主前端（Mac：front_mac.m；iOS：dm/ios 另实现同名接口）。
// Host front end (Mac: front_mac.m; iOS: dm/ios separately implements the same-named interface).
#pragma once
// 跑客体：有窗口时客体在独立线程、主线程归系统 UI；无窗口时直接在当前线程跑。
// Run the guest: with a window the guest runs on its own thread and the main thread belongs to the system UI; without a window it runs directly on the current thread.
void front_run(void (*guest)(void));
// 图形后端出图的 CAMetalLayer（无窗口时为 NULL）
// The CAMetalLayer the graphics backend presents frames to (NULL when there is no window)
void *front_metal_layer(void);
// 游戏输出分辨率（改写器 PARAMS 分辨率参数位）：docked = 1 主机模式 / 0 掌机模式，返回 (u64)高 << 32 | 宽。
// 原版常数 1600x900 / 1280x720（16:9）；屏幕比例不同的前端按同高度放宽，游戏原生按新宽度渲染并重排界面（宽 ≤ 4096）。
// Game output resolution (the rewriter's PARAMS resolution parameter slot): docked = 1 docked mode / 0 handheld mode, returns (u64)height << 32 | width.
// The original game's constants are 1600x900 / 1280x720 (16:9); front ends with a different screen aspect ratio widen at the same height, and the game natively renders at the new width and re-lays out the UI (width ≤ 4096).
#include <stdint.h>
uint64_t front_game_res(int docked);
// 文本输入框（软键盘 swkbd 用）：主线程弹原生输入框，调用线程阻塞到用户确定 / 取消。
// 文本均为 UTF-8；max_chars 为最大字数（0 = 不限，仅供前端提示；截断由调用方 hle_swkbd.c 按 UTF-16 单元统一做）。
// 确定返回 1 并把结果写进 out（含结尾 0），取消返回 0；
// 无窗口（front_metal_layer() == NULL）时不弹，返回 -1。
// Text input box (for the software keyboard swkbd): the main thread shows a native input box and the calling thread blocks until the user confirms / cancels.
// All text is UTF-8; max_chars is the maximum character count (0 = unlimited, only a hint for the front end; truncation is done uniformly by the caller hle_swkbd.c in UTF-16 units).
// Confirm returns 1 and writes the result into out (including the terminating 0); cancel returns 0;
// with no window (front_metal_layer() == NULL) nothing is shown and -1 is returned.
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap);

// 宿主装载提前起跑（main.c）：读 NSO / 重定位 / 强制接管 / 预取起跑放到后台线程，与前端（UIKit 启动链、Metal 视图）并行；
// 随后调用的 dm_main / main 会等它完成。调用时机与前提见 main.c 注释（先设好 DM_ROMFS 等环境变量与 cwd，参数与随后 main 的逐项相同）。
// Early start of host loading (main.c): reading the NSO / relocation / forced takeover / prefetch start are moved to a background thread, in parallel with the front end (UIKit launch chain, Metal view);
// the dm_main / main called afterwards waits for it to finish. For call timing and preconditions see the comments in main.c (set DM_ROMFS and the other environment variables and the cwd first; arguments identical item by item to those of the later main).
void dm_host_preload_start(int argc, char **argv);

// 触屏（宿主 hle_touch.c 实现；iOS / Mac 前端调）：触点集合一有变化（按下 / 移动 / 抬起 / 取消）就整组推一次，n = 0 = 全抬起。
// id = 手指槽号 0–15（按下取最小空槽、抬起前不变，直接当 finger_id）；x / y = 出图画面上的归一化坐标 [0,1)，左上为原点。
// 宿主换算到游戏读的面板坐标 1280x720（x·1280、y·720，main 0x1b65260 的读法），并锁存短按（两次读之间按下又抬起至少报一次）。可在任意线程调。
// Touchscreen (implemented by the host's hle_touch.c; called by the iOS / Mac front end): whenever the set of touch points changes (down / move / up / cancel) the whole set is pushed once; n = 0 = all lifted.
// id = finger slot number 0–15 (the lowest free slot on down, unchanged until up; used directly as finger_id); x / y = normalized coordinates [0,1) on the output frame, origin at top left.
// The host converts to the panel coordinates the game reads, 1280x720 (x·1280, y·720, the way main 0x1b65260 reads them), and latches short taps (a down and up between two reads is reported at least once). May be called from any thread.
typedef struct { uint32_t id; float x, y; } dm_touch;
void dm_touch_set(const dm_touch *t, int n);

// 存档未保存（宿主 hle_fs.c 调；iOS 前端写同名强实现弹原生提示「存储空间不足，进度未保存」，缺省弱实现只记日志）。
// 时机：游戏一次存档事务（两次 Commit 之间）里有存档写失败（盘满等），这次 Commit 被拒、已提交的旧档不变时，每个被拒的事务调一次；
// 下一个事务成功就照常提交，前端不必做恢复动作。err = 首个失败的 errno（ENOSPC = 28 即空间不足）。在游戏线程调，前端自己切主线程、不要阻塞
// Save not written (called by the host's hle_fs.c; the iOS front end provides a strong implementation of the same name that shows the native prompt 「存储空间不足，进度未保存」(Not enough storage. Progress was not saved); the default weak implementation only logs).
// Timing: when a save write fails (disk full etc.) within one of the game's save transactions (between two Commits), so that this Commit is refused and the previously committed save is unchanged, it is called once per refused transaction;
// the next transaction that succeeds commits as usual, and the front end need not do any recovery. err = the first failing errno (ENOSPC = 28 means out of space). Called on the game thread; the front end switches to the main thread itself and must not block
void front_save_failed(int err);

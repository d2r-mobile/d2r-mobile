// 界面文本框的触摸接法（金币数量框、编年史搜索框等，与起名同一条系统键盘通路）。地址为 3.3.94012 原映像偏移。
// 原版手柄流程：焦点在文本框上按确认 → 文本框基类（消息处理 main+0xac3688）把自己记到「当前输入框」[main+0x2f02540] 并开 TextInputModal
// （一层只留输入框不变暗的遮罩，main+0x20b8800）→ 遮罩等手柄再按一次确认（0xac8b4c）→ 调 main+0xac5bb8(输入框) 起键盘线程 → nn::swkbd::ShowKeyboard
// （hle_swkbd.c 已接成系统输入框）→ 完成回调 0xac923c 把文本放到 输入框+0x12a8 并清「当前输入框」→ 遮罩见它为空自己关。
// 触摸点文本框只走到「开遮罩」这一步，没人按第二次确认，于是画面变暗、点哪都没反应（金币数量框）。
// 这里补上那一下：看到 TextInputModal 打开，下一次取手柄时（主线程、不在消息投递栈里）直接调 0xac5bb8，并照 0xac8bd8 置 [界面根+0xb8] = 1。
// 数字框（NumericInputTextBoxWidget，虚表 main+0x20b91c0）用数字键盘：上限 [框+0x12e0]、下限 [框+0x12e4]，键盘返回后游戏自己解析并夹到范围内（0xace780）。
// Touch hookup for UI text boxes (gold quantity box, chronicle search box etc.; the same system keyboard path as character naming). Addresses are offsets in the 3.3.94012 original image.
// Original controller flow: press confirm with focus on a text box → the text box base class (message handler main+0xac3688) records itself as the "current input box" [main+0x2f02540] and opens TextInputModal
// (a modal layer that leaves only the input box undimmed, main+0x20b8800) → the modal waits for the controller to press confirm once more (0xac8b4c) → calls main+0xac5bb8(input box) to start the keyboard thread → nn::swkbd::ShowKeyboard
// (hle_swkbd.c already wires it to the system input box) → the completion callback 0xac923c puts the text at input box+0x12a8 and clears the "current input box" → the modal sees it empty and closes itself.
// Tapping a text box by touch only gets as far as the "open the modal" step; nobody presses confirm the second time, so the screen dims and tapping anywhere does nothing (gold quantity box).
// Here we supply that press: on seeing TextInputModal open, at the next controller poll (main thread, not inside the message delivery stack) call 0xac5bb8 directly and set [UI root+0xb8] = 1 as 0xac8bd8 does.
// Numeric boxes (NumericInputTextBoxWidget, vtable main+0x20b91c0) use the numeric keyboard: upper limit [box+0x12e0], lower limit [box+0x12e4]; after the keyboard returns the game parses and clamps to range itself (0xace780).
#include "hle.h"
#include "dm.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

extern int g_swkbd_numeric_max;   // 定义在 hle_swkbd.c。≥0 = 下一次系统输入框是数字框，值 = 上限（前端据此用数字键盘并当场拦）；hle_swkbd.c 用完置回 -1 | Defined in hle_swkbd.c. ≥0 = the next system input box is a numeric box, value = upper limit (the front end uses the numeric keyboard accordingly and blocks on the spot); hle_swkbd.c resets it to -1 after use
static _Atomic int g_ti_open, g_num_set = INT32_MIN;
static uint8_t *tb(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }

void dm_num_set(int n);
void dm_textinput_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息回调（diag.c 转来，主线程） | UI message callback (forwarded from diag.c, main thread)
    // 数量框上的「一半 / 全部」按钮（布局覆盖层）：按钮发 PanelManager:ClosePanel:TouchNumHalf / TouchNumAll 当信号
    // The 「一半 / 全部」(Half / All) buttons on the quantity box (layout overlay): the buttons send PanelManager:ClosePanel:TouchNumHalf / TouchNumAll as the signal
    if (!strcmp(cat, "PanelManager") && !strcmp(act, "ClosePanel") && !strncmp(arg, "TouchNum", 8)) { if (!strcmp(arg + 8, "Half")) dm_num_set(-2); else if (!strcmp(arg + 8, "All")) dm_num_set(-1); }
    if (!strcmp(cat, "PanelManager") && !strcmp(arg, "TextInputModal")) { if (!strcmp(act, "OpenPanel")) atomic_store(&g_ti_open, 1); else if (!strcmp(act, "ClosePanel") || !strcmp(act, "UnloadPanel")) atomic_store(&g_ti_open, 0); }
}
// 把当前打开的数量弹窗（DropGoldModal）的数字框设为 n；n = -1 全部（上限），-2 一半。任意线程调，主线程执行；没有数量弹窗时什么都不做
// Set the numeric box of the currently open quantity modal (DropGoldModal) to n; n = -1 all (upper limit), -2 half. Called from any thread, executed on the main thread; does nothing when there is no quantity modal
void dm_num_set(int n) { atomic_store(&g_num_set, n); }
static uint64_t ti_find(uint64_t w, const char *name, int dep) {   // 按名字找控件：名字 +8、子控件数组 +0x58、个数 +0x60（同 dm_cursor.c ui_find） | Find a widget by name: name +8, child array +0x58, count +0x60 (same as dm_cursor.c ui_find)
    if (!w || dep > 14) return 0; const char *nm = *(const char **)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = ti_find(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
void dm_textinput_pump(void) {   // 取手柄处每帧调 | Called every frame at the controller poll
    uint8_t *b = tb(); if (!b) return; uint64_t root = *(uint64_t *)(b + 0x2f05458); if (!root) return;
    if (atomic_exchange(&g_ti_open, 0)) {
        uint64_t w = *(uint64_t *)(b + 0x2f02540);
        if (w) {   // 原版遮罩在 [界面根+0xb8] 为 0 时才弹；触摸点进来时它已是 1（实测），不看它 | The original game's modal only pops up when [UI root+0xb8] is 0; when a touch comes in it is already 1 (measured), so it is not checked
            int num = *(uint64_t *)w - (uint64_t)(uintptr_t)b == 0x20b91c0; g_swkbd_numeric_max = num ? *(int32_t *)(w + 0x12e0) : -1;
            LOG("文本框：TextInputModal 打开 → 弹系统键盘（%s%s%d）", num ? "数字框" : "文字框", num ? "，上限 " : "，类型 ", num ? g_swkbd_numeric_max : *(int32_t *)(w + 0x58c));
            ((void (*)(uint64_t))(b + 0xac5bb8))(w); *(uint8_t *)(root + 0xb8) = 1;
        }
    }
    int n = atomic_exchange(&g_num_set, INT32_MIN);
    if (n != INT32_MIN) {
        uint64_t modal = ti_find(root, "DropGoldModal", 0), w = modal && *(uint8_t *)(modal + 0x50) ? *(uint64_t *)(modal + 0x210) : 0;
        if (w && *(uint64_t *)w - (uint64_t)(uintptr_t)b == 0x20b91c0) {
            int max = *(int32_t *)(w + 0x12e0), min = *(int32_t *)(w + 0x12e4), v = n == -1 ? max : n == -2 ? max / 2 : n; if (v > max) v = max; if (v < min) v = min;
            char t[16]; struct { const char *p; uint64_t n; } sv = { t, (uint64_t)snprintf(t, sizeof t, "%d", v) };
            ((void (*)(uint64_t, void *))(b + 0xac2058))(w, &sv);   // 设文本（主线程应用键盘结果用的同一个函数，0xac29a4） | Set text (the same function the main thread uses to apply the keyboard result, 0xac29a4)
            ((void (*)(uint64_t))(b + 0xace780))(w);                // 数字框把文本解析成数值、夹范围、回写（消息处理尾部 0xace270 之后调的同一个） | The numeric box parses the text into a value, clamps it to range and writes it back (the same one called after 0xace270 at the tail of the message handler)
            LOG("数量框：设为 %d（范围 %d–%d）→ 控件当前值 %d", v, min, max, *(int32_t *)(w + 0x12fc));
        } else LOG("数量框：没有打开的数量弹窗，忽略设值 %d", n);
    }
}

// 离线角色名放行中文（中文角色名需要）。不改游戏代码页（iOS 上签名后的代码页改不了），只做两件：
//   ① 接管 isalpha：游戏的角色名规则是逐字节判 ASCII——建角整串校验 main+0x7fed78、进游戏前复查 main+0x7fe8a8、载入角色复查 main+0x807fd8
//      以及服务端一侧载入 / 校验存档的两个函数 main+0x6f0c70、main+0x6fe640，都对每个字节调 isalpha（另允许 '-' '_' 合计一个，首尾必须「字母」，长度 2–15 字节）。只对从这几个函数里发起的调用把 ≥0x80 的字节当字母，
//      其余调用点保持 SDK 原实现的语义（原实现 sdk+0x545840：(c | 0x20) − 'a' < 26）。
//   ② 起名文本框登记的键入过滤 main+0xe09190 只放行英文字母和 - _，软键盘返回的串会按字节过它（main+0x1b668b8 起：请求里的过滤器函数对象
//      [main+0x3213dd8] 非空就逐字节调）。起名面板开着时由 hle_swkbd.c 在键盘返回前把它置空，过滤改由前端输入框按同一规则 + 汉字来做。
// 存档里角色名字段本来就是 UTF-8（参考存档「法师」），文件名直接用名字。长度上限按字节算：汉字每个 3 字节，最多 5 个。联机时名字归服务器判，不在此列。
// 地址均为 3.3.94012 原映像偏移；函数边界取自 eh_frame（0x7fe8a8–0x7feeec 两个函数相邻，0x807fd8–0x809d60）。
// Allow Chinese in offline character names (needed for Chinese character names). Game code pages are not modified (signed code pages cannot be changed on iOS); only two things are done:
//   ① Take over isalpha: the game's character name rule checks ASCII byte by byte -- whole-string validation at character creation main+0x7fed78, re-check before entering the game main+0x7fe8a8, re-check on loading a character main+0x807fd8,
//      and the two server-side functions that load / validate saves main+0x6f0c70, main+0x6fe640, all call isalpha on every byte (additionally allowing one '-' or '_' in total, first and last must be a "letter", length 2–15 bytes). Only for calls made from within these functions are bytes ≥0x80 treated as letters;
//      all other call sites keep the semantics of the SDK's original implementation (original implementation sdk+0x545840: (c | 0x20) − 'a' < 26).
//   ② The typing filter main+0xe09190 registered by the naming text box only lets through English letters and - _, and the string returned by the software keyboard is passed through it byte by byte (from main+0x1b668b8: if the filter function object in the request
//      [main+0x3213dd8] is non-empty it is called per byte). While the naming panel is open, hle_swkbd.c clears it before the keyboard returns, and filtering is done instead by the front-end input box with the same rule + Chinese characters.
// The character name field in the save is UTF-8 to begin with (reference save 「法师」(Sorceress)), and the file name uses the name directly. The length limit is counted in bytes: 3 bytes per Chinese character, at most 5. Online, names are judged by the server and are out of scope here.
// Addresses are all offsets in the original 3.3.94012 image; function boundaries are taken from eh_frame (0x7fe8a8–0x7feeec are two adjacent functions, 0x807fd8–0x809d60).
#include "hle.h"
#include "dm.h"

static uintptr_t main_base(void) {
    static uintptr_t b;
    if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uintptr_t)g_mod[i].base;
    return b;
}

HLE_FORCE("isalpha", "离线角色名放行非 ASCII：只对角色名三处校验里的调用把 ≥0x80 的字节当字母，其余同 SDK 原实现", int, h_isalpha, int c) {
    if ((unsigned)((c | 0x20) - 'a') < 26) return 1;
    if (c >= 0x80 || c < 0) {
        uintptr_t off = (uintptr_t)__builtin_return_address(0) - main_base();
        if ((off >= 0x7fe8a8 && off < 0x7feeec) || (off >= 0x807fd8 && off < 0x809d60) ||
            (off >= 0x6f0c70 && off < 0x6f1220) || (off >= 0x6fe640 && off < 0x702e40)) return 1;   // 后两段是局内服务端一侧对存档里角色名的校验（进局时不过就报致命错误退回前端；按调用点实测补上） | The last two ranges are the in-run server-side validation of the character name in the save (failing it on joining a run reports a fatal error and returns to the front end; added from measured call sites)
    }
    return 0;
}

// 键盘请求里的过滤器（main+0x3213dd8 起）是一个带标记位的函数对象：值为 1 = 空（不过滤）；否则是内联 / 堆上的可调用对象，
// 内联时紧随其后的一个字就是被包装的函数指针。只在它包装的正是起名过滤函数 main+0xe09190 时把它置空；返回 1 = 已置空
// The filter in the keyboard request (from main+0x3213dd8) is a function object with a tag bit: value 1 = empty (no filtering); otherwise it is an inline / heap callable object,
// and when inline the word right after it is the wrapped function pointer. Clear it only when what it wraps is exactly the naming filter function main+0xe09190; returns 1 = cleared
int dm_charname_unfilter(void) {
    uintptr_t b = main_base(); if (!b) return 0;
    uint64_t *fn = (uint64_t *)(b + 0x3213dd8);
    if (fn[0] == 1 || fn[1] != b + 0xe09190) return 0;
    fn[0] = 1; return 1;
}

__attribute__((constructor)) static void charname_register(void) { extern int (*g_swkbd_unfilter)(void); g_swkbd_unfilter = dm_charname_unfilter; }

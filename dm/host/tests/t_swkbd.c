// swkbd 单测（秒级）：按 SDK 布局手工摆一个 ShowKeyboardArg，经 swkbd_show 回写 String，期望字节全部手算。
// 编译运行：cd dm/host/tests && clang -O1 -I.. -o /tmp/t_swkbd t_swkbd.c ../hle_swkbd.c && /tmp/t_swkbd
// swkbd unit test (takes seconds): lay out a ShowKeyboardArg by hand following the SDK layout, have swkbd_show write back the String, and all expected bytes are computed by hand.
// Build and run: cd dm/host/tests && clang -O1 -I.. -o /tmp/t_swkbd t_swkbd.c ../hle_swkbd.c && /tmp/t_swkbd
#include "../hle.h"
#include <stdio.h>

Result swkbd_show(void *out, const void *arg, int (*input)(const char *, const char *, const char *, const char *, int, char *, int));
void dm_ui_text_entered(const char *text) { (void)text; }   // 真实现在 dm_cursor.c（起名页输完自动接受） | the real implementation is in dm_cursor.c (the naming page auto-accepts once input is finished)
int front_text_input(const char *t, const char *s, const char *g, const char *i, int m, char *o, int c) { (void)t; (void)s; (void)g; (void)i; (void)m; (void)o; (void)c; return -1; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// 输入桩：记下收到的参数，按设定返回
// Input stub: record the arguments received and return as configured
static const char *g_reply; static int g_ret, g_called; static char g_title[256], g_init[256]; static int g_max;
static int stub(const char *t, const char *s, const char *g, const char *i, int m, char *o, int c) {
    (void)s; (void)g; g_called++; snprintf(g_title, sizeof g_title, "%s", t); snprintf(g_init, sizeof g_init, "%s", i); g_max = m;
    if (g_ret > 0) snprintf(o, c, "%s", g_reply);
    return g_ret;
}

static uint8_t arg[0x500], work[0x1000], sbuf[0x7d4];
static struct { uint8_t *p; uint64_t n; } str = {sbuf, sizeof sbuf};
static void mkarg(int utf8, int maxlen, const uint16_t *init, int ilen) {
    memset(arg, 0, sizeof arg); memset(work, 0, sizeof work); memset(sbuf, 0xAA, sizeof sbuf);
    static const uint16_t title[] = {0x8D77, 0x540D, 0};                 // 「起名」 | 「起名」(Name your character)
    memcpy(arg + 0x24, title, sizeof title);
    memcpy(arg + 0x3ac, &maxlen, 4); arg[0x3bd] = (uint8_t)utf8;
    int off = 0x14; memcpy(arg + 0x3c0, &off, 4); memcpy(arg + 0x3c4, &ilen, 4);   // SetInitialTextUtf8 的写法 | the way SetInitialTextUtf8 writes it
    memcpy(work + off, init, (size_t)ilen * 2);
    uint8_t *w = work; memcpy(arg + 0x4d0, &w, 8);
    g_called = 0;
}
static int bytes_eq(const uint8_t *want, size_t n) {   // 前 n 字节等于期望，其后到缓冲末尾全 0 | the first n bytes equal the expectation, and everything after to the end of the buffer is 0
    if (memcmp(sbuf, want, n)) return 0;
    for (size_t i = n; i < sizeof sbuf; i++) if (sbuf[i]) return 0;
    return 1;
}

void guest_clock_freeze_for(int reason, int on) { (void)reason; (void)on; }   // 真实现在 hle_os.c；单测不关心时钟 | the real implementation is in hle_os.c; the unit test does not care about the clock
int main(void) {
    unsetenv("DM_KBD");
    static const uint16_t hero[] = {'H', 'e', 'r', 'o'};

    printf("1 确定、UTF-8 输出（游戏实际用法）\n");
    mkarg(1, 16, hero, 4); g_ret = 1; g_reply = "Ünï名";
    Result r = swkbd_show(&str, arg, stub);
    static const uint8_t w1[] = {0xC3, 0x9C, 0x6E, 0xC3, 0xAF, 0xE5, 0x90, 0x8D, 0x00};   // Ü n ï 名 + 结尾 0 | U+00DC n U+00EF U+540D + terminating 0
    CHECK(r == 0, "Result %#x 期望 0", r);
    CHECK(bytes_eq(w1, sizeof w1), "UTF-8 回写字节不符");
    CHECK(!strcmp(g_title, "\xE8\xB5\xB7\xE5\x90\x8D"), "标题解出「%s」期望「起名」", g_title);
    CHECK(!strcmp(g_init, "Hero"), "初始文本解出「%s」期望 Hero", g_init);
    CHECK(g_max == 16, "上限 %d 期望 16", g_max);

    printf("2 取消 → 0x29F，缓冲不动\n");
    mkarg(1, 16, hero, 4); g_ret = 0;
    r = swkbd_show(&str, arg, stub);
    CHECK(r == 0x29F, "Result %#x 期望 0x29F", r);
    CHECK(sbuf[0] == 0xAA && sbuf[sizeof sbuf - 1] == 0xAA, "取消时改了缓冲");

    printf("3 无窗口（输入返回 -1）→ 按取消 0x29F\n");
    mkarg(1, 16, hero, 4); g_ret = -1;
    CHECK(swkbd_show(&str, arg, stub) == 0x29F, "无窗口未按取消返回");

    printf("4 UTF-16 输出（isUseUtf8 = 0）\n");
    mkarg(0, 16, hero, 0); g_ret = 1; g_reply = "A名";
    r = swkbd_show(&str, arg, stub);
    static const uint8_t w4[] = {0x41, 0x00, 0x0D, 0x54, 0x00, 0x00};   // 'A' U+540D 结尾 0（小端） | 'A' U+540D terminating 0 (little endian)
    CHECK(r == 0 && bytes_eq(w4, sizeof w4), "UTF-16 回写字节不符");
    CHECK(g_init[0] == 0, "初始长度 0 时应无初始文本");

    printf("5 最大长度截断（3 字）\n");
    mkarg(1, 3, hero, 4); g_ret = 1; g_reply = "ABCDE";
    swkbd_show(&str, arg, stub);
    static const uint8_t w5[] = {'A', 'B', 'C', 0};
    CHECK(bytes_eq(w5, sizeof w5), "截断结果不符");

    printf("6 截断不拆代理对（上限 2 单元，a + U+1F600 需 3 单元）\n");
    mkarg(1, 2, hero, 4); g_ret = 1; g_reply = "a\xF0\x9F\x98\x80";
    swkbd_show(&str, arg, stub);
    static const uint8_t w6[] = {'a', 0};
    CHECK(bytes_eq(w6, sizeof w6), "代理对被拆或未截断");

    printf("7 DM_KBD 直给文本：不调输入框\n");
    setenv("DM_KBD", "Kbd", 1);
    mkarg(1, 16, hero, 4); g_ret = 0;
    r = swkbd_show(&str, arg, stub);
    static const uint8_t w7[] = {'K', 'b', 'd', 0};
    CHECK(r == 0 && bytes_eq(w7, sizeof w7) && g_called == 0, "DM_KBD 路径不符（调用输入框 %d 次）", g_called);
    unsetenv("DM_KBD");

    printf("%s：%d/%d\n", fails ? "红" : "全过", checks - fails, checks);
    return fails != 0;
}

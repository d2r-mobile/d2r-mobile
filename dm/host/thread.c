// 客体线程环境：门槽 = 宿主门入口；x18 槽清零；tpidr_el0 槽 = ELF TLS 块；tpidrro_el0 槽 = Horizon ThreadLocalRegion（0x200）。
// SDK 的内部临界区从 TLR[0x1f8] 取当前线程对象指针，这里给每线程分配一个零初始化对象并写入。
// Guest thread environment: gate slot = host gate entry; x18 slot cleared; tpidr_el0 slot = ELF TLS block; tpidrro_el0 slot = Horizon ThreadLocalRegion (0x200).
// The SDK's internal critical section takes the current thread object pointer from TLR[0x1f8]; here each thread gets a zero-initialized object allocated and written in.
#include "dm.h"
#define TLS_SIZE 8192
#define TLR_SIZE 0x200
#define THREAD_OBJ_SIZE 0x400
extern void dm_gate(void);            // entry.S：门公共入口 | entry.S: common gate entry
#include "front.h"
#include "dm_uiscreen.h"
void guest_thread_init(void) {
    pthread_setspecific(g_keygate, (void *)dm_gate);
    for (int i = 0; i < 2; i++) if (g_keyres[i]) pthread_setspecific(g_keyres[i], (void *)dm_game_res(i));   // 分辨率参数位：{u32 宽, u32 高} 打包 | resolution parameter slot: {u32 width, u32 height} packed
    if (g_keyaspect || g_keyaspectui) {   // 宽高比上限参数位：max(19/9, 两种模式输出宽 / 高)，float 位模式放低 32 位 | aspect ratio limit parameter slot: max(19/9, output width / height of the two modes), float bit pattern in the low 32 bits
        float a = 19.0f / 9.0f; int wide = 0;
        for (int i = 0; i < 2; i++) { uint64_t r = front_game_res(i); float w = (float)(uint32_t)r, h = (float)(uint32_t)(r >> 32); if (h > 0 && w / h > a) { a = w / h; wide = 1; } }
        // 界面边柱着色器的上限：屏幕宽于 19:9 时另加 0.15，把边柱向内的柔边推出屏幕；16:9 不变。
        // 推导：边柱内缘 x = 屏高 × 上限 / 2，上限每加 Δ，内缘每侧外移 屏高 × Δ / 2。Mac 1952x900 实测柔边宽度 ≤ 30 像素
        // （上限 = 屏幕比例时右缘第 0–20 列由黑渐亮，上限推到 2.4 即消失）⇒ 需 Δ ≥ 2 × 30 / 900 ≈ 0.067；取 0.15（每侧外移约 67 像素）留出一倍以上余量。
        // 不是魔数：屏高变大时余量按比例不变（Δ 是相对屏高的量）
        // Limit of the UI side-pillar shader: when the screen is wider than 19:9 add another 0.15, pushing the pillars' inward soft edge off the screen; 16:9 is unchanged.
        // Derivation: pillar inner edge x = screen height * limit / 2, so for every delta added to the limit the inner edge moves outward by screen height * delta / 2 on each side. Measured on Mac 1952x900, the soft edge is <= 30 pixels wide
        // (with limit = screen aspect ratio, columns 0–20 at the right edge fade from black to bright, and it disappears once the limit is pushed to 2.4) => need delta >= 2 * 30 / 900 ~ 0.067; 0.15 is taken (about 67 pixels outward per side), leaving more than double the margin.
        // Not a magic number: when the screen height grows the margin stays proportional (delta is relative to screen height)
        float ui = wide ? a + 0.15f : a;
        uint32_t bits;
        if (g_keyaspect) { memcpy(&bits, &a, 4); pthread_setspecific(g_keyaspect, (void *)(uintptr_t)bits); }
        if (g_keyaspectui) { memcpy(&bits, &ui, 4); pthread_setspecific(g_keyaspectui, (void *)(uintptr_t)bits); }
    }
    pthread_setspecific(g_key18, 0);
    pthread_setspecific(g_keytls, calloc(1, TLS_SIZE));
    uint8_t *tlr = calloc(1, TLR_SIZE);
    *(void **)(tlr + 0x1f8) = calloc(1, THREAD_OBJ_SIZE);   // 当前线程对象 | current thread object
    pthread_setspecific(g_keytlr, tlr);
}

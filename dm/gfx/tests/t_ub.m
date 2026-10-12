#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w, g, w==g?"OK":"FAIL"); if(w!=g) fail++; } while(0)
int g_t_ub_var = 7, g_t_ub_var2 = -3;   // 被 @set 按符号名找到的全局变量 | global variables found by @set via symbol name
// 同进程交替测量（DM_NVN_UB）的相位逻辑单测：不起渲染，只按帧号推进。
// 配置：两类，每相 60 帧。相序 = 基准, a, 基准, b, 基准, a, …；a = 设 g_t_ub_var=5 且丢键 abcd…；b = 设 g_t_ub_var2=9 且丢全部（*）。
// Unit tests for the phase logic of same-process alternating measurement (DM_NVN_UB): no rendering is started, it only advances by frame number.
// Configuration: two classes, 60 frames per phase. Phase order = baseline, a, baseline, b, baseline, a, ...; a = set g_t_ub_var=5 and drop keys abcd...; b = set g_t_ub_var2=9 and drop everything (*).
int main(void) {
    setenv("DM_NVN_UB", "a=@set:g_t_ub_var=5,abcd;b=@set:g_t_ub_var2=9,*;c=@set:g_no_such_var=1", 1); setenv("DM_NVN_UB_N", "60", 1);
    struct { int f, v1, v2, skip_abcd, skip_other; const char *what; } T[] = {
        {0, 7, -3, 0, 0, "相 0 基准"}, {59, 7, -3, 0, 0, "相 0 末帧"},
        {60, 5, -3, 1, 0, "相 1 = a：变量 1 被设、丢 abcd"}, {119, 5, -3, 1, 0, "相 1 末帧"},
        {120, 7, -3, 0, 0, "相 2 基准：变量 1 还原"},
        {180, 7, 9, 1, 1, "相 3 = b：变量 2 被设、全丢"},
        {240, 7, -3, 0, 0, "相 4 基准：变量 2 还原"},
        {300, 7, -3, 0, 0, "相 5 = c：变量不存在，什么都不改"},
        {360, 7, -3, 0, 0, "相 6 基准"},
        {420, 5, -3, 1, 0, "相 7 = a（第二圈）"},
    };
    int ti = 0, n = (int)(sizeof T / sizeof T[0]);
    for (int f = 0; f <= 420 && ti < n; f++) { mtl_set_frame((uint64_t)f);
        if (f != T[ti].f) continue;
        char nm[96]; snprintf(nm, sizeof nm, "%s 变量1", T[ti].what); EXPECT(nm, T[ti].v1, g_t_ub_var);
        snprintf(nm, sizeof nm, "%s 变量2", T[ti].what); EXPECT(nm, T[ti].v2, g_t_ub_var2);
        snprintf(nm, sizeof nm, "%s 丢 abcd0123", T[ti].what); EXPECT(nm, T[ti].skip_abcd, ub_skip("abcd0123ffff0000"));
        snprintf(nm, sizeof nm, "%s 丢别的键", T[ti].what); EXPECT(nm, T[ti].skip_other, ub_skip("9999888877776666"));
        ti++; }
    EXPECT("走完的检查点数", n, ti);
    // 上屏间隔按相统计：相内第 45 帧起才计；三档界线 20.8 / 29.2 ms；−1 记丢弃
    // On-screen present intervals are tallied per phase: counted only from frame 45 of the phase on; boundaries of the three bands are 20.8 / 29.2 ms; −1 is recorded as discarded
    for (int f = 480; f < 480 + 45; f++) { mtl_set_frame((uint64_t)f); mtl_ub_present(16.7); }   // 过渡段：不计 | transition segment: not counted
    EXPECT("过渡段不计", 0, atomic_load(&g_ubp_c[0]));
    mtl_set_frame(480 + 45); mtl_ub_present(16.7); mtl_ub_present(25.0); mtl_ub_present(25.0); mtl_ub_present(33.3); mtl_ub_present(-1); mtl_ub_present(0);
    EXPECT("16.7 ms 档", 1, atomic_load(&g_ubp_c[0])); EXPECT("25 ms 档", 2, atomic_load(&g_ubp_c[1])); EXPECT("≥33 ms 档", 1, atomic_load(&g_ubp_c[2])); EXPECT("丢弃", 1, atomic_load(&g_ubp_c[3]));
    EXPECT("间隔和（微秒）", 16700 + 25000 + 25000 + 33300, (long long)atomic_load(&g_ubp_sum_us));
    printf("t_ub：%d 项，失败 %d\n", count, fail); return fail != 0;
}

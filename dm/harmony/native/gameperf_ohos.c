// Game Service Kit 游戏性能（GamePerformance）：系统给的温控档 / 热余量 / 温度趋势 / 电流，以及 GPU 各级负载（顶点 / 片元 / 纹理 / 带宽）与主频、
// CPU 负载。后台线程每 2 秒查一次，每 10 秒写一行 perf 文件（行首 GP）并进日志；dm_thermal_now() 改用系统温控档（perf 汇总行的「温控」列）。
// 运行时 dlopen：设备 / 系统不支持时整段跳过，不影响启动。DM_GAMEPERF=0 关。
// Game Service Kit game performance: the system's thermal level / thermal margin / temperature trend / current, plus GPU load per stage
// (vertex / fragment / texture / bandwidth) with the GPU clock, and CPU load. A background thread queries every 2 s, writes one perf-file line
// (prefix GP) every 10 s and logs it; dm_thermal_now() now returns the system thermal level (the 「温控」 column of the perf summary).
// Loaded at run time with dlopen: skipped entirely when the device / system lacks it, never blocking startup. DM_GAMEPERF=0 disables.
#include <GameServiceKit/game_performance.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../host/dm.h"

typedef GamePerformance_ErrorCode E;
static struct {
    E (*create_init)(GamePerformance_InitParameters **); E (*destroy_init)(GamePerformance_InitParameters **);
    E (*set_bundle)(GamePerformance_InitParameters *, const char *); E (*set_version)(GamePerformance_InitParameters *, const char *);
    E (*init)(GamePerformance_InitParameters *);
    E (*create_tq)(GamePerformance_ThermalInfoQueryParameters **); E (*destroy_tq)(GamePerformance_ThermalInfoQueryParameters **);
    E (*query_thermal)(GamePerformance_ThermalInfoQueryParameters *, GamePerformance_ThermalInfo **); E (*destroy_thermal)(GamePerformance_ThermalInfo **);
    E (*t_level)(GamePerformance_ThermalInfo *, int32_t *); E (*t_margin)(GamePerformance_ThermalInfo *, int32_t *); E (*t_trend)(GamePerformance_ThermalInfo *, int32_t *);
    E (*t_now_cur)(GamePerformance_ThermalInfo *, int32_t *); E (*t_rec_cur)(GamePerformance_ThermalInfo *, int32_t *);
    E (*query_gpu)(GamePerformance_GpuInfo **); E (*destroy_gpu)(GamePerformance_GpuInfo **);
    E (*g_load)(GamePerformance_GpuInfo *, int32_t *); E (*g_vertex)(GamePerformance_GpuInfo *, int32_t *); E (*g_frag)(GamePerformance_GpuInfo *, int32_t *);
    E (*g_tex)(GamePerformance_GpuInfo *, int32_t *); E (*g_bw)(GamePerformance_GpuInfo *, int32_t *); E (*g_freq)(GamePerformance_GpuInfo *, int32_t *);
    E (*query_cpu)(GamePerformance_CpuInfo **); E (*destroy_cpu)(GamePerformance_CpuInfo **);
    E (*c_load)(GamePerformance_CpuInfo *, int32_t *); E (*c_single)(GamePerformance_CpuInfo *, int32_t *);
} g;
// 最近读数（−1 = 无） | Latest readings (−1 = none)
static _Atomic int g_level = -1; static GamePerformance_ThermalInfoQueryParameters *g_tq;
static int g_margin = -1, g_trend = 0, g_now_cur = -1, g_rec_cur = -1, g_gload = -1, g_gvert = -1, g_gfrag = -1, g_gtex = -1, g_gbw = -1, g_gfreq = -1, g_cload = -1, g_csingle = -1;

int dm_thermal_now(void) { return atomic_load(&g_level); }
int dm_gp_thermal_margin(void) { return g_margin; }   // 动态 3D 比例等可按热余量预先收（暂只读） | e.g. the dynamic 3D scale may back off early by thermal margin (read-only for now)

#define SYM(f, n) (*(void **)&g.f = dlsym(lib, n))
static void query(void) {
    if (g.query_thermal) { GamePerformance_ThermalInfo *t = NULL;
        if (g.query_thermal(g_tq, &t) == GAME_PERFORMANCE_SUCCESS && t) {
            int32_t v;
            if (g.t_level && g.t_level(t, &v) == GAME_PERFORMANCE_SUCCESS) atomic_store(&g_level, v);
            if (g.t_margin && g.t_margin(t, &v) == GAME_PERFORMANCE_SUCCESS) g_margin = v;
            if (g.t_trend && g.t_trend(t, &v) == GAME_PERFORMANCE_SUCCESS) g_trend = v;
            if (g.t_now_cur && g.t_now_cur(t, &v) == GAME_PERFORMANCE_SUCCESS) g_now_cur = v;
            if (g.t_rec_cur && g.t_rec_cur(t, &v) == GAME_PERFORMANCE_SUCCESS) g_rec_cur = v;
            if (g.destroy_thermal) g.destroy_thermal(&t); } }
    if (g.query_gpu) { GamePerformance_GpuInfo *p = NULL;
        if (g.query_gpu(&p) == GAME_PERFORMANCE_SUCCESS && p) {
            if (g.g_load) g.g_load(p, &g_gload); if (g.g_vertex) g.g_vertex(p, &g_gvert); if (g.g_frag) g.g_frag(p, &g_gfrag);
            if (g.g_tex) g.g_tex(p, &g_gtex); if (g.g_bw) g.g_bw(p, &g_gbw); if (g.g_freq) g.g_freq(p, &g_gfreq);
            if (g.destroy_gpu) g.destroy_gpu(&p); } }
    if (g.query_cpu) { GamePerformance_CpuInfo *c = NULL;
        if (g.query_cpu(&c) == GAME_PERFORMANCE_SUCCESS && c) {
            if (g.c_load) g.c_load(c, &g_cload); if (g.c_single) g.c_single(c, &g_csingle);
            if (g.destroy_cpu) g.destroy_cpu(&c); } }
}
static void *run(void *a) {
    (void)a; pthread_setname_np(pthread_self(), "dm-gameperf");
    void *lib = dlopen("libgame_performance.z.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) { LOG("GamePerformance：库加载不到（%s），跳过 | library not loadable, skipped", dlerror()); return NULL; }
    SYM(create_init, "HMS_GamePerformance_CreateInitParameters"); SYM(destroy_init, "HMS_GamePerformance_DestroyInitParameters");
    SYM(set_bundle, "HMS_GamePerformance_InitParameters_SetBundleName"); SYM(set_version, "HMS_GamePerformance_InitParameters_SetAppVersion");
    SYM(init, "HMS_GamePerformance_Init");
    SYM(create_tq, "HMS_GamePerformance_CreateThermalInfoQueryParameters"); SYM(destroy_tq, "HMS_GamePerformance_DestroyThermalInfoQueryParameters");
    SYM(query_thermal, "HMS_GamePerformance_QueryThermalInfo"); SYM(destroy_thermal, "HMS_GamePerformance_DestroyThermalInfo");
    SYM(t_level, "HMS_GamePerformance_ThermalInfo_GetThermalLevel"); SYM(t_margin, "HMS_GamePerformance_ThermalInfo_GetThermalMargin");
    SYM(t_trend, "HMS_GamePerformance_ThermalInfo_GetThermalTrend"); SYM(t_now_cur, "HMS_GamePerformance_ThermalInfo_GetNowNormalizedCurrent");
    SYM(t_rec_cur, "HMS_GamePerformance_ThermalInfo_GetRecommendNormalizedCurrent");
    SYM(query_gpu, "HMS_GamePerformance_QueryGpuInfo"); SYM(destroy_gpu, "HMS_GamePerformance_DestroyGpuInfo");
    SYM(g_load, "HMS_GamePerformance_GpuInfo_GetGpuLoadLevel"); SYM(g_vertex, "HMS_GamePerformance_GpuInfo_GetVertexLoadLevel");
    SYM(g_frag, "HMS_GamePerformance_GpuInfo_GetFragmentLoadLevel"); SYM(g_tex, "HMS_GamePerformance_GpuInfo_GetTextureLoadLevel");
    SYM(g_bw, "HMS_GamePerformance_GpuInfo_GetBandwidthLoadLevel"); SYM(g_freq, "HMS_GamePerformance_GpuInfo_GetCurrentFrequency");
    SYM(query_cpu, "HMS_GamePerformance_QueryCpuInfo"); SYM(destroy_cpu, "HMS_GamePerformance_DestroyCpuInfo");
    SYM(c_load, "HMS_GamePerformance_CpuInfo_GetCpuLoadLevel"); SYM(c_single, "HMS_GamePerformance_CpuInfo_GetSingleThreadLoadLevel");
    if (!g.create_init || !g.set_bundle || !g.init) { LOG("GamePerformance：接口不全，跳过 | incomplete API, skipped"); return NULL; }
    GamePerformance_InitParameters *ip = NULL; E r = g.create_init(&ip);
    if (r == GAME_PERFORMANCE_SUCCESS) {
        const char *bn = getenv("DM_BUNDLE_NAME"), *ver = getenv("DM_APP_VERSION");
        g.set_bundle(ip, bn && *bn ? bn : "com.xiao.boluo"); if (g.set_version) g.set_version(ip, ver && *ver ? ver : "0");
        r = g.init(ip); if (g.destroy_init) g.destroy_init(&ip); }
    LOG("GamePerformance：初始化结果 %d | init result %d", (int)r, (int)r);
    if (r != GAME_PERFORMANCE_SUCCESS) return NULL;
    if (g.create_tq) g.create_tq(&g_tq);   // 不要预测：只读当前 | no prediction: current readings only
    const char *e = getenv("DM_PERF_FILE"); const char *path = e ? (strcmp(e, "0") ? e : NULL) : "perf.txt";
    for (unsigned tick = 0;; tick++) {
        query();
        if (tick % 5 == 0) {
            LOG("[gp] 温控档 %d 热余量 %d 趋势 %d 电流 %d/建议 %d | GPU 负载 %d 顶点 %d 片元 %d 纹理 %d 带宽 %d 频率 %d | CPU 负载 %d 单线程 %d",
                atomic_load(&g_level), g_margin, g_trend, g_now_cur, g_rec_cur, g_gload, g_gvert, g_gfrag, g_gtex, g_gbw, g_gfreq, g_cload, g_csingle);
            if (path) { FILE *fp = fopen(path, "a");
                if (fp) { time_t w = time(NULL); struct tm tmv; localtime_r(&w, &tmv); char ts[16]; strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
                    fprintf(fp, "GP\t%s\tlevel=%d margin=%d trend=%d cur=%d rec=%d gpu=%d vert=%d frag=%d tex=%d bw=%d freq=%d cpu=%d single=%d\n", ts,
                            atomic_load(&g_level), g_margin, g_trend, g_now_cur, g_rec_cur, g_gload, g_gvert, g_gfrag, g_gtex, g_gbw, g_gfreq, g_cload, g_csingle);
                    fclose(fp); } } }
        sleep(2);
    }
    return NULL;
}
void dm_gameperf_start(void) {
    static int started; if (started) return; started = 1;
    const char *e = getenv("DM_GAMEPERF"); if (e && !strcmp(e, "0")) return;
    pthread_t t; if (!pthread_create(&t, NULL, run, NULL)) pthread_detach(t);
}

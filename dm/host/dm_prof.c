// 宿主每帧模块的指令数读数，说明见 dm_prof.h
// Per-frame instruction counts of host modules; see dm_prof.h
#include "dm_prof.h"
#include "hle.h"
#include <stdlib.h>
#include <string.h>
extern int thread_selfcounts(int type, void *buf, size_t nbytes);
int dm_ipf_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("DM_PUMP_PROF"); on = e && strcmp(e, "0"); } return on; }
uint64_t dm_ipf_instr(void) { uint64_t c[2] = { 0, 0 }; thread_selfcounts(1, c, sizeof c); return c[0]; }
enum { NMOD = 48 };
static struct { const char *name[NMOD]; uint64_t sum[NMOD], n_frames; int n; } g_t[2];
void dm_ipf_add(int t, const char *name, uint64_t instr) {
    int i = 0; for (; i < g_t[t].n && g_t[t].name[i] != name; i++) {}
    if (i == g_t[t].n) { if (i >= NMOD) return; g_t[t].name[i] = name; g_t[t].n++; }
    g_t[t].sum[i] += instr;
}
void dm_ipf_frame(int t) {
    if (!dm_ipf_on() || ++g_t[t].n_frames % 600) return;
    int ord[NMOD], n = g_t[t].n; uint64_t tot = 0; for (int i = 0; i < n; i++) { ord[i] = i; tot += g_t[t].sum[i]; }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (g_t[t].sum[ord[j]] > g_t[t].sum[ord[i]]) { int x = ord[i]; ord[i] = ord[j]; ord[j] = x; }
    char ln[1600]; int o = 0; ln[0] = 0;
    for (int k = 0; k < n && o < (int)sizeof ln - 80; k++) { int i = ord[k]; o += snprintf(ln + o, sizeof ln - o, " %s %.1fk", g_t[t].name[i], g_t[t].sum[i] / 600.0 / 1000.0); }
    LOG("[每帧指令] %s 近 600 %s 合计 %.1fk 条/%s：%s", t ? "服务端拍" : "主线程取手柄处", t ? "拍" : "帧", tot / 600.0 / 1000.0, t ? "拍" : "帧", ln);
    memset(g_t[t].sum, 0, sizeof g_t[t].sum);
}
#include "dm_widget.h"
int dm_ui_top_chk_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("DM_TEST_FINDCHK"); on = e && strcmp(e, "0"); } return on; }
static uint64_t full_find(uint64_t w, const char *name, int dep) {
    if (!w || dep > 14) return 0; const char *nm = *(const char **)(uintptr_t)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(uintptr_t)(w + 0x58), n = *(uint64_t *)(uintptr_t)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = full_find(*(uint64_t *)(uintptr_t)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
void dm_ui_top_chk(uint64_t root, const char *name, uint64_t got) {
    static uint64_t n_ok; uint64_t want = full_find(root, name, 1);
    if (want != got) { static int k; if (k++ < 20) LOG("【红】顶层面板对拍：「%s」只扫根下一层得 %#llx，全树深搜得 %#llx", name, (unsigned long long)got, (unsigned long long)want); }
    else if (++n_ok % 20000 == 0) LOG("顶层面板对拍：已核 %llu 次一致", (unsigned long long)n_ok);
}

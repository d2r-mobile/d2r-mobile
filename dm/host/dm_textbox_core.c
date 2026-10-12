// 文字框颜色码对照表的纯逻辑（读表、按表改写），不碰游戏内存；dm_textbox.c 挂门调它，单测 tests/t_textbox.c 直接测。
// 表 = 物品注释生成器（dm/ios/tools/annot_strings.py write_textbox_table）写的 dm_textbox_plain.tsv：一行「注释版 \t 文字框版」，\\ \n \t 转义。
// 规则只有一条：表里的注释版才换。整串命中换成文字框版；整串不在表里但含表里的注释版（被拼进长句）就把那几段换掉；其余一个字节都不动。
// Pure logic of the text box color code mapping table (read the table, rewrite by the table), never touching game memory; dm_textbox.c hooks the gate and calls it, and the unit test tests/t_textbox.c tests it directly.
// Table = dm_textbox_plain.tsv written by the item annotation generator (dm/ios/tools/annot_strings.py write_textbox_table): one line of "annotated version \t text box version", with \\\\ \
//  \\t escapes.
// There is only one rule: only annotated versions in the table are replaced. A whole-string hit is replaced with the text box version; a whole string not in the table but containing annotated versions from the table (spliced into a longer sentence) gets those segments replaced; everything else is left untouched, not one byte changed.
#include "dm_textbox_core.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *a, *b; size_t na, nb; uint64_t h; } tb_ent;
static tb_ent *g_tab; static int g_n, g_cap; static uint32_t *g_slot; static uint32_t g_mask;   // 开放寻址：槽里存 条目号 + 1 | Open addressing: a slot stores entry index + 1

static uint64_t fnv(const char *s, size_t n) { uint64_t h = 0xcbf29ce484222325ull; for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 0x100000001b3ull; return h; }
static char *unesc(const char *s, size_t *n) {
    size_t L = strlen(s); char *o = malloc(L + 1), *p = o;
    for (size_t i = 0; i < L; i++) { if (s[i] == '\\' && i + 1 < L) { char c = s[++i]; *p++ = c == 'n' ? '\n' : c == 't' ? '\t' : c; } else *p++ = s[i]; }
    *p = 0; *n = (size_t)(p - o); return o;
}
void dm_textbox_reset(void) {
    for (int i = 0; i < g_n; i++) { free(g_tab[i].a); free(g_tab[i].b); }
    free(g_tab); free(g_slot); g_tab = NULL; g_slot = NULL; g_n = g_cap = 0; g_mask = 0;
}
int dm_textbox_load(const char *path) {
    dm_textbox_reset(); FILE *f = fopen(path, "r"); if (!f) return -1;
    char *ln = NULL; size_t cap = 0; ssize_t r;
    while ((r = getline(&ln, &cap, f)) > 0) { if (ln[0] == '#') continue; if (ln[r - 1] == '\n') ln[--r] = 0; char *t = strchr(ln, '\t'); if (!t) continue; *t = 0;
        if (g_n == g_cap) { g_cap = g_cap ? g_cap * 2 : 1024; g_tab = realloc(g_tab, sizeof *g_tab * (size_t)g_cap); }
        tb_ent *e = &g_tab[g_n++]; e->a = unesc(ln, &e->na); e->b = unesc(t + 1, &e->nb); e->h = fnv(e->a, e->na); }
    free(ln); fclose(f);
    g_mask = 1; while (g_mask < (uint32_t)g_n * 4) g_mask <<= 1; g_slot = calloc(g_mask, sizeof *g_slot); g_mask--;
    for (int i = 0; i < g_n; i++) { uint32_t k = (uint32_t)g_tab[i].h & g_mask; while (g_slot[k]) k = (k + 1) & g_mask; g_slot[k] = (uint32_t)i + 1; }
    return g_n;
}
int dm_textbox_count(void) { return g_n; }
static const tb_ent *find(const char *s, size_t n) {
    uint64_t h = fnv(s, n);
    for (uint32_t k = (uint32_t)h & g_mask; g_slot[k]; k = (k + 1) & g_mask) { const tb_ent *e = &g_tab[g_slot[k] - 1]; if (e->h == h && e->na == n && !memcmp(e->a, s, n)) return e; }
    return NULL;
}
int dm_textbox_rewrite(const char *s, size_t n, char **out, size_t *on) {
    *out = NULL; *on = 0;
    if (!g_n || !s || !n || !memmem(s, n, "\xee\x81\xbe", 3)) return 0;   // 不含 U+E07E：一次扫描就返回 | No U+E07E: return after a single scan
    const tb_ent *e = find(s, n);
    if (e) { *out = malloc(e->nb + 1); memcpy(*out, e->b, e->nb); (*out)[e->nb] = 0; *on = e->nb; return 1; }
    char *cur = malloc(n + 1); memcpy(cur, s, n); cur[n] = 0; size_t cn = n; int any = 0;   // 注释版被拼进长句：逐条找子串换掉（很少走） | Annotated version spliced into a longer sentence: find and replace substrings entry by entry (rarely taken)
    for (int i = 0; i < g_n; i++) { char *p; size_t from = 0;
        while (from < cn && (p = memmem(cur + from, cn - from, g_tab[i].a, g_tab[i].na))) { size_t off = (size_t)(p - cur), nn = cn - g_tab[i].na + g_tab[i].nb; char *x = malloc(nn + 1);
            memcpy(x, cur, off); memcpy(x + off, g_tab[i].b, g_tab[i].nb); memcpy(x + off + g_tab[i].nb, p + g_tab[i].na, cn - off - g_tab[i].na); x[nn] = 0;
            free(cur); cur = x; cn = nn; from = off + g_tab[i].nb; any = 1; } }
    if (!any) { free(cur); return 0; }
    *out = cur; *on = cn; return 2;
}

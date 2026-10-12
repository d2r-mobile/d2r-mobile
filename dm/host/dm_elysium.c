// 「极乐世界」。不接管函数、不改代码页：进该角色的局时改内存里的数据表，退局写回原值。地址为 3.3.94012 原映像偏移。
//
// 角色标记：存档工作目录根下一份名单文件 sw_elysium.txt，一行一个角色名（写在工作目录里才会随游戏的存档提交进已提交版，见 hle_fs.c 事务式存档）。
//   建角：界面消息 TryCreateCharacter 之后第一次进局的那个角色就是新建的——勾了（dm_elysium_arm(1)）把名字加进名单，没勾把同名旧记录删掉（删角色后重建同名）。
//   老角色不在名单里 = 未勾选。角色名取客户端玩家单位的玩家数据（[单位+0x10]，开头即名字）。
//
// 暗金 / 套装：品质比例表 itemratio——数据表结构（[main+0x2d64a18 + 版本×16]，版本 1–3）+0x16c0 记录数组、+0x16c8 条数，每条 0x44 字节：
//   暗金 基数 +0x0 / 除数 +0x4 / 下限 +0x8；套装 基数 +0x18 / 除数 +0x1c / 下限 +0x20（读表描述符 main+0x2096108）。
//   判定（main+0x65c7c4 起，暗金；0x65c8a4 起，套装）：机会值 = (基数 − 等级差 ÷ 除数) × 128；有魔法寻找 mf 时 = (基数 − 等级差 ÷ 除数) × 12800 ÷ 折算（mf ≤ 10 为 100 + mf，否则 100 + mf×250÷(mf+250)，套装 500）；
//   取 max(机会值, 下限)；再减去 机会值 × 宝藏表加成 ÷ 1024；机会值 < 1 必出，否则掷 [0, 机会值) < 128 即出。概率 = 128 ÷ 机会值。
//   所以「概率 ×N」= 机会值 ÷N = 基数 ÷N、除数 ×N、下限 ÷N（魔法寻找与宝藏表加成是按比例的，次序不变）。机会值 ≤ 128 时本来就必出，÷N 后仍必出（封顶 100%）。
//
// 符文 ×5：宝藏表运行时结构——[数据表+0xdb0] 记录数组、+0xdb8 条数，每条 0x50 字节：+0x10 / +0x14 总权重（经典 / 资料片两列）、+0x18 次数、+0x1c「不掉落」权重、
//   +0x38 掉落项数组、+0x40 项数；每项 0x1c 字节：+0 / +4 该项之前的累计权重（两列）、+8 物品行号或宝藏表号、+0xc 标志（0x04 = 这一项是另一张宝藏表）。
//   符文组 = 所有项都是符文物品（物品码 r 加两位数字）或另一张符文组的表（3.3 里是 17 张连成一串，高级的引用低一级的，决定出哪一级符文——这一层不动）。
//   只在「直接引用符文组的那一层」改一次（3.3 里是各幕的「好东西」表 14 张 + 女伯爵符文表 3 张），不会层层叠乘：
//   · 该表（总权重 T、其中符文项权重和 w）里符文项的权重 ×5，其它项不动，新总权重 T+4w。单这样改，符文在表内的占比只到 5w/(T+4w)（不足 5 倍），同表其它项（宝石、首饰等）也被挤掉一截；
//     所以再到引用这张表的上一层去补：上一层所有项与「不掉落」×T，这张表的权重改为原值 ×(T+4w)，多出来的 4w 份从「不掉落」里扣（扣到 0 为止）。
//     「不掉落」够扣时：选到符文的最终概率恰为原来的 5 倍，同表其它项、上一层其它项的最终概率都不变，代价只是空掉落变少。不够扣（上一层没有「不掉落」）时符文不足 5 倍、其它项略降。
//   · w == T（整张表只出符文，女伯爵那三张，上一层是「两张表各出一次」没有权重可调）：只把这张表自己的「不掉落」权重清零（每次必出）。
//   经典列（非资料片角色用）符文权重本来就是 0，只跟着同乘，表内比例不变；「不掉落」两列共用，经典角色的空掉落会略少。
// boss 永远首杀：怪物表——[数据表+0xf90] 记录数组、+0xf98 条数，每条 0x1fc 字节；每个难度一块 0x10 字节（+0xc0 + 难度×0x10）：
//   +0 普通、+2 冠军、+4 头目、+6 任务掉落表、+8 / +0xa / +0xc / +0xe 另一套（恐怖地带等）；+0xf0 任务号、+0xf1 任务完成位。
//   掉落函数 main+0x689e40 先按怪的类别取上面某一格，再看：任务号非 0、任务掉落表非 0、击杀者这个任务既没完成也没置完成位 → 改用任务掉落表（0x68a050–0x68a1b8）。
//   这里把填了任务号且有任务掉落表的那几行的其余七格都写成任务掉落表，等于「是否首杀」恒为真；任务进度一个字节不动。
// 还原：第一次见到表时整份拷下原值并记校验和；退局写回；进局先写回再按标记改；普通角色进局后表的校验和 ≠ 原值即【红】。
// 「极乐世界」(Elysium). No functions taken over, no code pages changed: on entering a run of such a character the in-memory data tables are changed, and on leaving the run the original values are written back. Addresses are offsets in the original 3.3.94012 image.
// Character marker: a list file sw_elysium.txt at the root of the save working directory, one character name per line (only when written in the working directory does it get committed into the committed version with the game's save, see the transactional save in hle_fs.c).
//   Character creation: the character that first enters a run after the UI message TryCreateCharacter is the newly created one -- if checked (dm_elysium_arm(1)) the name is added to the list, if not checked an old record with the same name is deleted (a same-name character recreated after deletion).
//   An old character not in the list = not checked. The character name is taken from the player data of the client player unit ([unit+0x10], the name is at the very start).
// Uniques / sets: the quality ratio table itemratio -- data table structure ([main+0x2d64a18 + version×16], versions 1–3) +0x16c0 record array, +0x16c8 count, 0x44 bytes per record:
//   unique base +0x0 / divisor +0x4 / minimum +0x8; set base +0x18 / divisor +0x1c / minimum +0x20 (table read descriptor main+0x2096108).
//   Decision (from main+0x65c7c4, unique; from 0x65c8a4, set): chance value = (base − level difference ÷ divisor) × 128; with magic find mf = (base − level difference ÷ divisor) × 12800 ÷ conversion (100 + mf for mf ≤ 10, otherwise 100 + mf×250÷(mf+250), 500 for sets);
//   take max(chance value, minimum); then subtract chance value × treasure class bonus ÷ 1024; chance value < 1 always drops, otherwise it drops if a roll in [0, chance value) < 128. Probability = 128 ÷ chance value.
//   So 'probability ×N' = chance value ÷N = base ÷N, divisor ×N, minimum ÷N (magic find and the treasure class bonus are proportional, and the order is unchanged). When the chance value ≤ 128 it always dropped anyway, and after ÷N it still always drops (capped at 100%).
// Runes ×5: runtime structure of the treasure class tables -- [data table+0xdb0] record array, +0xdb8 count, 0x50 bytes per record: +0x10 / +0x14 total weight (two columns, classic / expansion), +0x18 count, +0x1c 'NoDrop' weight,
//   +0x38 drop entry array, +0x40 entry count; 0x1c bytes per entry: +0 / +4 cumulative weight before this entry (two columns), +8 item row number or treasure class number, +0xc flags (0x04 = this entry is another treasure class).
//   Rune group = all entries are rune items (item code r plus two digits) or another rune group table (in 3.3 there are 17 tables linked in a chain, the higher one referencing the next lower one, deciding which rune level drops -- this layer is not touched).
//   The change is made only once, at 'the layer that references the rune groups directly' (in 3.3 the 14 per-act 'Good' tables + the 3 Countess rune tables), so it does not multiply layer upon layer:
//   - In that table (total weight T, of which the rune entries' weights sum to w) the rune entries' weights are ×5, the other entries are untouched, and the new total weight is T+4w. With this change alone the runes' share within the table only reaches 5w/(T+4w) (less than 5x), and the other entries of the same table (gems, jewelry, etc.) are also squeezed down a bit;
//     so compensation is then made in the layer above that references this table: all entries of the layer above and 'NoDrop' are ×T, this table's weight becomes the original value ×(T+4w), and the extra 4w shares are deducted from 'NoDrop' (down to 0 at most).
//     When 'NoDrop' is enough to deduct from: the final probability of picking a rune is exactly 5 times the original, the final probabilities of the other entries of the same table and of the other entries of the layer above are all unchanged, and the only cost is fewer empty drops. When it is not enough (the layer above has no 'NoDrop') runes are less than 5x and the other entries drop slightly.
//   - w == T (the whole table drops only runes, the three Countess tables, whose layer above is 'one drop from each of two tables' with no weights to adjust): only this table's own 'NoDrop' weight is cleared to zero (always drops).
//   The classic column (used by non-expansion characters) has rune weight 0 to begin with and is only multiplied along, so the in-table ratio is unchanged; 'NoDrop' is shared by both columns, so classic characters get slightly fewer empty drops.
// A boss is always a first kill: monster table -- [data table+0xf90] record array, +0xf98 count, 0x1fc bytes per record; one 0x10-byte block per difficulty (+0xc0 + difficulty×0x10):
//   +0 normal, +2 champion, +4 unique, +6 quest treasure class, +8 / +0xa / +0xc / +0xe another set (Terror Zones, etc.); +0xf0 quest number, +0xf1 quest completion bit.
//   The drop function main+0x689e40 first takes one of the cells above by the monster's category, then checks: quest number non-zero, quest treasure class non-zero, and the killer has neither completed this quest nor has the completion bit set -> use the quest treasure class instead (0x68a050–0x68a1b8).
//   Here, for the few rows that have a quest number filled in and a quest treasure class, the other seven cells are all written as the quest treasure class, which amounts to 'is first kill' always being true; not one byte of quest progress is touched.
// Restore: the first time the tables are seen the original values are copied in full and a checksum recorded; written back on leaving the run; on entering a run they are first written back and then changed according to the marker; for a normal character, a table checksum ≠ the original after entering a run is 【红】(red).
#include "dm_elysium.h"
#include "hle.h"
#include "dm.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATIO_REC 0x44
#define UNIQUE_MUL 5
#define SET_MUL 3
void uimsg_subscribe(void (*fn)(const char *cat, const char *act, const char *arg));
const char *dm_save_workdir(void);

static _Atomic int g_arm, g_created, g_active;
static uint8_t *g_orig[4]; static uint64_t g_orig_n[4], g_orig_sum; static int g_have_orig;

static _Atomic int g_topup;   // 进局的这个极乐世界角色还欠一次共享仓库补齐：服务端那一侧去补（dm_elysium_stash_step） | The Elysium character entering the run still owes one shared stash top-up: the server side does it (dm_elysium_stash_step)
static _Atomic int g_topup_done;   // 1 = 服务端补完且核过数了，等主线程请游戏存盘并划欠条 | 1 = the server side has finished topping up and verified the amounts; waiting for the main thread to ask the game to save and strike off the IOU
static char g_topup_name[64];
void dm_elysium_arm(int on) { atomic_store(&g_arm, on != 0); LOG("极乐世界：建角勾选 = %d", on != 0); }
int dm_elysium_active(void) { return atomic_load(&g_active); }
static void on_uimsg(const char *cat, const char *act, const char *arg) { (void)cat; (void)arg; if (!strcmp(act, "TryCreateCharacter")) atomic_store(&g_created, 1); }

static uint8_t *main_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static uint64_t table(uint8_t *b, int ver, uint64_t *n) { uint64_t t = *(uint64_t *)(b + 0x2d64a18 + (uint64_t)ver * 16); if (!t) { *n = 0; return 0; } *n = *(uint64_t *)(t + 0x16c8); return *(uint64_t *)(t + 0x16c0); }
static uint64_t fnv(uint64_t h, const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; } return h; }
static uint64_t tc_sum(uint8_t *b, uint64_t h);
static uint64_t mon_sum(uint8_t *b, uint64_t h);
static uint64_t tables_sum(uint8_t *b) { uint64_t h = 0xcbf29ce484222325ull; for (int v = 1; v <= 3; v++) { uint64_t n, r = table(b, v, &n); if (r && n) h = fnv(h, (const uint8_t *)(uintptr_t)r, n * RATIO_REC); } return mon_sum(b, tc_sum(b, h)); }

#define TC_REC 0x50
#define TC_ENT 0x1c
#define RUNE_MUL 5
#define NODROP_DIV 10
typedef struct { uint64_t arr, n; uint8_t *recs; uint8_t **ents; } tc_snap;
static tc_snap g_tc[4];
static uint64_t tc_table(uint8_t *b, int ver, uint64_t *n) { uint64_t t = *(uint64_t *)(b + 0x2d64a18 + (uint64_t)ver * 16); if (!t) { *n = 0; return 0; } *n = *(uint64_t *)(t + 0xdb8); return *(uint64_t *)(t + 0xdb0); }
static uint64_t tc_sum(uint8_t *b, uint64_t h) { for (int v = 1; v <= 3; v++) { uint64_t n, a = tc_table(b, v, &n); for (uint64_t i = 0; a && i < n; i++) { uint8_t *r = (uint8_t *)(uintptr_t)(a + i * TC_REC); h = fnv(h, r + 0x10, 0x10); uint64_t e = *(uint64_t *)(r + 0x38), ne = *(uint64_t *)(r + 0x40); if (e && ne) h = fnv(h, (const uint8_t *)(uintptr_t)e, ne * TC_ENT); } } return h; }
static int tc_snapshot(uint8_t *b) {
    for (int v = 1; v <= 3; v++) { uint64_t n, a = tc_table(b, v, &n); if (!a || !n) continue; tc_snap *s = &g_tc[v]; s->arr = a; s->n = n; s->recs = malloc(n * TC_REC); s->ents = calloc(n, sizeof *s->ents); if (!s->recs || !s->ents) return 0; memcpy(s->recs, (void *)(uintptr_t)a, n * TC_REC);
        for (uint64_t i = 0; i < n; i++) { uint8_t *r = s->recs + i * TC_REC; uint64_t e = *(uint64_t *)(r + 0x38), ne = *(uint64_t *)(r + 0x40); if (e && ne) { s->ents[i] = malloc(ne * TC_ENT); if (!s->ents[i]) return 0; memcpy(s->ents[i], (void *)(uintptr_t)e, ne * TC_ENT); } } }
    return 1;
}
// 表还在原处、条数与各项数组指针都没变才动（游戏没有重载数据表）
// Act only if the table is still in the same place and the count and each entry array pointer are unchanged (the game has not reloaded the data tables)
static int tc_same(uint8_t *b, int v) { tc_snap *s = &g_tc[v]; uint64_t n, a = tc_table(b, v, &n); if (!s->recs || a != s->arr || n != s->n) return 0;
    for (uint64_t i = 0; i < n; i++) { uint8_t *r = (uint8_t *)(uintptr_t)(a + i * TC_REC), *o = s->recs + i * TC_REC; if (*(uint64_t *)(r + 0x38) != *(uint64_t *)(o + 0x38) || *(uint64_t *)(r + 0x40) != *(uint64_t *)(o + 0x40)) return 0; } return 1; }
static void tc_restore(uint8_t *b) { for (int v = 1; v <= 3; v++) { if (!tc_same(b, v)) continue; tc_snap *s = &g_tc[v];
        for (uint64_t i = 0; i < s->n; i++) { uint8_t *r = (uint8_t *)(uintptr_t)(s->arr + i * TC_REC), *o = s->recs + i * TC_REC; memcpy(r + 0x10, o + 0x10, 0x10); uint64_t ne = *(uint64_t *)(o + 0x40); if (s->ents[i]) memcpy((void *)(uintptr_t)*(uint64_t *)(o + 0x38), s->ents[i], ne * TC_ENT); } } }
// 一张表的两列权重（由「该项之前的累计」还原出每项自己的权重）与写回
// The two weight columns of a table (each entry's own weight is recovered from 'the cumulative total before this entry') and writing them back
static void tc_weights(uint8_t *r, int64_t *wc, int64_t *we) { uint8_t *e = (uint8_t *)(uintptr_t)*(uint64_t *)(r + 0x38); uint64_t ne = *(uint64_t *)(r + 0x40);
    for (uint64_t k = 0; k < ne; k++) { int32_t nc = k + 1 < ne ? *(int32_t *)(e + (k + 1) * TC_ENT) : *(int32_t *)(r + 0x10), nx = k + 1 < ne ? *(int32_t *)(e + (k + 1) * TC_ENT + 4) : *(int32_t *)(r + 0x14); wc[k] = nc - *(int32_t *)(e + k * TC_ENT); we[k] = nx - *(int32_t *)(e + k * TC_ENT + 4); } }
static void tc_write(uint8_t *r, const int64_t *wc, const int64_t *we) { uint8_t *e = (uint8_t *)(uintptr_t)*(uint64_t *)(r + 0x38); uint64_t ne = *(uint64_t *)(r + 0x40); int64_t c = 0, x = 0;
    for (uint64_t k = 0; k < ne; k++) { *(int32_t *)(e + k * TC_ENT) = (int32_t)c; *(int32_t *)(e + k * TC_ENT + 4) = (int32_t)x; c += wc[k]; x += we[k]; } *(int32_t *)(r + 0x10) = (int32_t)c; *(int32_t *)(r + 0x14) = (int32_t)x; }
#define MON_REC 0x1fc
static uint8_t *g_mon[4]; static uint64_t g_mon_arr[4], g_mon_n[4];
static uint64_t mon_table(uint8_t *b, int ver, uint64_t *n) { uint64_t t = *(uint64_t *)(b + 0x2d64a18 + (uint64_t)ver * 16); if (!t) { *n = 0; return 0; } *n = *(uint64_t *)(t + 0xf98); return *(uint64_t *)(t + 0xf90); }
static int mon_snapshot(uint8_t *b) { for (int v = 1; v <= 3; v++) { uint64_t n, a = mon_table(b, v, &n); if (!a || !n) continue; g_mon[v] = malloc(n * 0x30); if (!g_mon[v]) return 0; g_mon_arr[v] = a; g_mon_n[v] = n;
        for (uint64_t i = 0; i < n; i++) memcpy(g_mon[v] + i * 0x30, (void *)(uintptr_t)(a + i * MON_REC + 0xc0), 0x30); } return 1; }
static int mon_same(uint8_t *b, int v) { uint64_t n, a = mon_table(b, v, &n); return g_mon[v] && a == g_mon_arr[v] && n == g_mon_n[v]; }
static uint64_t mon_sum(uint8_t *b, uint64_t h) { for (int v = 1; v <= 3; v++) { uint64_t n, a = mon_table(b, v, &n); for (uint64_t i = 0; a && i < n; i++) h = fnv(h, (const uint8_t *)(uintptr_t)(a + i * MON_REC + 0xc0), 0x30); } return h; }
static void mon_restore(uint8_t *b) { for (int v = 1; v <= 3; v++) if (mon_same(b, v)) for (uint64_t i = 0; i < g_mon_n[v]; i++) memcpy((void *)(uintptr_t)(g_mon_arr[v] + i * MON_REC + 0xc0), g_mon[v] + i * 0x30, 0x30); }
static void mon_apply(uint8_t *b) {
    for (int v = 1; v <= 3; v++) { if (!mon_same(b, v)) continue; { int dup = 0; for (int u = 1; u < v; u++) if (g_mon_arr[u] == g_mon_arr[v]) dup = 1; if (dup) continue; } char list[640] = ""; int nb = 0;
        for (uint64_t i = 0; i < g_mon_n[v]; i++) { uint8_t *r = (uint8_t *)(uintptr_t)(g_mon_arr[v] + i * MON_REC); if (!r[0xf0]) continue; int any = 0;
            for (int d = 0; d < 3; d++) { uint16_t *blk = (uint16_t *)(r + 0xc0 + d * 0x10), q = blk[3]; if (!q) continue; any = 1; for (int k = 0; k < 8; k++) if (k != 3) blk[k] = q; }
            if (any) { nb++; size_t L = strlen(list); snprintf(list + L, sizeof list - L, " %llu（任务 %u 位 %u，三个难度的普通格 %u/%u/%u → %u/%u/%u）", i, r[0xf0], r[0xf1], *(uint16_t *)(g_mon[v] + i * 0x30), *(uint16_t *)(g_mon[v] + i * 0x30 + 0x10), *(uint16_t *)(g_mon[v] + i * 0x30 + 0x20), *(uint16_t *)(r + 0xc0), *(uint16_t *)(r + 0xd0), *(uint16_t *)(r + 0xe0)); } }
        if (v == 2 || v == 3 || getenv("DM_ELYSIUM_LOG")) LOG("极乐世界：版本 %d 怪物表——永远按首杀掉落的 %d 行：%s", v, nb, list); }
}
#define TC_MAXE 64
static void tc_apply(uint8_t *b) {
    for (int v = 1; v <= 3; v++) { if (!tc_same(b, v)) continue; tc_snap *s = &g_tc[v]; uint64_t n = s->n; { int dup = 0; for (int u = 1; u < v; u++) if (g_tc[u].arr == s->arr) dup = 1; if (dup) continue; }   // 两个版本共用同一份表时只改一次 | When two versions share the same table, change it only once
        uint64_t t = *(uint64_t *)(b + 0x2d64a18 + (uint64_t)v * 16), items = *(uint64_t *)(t + 0x15c0); uint32_t nitems = *(uint32_t *)(t + 0x15c8);
        uint8_t *rune = calloc(n, 1); int64_t *fa = calloc(n, sizeof *fa), *fd = calloc(n, sizeof *fd); if (!rune || !fa || !fd) { free(rune); free(fa); free(fd); continue; }
        // 符文组：先假定「项全是符文物品或引用别的表」的都是，再反复剔掉引用了非符文组的，直到不变
        // Rune groups: first assume every table whose 'entries are all rune items or references to other tables' is one, then repeatedly remove those that reference a non-rune-group, until nothing changes
        for (uint64_t i = 0; i < n; i++) { uint8_t *r = (uint8_t *)(uintptr_t)(s->arr + i * TC_REC), *e = (uint8_t *)(uintptr_t)*(uint64_t *)(r + 0x38); uint64_t ne = *(uint64_t *)(r + 0x40); int ok = e && ne > 0 && ne <= TC_MAXE, any = 0;
            for (uint64_t k = 0; ok && k < ne; k++) { uint16_t id = *(uint16_t *)(e + k * TC_ENT + 8); if (e[k * TC_ENT + 0xc] & 4) continue; const char *c = id < nitems ? (const char *)(uintptr_t)(items + (uint64_t)id * 0x1c0 + 0x80) : "";
                if (c[0] == 'r' && c[1] >= '0' && c[1] <= '9' && c[2] >= '0' && c[2] <= '9' && (c[3] == ' ' || !c[3])) any = 1; else ok = 0; }
            rune[i] = ok ? (any ? 1 : 2) : 0; }   // 2 = 只有引用、还没见到符文物品 | 2 = only references, no rune item seen yet
        for (int ch = 1; ch; ) { ch = 0; for (uint64_t i = 0; i < n; i++) { if (!rune[i]) continue; uint8_t *r = (uint8_t *)(uintptr_t)(s->arr + i * TC_REC), *e = (uint8_t *)(uintptr_t)*(uint64_t *)(r + 0x38); uint64_t ne = *(uint64_t *)(r + 0x40);
                for (uint64_t k = 0; k < ne; k++) if (e[k * TC_ENT + 0xc] & 4) { uint16_t id = *(uint16_t *)(e + k * TC_ENT + 8); if (id >= n || !rune[id]) { rune[i] = 0; ch = 1; break; } } } }
        for (uint64_t i = 0; i < n; i++) if (rune[i] == 2) { uint8_t *r = (uint8_t *)(uintptr_t)(s->arr + i * TC_REC), *e = (uint8_t *)(uintptr_t)*(uint64_t *)(r + 0x38); uint64_t ne = *(uint64_t *)(r + 0x40); int direct = 0;   // 只含引用的表（女伯爵符文表那种）算「引用符文组的那一层」，不算符文组本身 | A table containing only references (like the Countess rune table) counts as 'the layer that references the rune groups', not as a rune group itself
            for (uint64_t k = 0; k < ne; k++) if (!(e[k * TC_ENT + 0xc] & 4)) direct = 1; if (!direct) rune[i] = 0; }
        int nrune = 0, nx5 = 0, nfull = 0, npar = 0, nshort = 0;
        for (uint64_t i = 0; i < n; i++) nrune += rune[i] != 0;
        // 第一步：直接引用符文组的那一层
        // Step 1: the layer that references the rune groups directly
        for (uint64_t i = 0; i < n; i++) { if (rune[i]) continue; uint8_t *r = (uint8_t *)(uintptr_t)(s->arr + i * TC_REC), *e = (uint8_t *)(uintptr_t)*(uint64_t *)(r + 0x38); uint64_t ne = *(uint64_t *)(r + 0x40); if (!e || !ne || ne > TC_MAXE) continue;
            if (*(int32_t *)(r + 0x18) < 0) continue;
            int64_t wc[TC_MAXE], we[TC_MAXE], T = 0, w = 0; tc_weights(r, wc, we);
            for (uint64_t k = 0; k < ne; k++) { T += we[k]; if ((e[k * TC_ENT + 0xc] & 4) && *(uint16_t *)(e + k * TC_ENT + 8) < n && rune[*(uint16_t *)(e + k * TC_ENT + 8)]) w += we[k]; }
            if (w <= 0) continue;
            if (w < T) {
                for (uint64_t k = 0; k < ne; k++) { int isr = (e[k * TC_ENT + 0xc] & 4) && *(uint16_t *)(e + k * TC_ENT + 8) < n && rune[*(uint16_t *)(e + k * TC_ENT + 8)]; if (isr) we[k] *= RUNE_MUL; }
                tc_write(r, wc, we); fa[i] = T + (RUNE_MUL - 1) * w; fd[i] = T; nx5++; }
            else { *(int32_t *)(r + 0x1c) = 0; nfull++; } }
        // 第二步：引用了上面那些表的上一层，把被挤掉的份额从「不掉落」里补回来
        // Step 2: in the layer above that references the tables above, compensate the squeezed-out share back from 'NoDrop'
        for (uint64_t i = 0; i < n; i++) { uint8_t *r = (uint8_t *)(uintptr_t)(s->arr + i * TC_REC), *e = (uint8_t *)(uintptr_t)*(uint64_t *)(r + 0x38); uint64_t ne = *(uint64_t *)(r + 0x40); if (!e || !ne || ne > TC_MAXE) continue;
            if (*(int32_t *)(r + 0x18) < 0) continue;   // 次数为负 = 「各项按权重数各出若干次」，权重是次数不是概率，不能缩放 | A negative count = 'each entry drops as many times as its weight'; the weight is a count, not a probability, and cannot be scaled
            for (uint64_t j = 0; j < ne; j++) { uint16_t id = *(uint16_t *)(e + j * TC_ENT + 8); if (!(e[j * TC_ENT + 0xc] & 4) || id >= n || !fd[id]) continue;
                int64_t wc[TC_MAXE], we[TC_MAXE], A = fa[id], D = fd[id], N = *(int32_t *)(r + 0x1c); tc_weights(r, wc, we); int64_t g = we[j];
                for (uint64_t k = 0; k < ne; k++) { wc[k] *= D; we[k] *= D; } we[j] = g * A; N = N * D - g * (A - D); if (N < 0) { N = 0; nshort++; }
                tc_write(r, wc, we); *(int32_t *)(r + 0x1c) = (int32_t)N; npar++; } }
        // 第三步（所有怪的「不掉落」都降到十分之一）：每张表的「不掉落」权重 ÷10（四舍五入，原来非 0 的至少留 1，「偶尔空」不变成「必出」）。
        // 放在符文补偿之后：补偿按原刻度从「不掉落」里扣出符文多出的份额（其它项绝对概率不变），剩下的再 ÷10，所以符文仍是其它项的 5 倍关系。
        // 人数档的换算（游戏掉落时按人数把「不掉落」再缩一次）读的就是这个字段，在 ÷10 之后照常生效
        // Step 3 (the 'NoDrop' of all monsters is reduced to one tenth): the 'NoDrop' weight of every table ÷10 (rounded; values that were non-zero keep at least 1, so 'occasionally empty' does not become 'always drops').
        // Placed after the rune compensation: the compensation deducts the runes' extra share from 'NoDrop' on the original scale (the absolute probability of the other entries is unchanged), and what remains is then ÷10, so runes keep the 5x relation to the other entries.
        // The players-setting conversion (at drop time the game shrinks 'NoDrop' once more by player count) reads this very field and takes effect as usual after the ÷10
        int ndiv = 0; for (uint64_t i = 0; i < n; i++) { uint8_t *r = (uint8_t *)(uintptr_t)(s->arr + i * TC_REC); int32_t N = *(int32_t *)(r + 0x1c); if (N <= 0) continue; int32_t q = (N + NODROP_DIV / 2) / NODROP_DIV; *(int32_t *)(r + 0x1c) = q ? q : 1; ndiv++; }
        if (v == 2 || getenv("DM_ELYSIUM_LOG")) LOG("极乐世界：版本 %d 宝藏表——符文组 %d 张；引用它的表里符文权重 ×%d 的 %d 张、整表只出符文改成必出的 %d 张；上一层补偿 %d 项（其中「不掉落」不够扣的 %d 项）；「不掉落」÷%d 的 %d 张", v, nrune, RUNE_MUL, nx5, nfull, npar, nshort, NODROP_DIV, ndiv);
        free(rune); free(fa); free(fd); }
}

static int snapshot(uint8_t *b) {
    if (g_have_orig) return 1; int any = 0;
    for (int v = 1; v <= 3; v++) { uint64_t n, r = table(b, v, &n); if (!r || !n) continue; g_orig[v] = malloc(n * RATIO_REC); if (!g_orig[v]) return 0; memcpy(g_orig[v], (void *)(uintptr_t)r, n * RATIO_REC); g_orig_n[v] = n; any = 1; }
    if (!any) return 0;
    if (!tc_snapshot(b) || !mon_snapshot(b)) return 0;
    g_orig_sum = tables_sum(b); g_have_orig = 1; LOG("极乐世界：记下品质比例表与宝藏表原值（版本 1–3 各 %llu / %llu / %llu 条，校验和 %016llx）", g_orig_n[1], g_orig_n[2], g_orig_n[3], g_orig_sum); return 1;
}
static void restore(uint8_t *b) { tc_restore(b); mon_restore(b); for (int v = 1; v <= 3; v++) { uint64_t n, r = table(b, v, &n); if (r && g_orig[v] && n == g_orig_n[v]) memcpy((void *)(uintptr_t)r, g_orig[v], n * RATIO_REC); } }
static void apply(uint8_t *b) {
    for (int v = 1; v <= 3; v++) { uint64_t n, r = table(b, v, &n); if (!r || !g_orig[v] || n != g_orig_n[v]) continue;
        for (uint64_t i = 0; i < n; i++) { int32_t *o = (int32_t *)(g_orig[v] + i * RATIO_REC), *d = (int32_t *)(uintptr_t)(r + i * RATIO_REC);
            d[0] = o[0] / UNIQUE_MUL; d[1] = o[1] * UNIQUE_MUL; d[2] = o[2] / UNIQUE_MUL;
            d[6] = o[6] / SET_MUL; d[7] = o[7] * SET_MUL; d[8] = o[8] / SET_MUL; } }
}
static void dump(uint8_t *b, const char *when) {
    for (int v = 1; v <= 3; v++) { uint64_t n, r = table(b, v, &n); for (uint64_t i = 0; r && i < n; i++) { int32_t *d = (int32_t *)(uintptr_t)(r + i * RATIO_REC); uint8_t *c = (uint8_t *)d;
        LOG("极乐世界[%s] 版本 %d 第 %llu 条（版本号 %u 特殊 %u 职业专属 %u）：暗金 %d / %d / %d，套装 %d / %d / %d", when, v, i, *(uint16_t *)(c + 0x40), c[0x42], c[0x43], d[0], d[1], d[2], d[6], d[7], d[8]); } }
}

// 名单文件：读 = 名字在不在；写 = 加上 / 去掉这个名字（整份重写）
// List file: read = whether the name is in it; write = add / remove this name (the whole file is rewritten)
static int list_path(const char *file, char *out, size_t cap) { const char *w = dm_save_workdir(); if (!w) return 0; snprintf(out, cap, "%s/%s", w, file); return 1; }
#define LIST_ELY "sw_elysium.txt"
#define LIST_TOPUP "dm_stash_topup.txt"   /* 还欠着一次共享仓库补齐的角色名（建角时记，补齐并落盘后删；写在工作目录里随存档提交） | Names of characters that still owe one shared stash top-up (recorded at character creation, deleted after the top-up is complete and persisted; written in the working directory so it is committed with the save) */
static int list_has(const char *file, const char *name) { char p[1200], ln[128]; if (!list_path(file, p, sizeof p)) return 0; FILE *f = fopen(p, "r"); if (!f) return 0; int hit = 0;
    while (fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\r\n")] = 0; if (!strcmp(ln, name)) hit = 1; } fclose(f); return hit; }
static int list_set(const char *file, const char *name, int on) { char p[1200], t[1210], ln[128]; if (!list_path(file, p, sizeof p)) return 0; snprintf(t, sizeof t, "%s.tmp", p);
    FILE *o = fopen(t, "w"); if (!o) return 0; FILE *f = fopen(p, "r"); int kept = 0;
    if (f) { while (fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\r\n")] = 0; if (ln[0] && strcmp(ln, name)) { fprintf(o, "%s\n", ln); kept++; } } fclose(f); }
    if (on) { fprintf(o, "%s\n", name); kept++; }
    int ok = fclose(o) == 0; if (!ok) { remove(t); return 0; }
    if (kept) return rename(t, p) == 0; remove(t); return remove(p) == 0 || !f; }

// 诊断：DM_ELYSIUM_TCDUMP=<路径>——进局时把宝藏表运行时结构整份打到文件（每个版本：记录数组 [数据表+0xdb0]、条数 +0xdb8、每条 0x50 字节；掉落项数组 [记录+0x38]、项数 +0x40、每项 0x1c 字节）
// Diagnostic: DM_ELYSIUM_TCDUMP=<path> -- on run entry dump the whole runtime structure of the treasure class tables to a file (for each version: record array [data table+0xdb0], count +0xdb8, 0x50 bytes per record; drop entry array [record+0x38], entry count +0x40, 0x1c bytes per entry)
static void tc_dump(uint8_t *b) {
    const char *path = getenv("DM_ELYSIUM_TCDUMP"); if (!path) return; FILE *f = fopen(path, "w"); if (!f) return;
    for (int v = 1; v <= 3; v++) { uint64_t t = *(uint64_t *)(b + 0x2d64a18 + (uint64_t)v * 16); if (!t) continue; uint64_t arr = *(uint64_t *)(t + 0xdb0), n = *(uint64_t *)(t + 0xdb8), items = *(uint64_t *)(t + 0x15c0); uint32_t nitems = *(uint32_t *)(t + 0x15c8);
        fprintf(f, "# 版本 %d：%llu 条\n", v, n);
        for (uint64_t i = 0; arr && i < n; i++) { uint8_t *r = (uint8_t *)(uintptr_t)(arr + i * 0x50); uint64_t ent = *(uint64_t *)(r + 0x38), ne = *(uint64_t *)(r + 0x40);
            fprintf(f, "v%d tc %llu 组 %u 级 %u +8=%d +c=%d 累计 %d/%d +18=%d +1c=%d +20=%d 标志 %02x %02x 品质 %u %u %u %u %u %u 项 %llu：", v, i, *(uint16_t *)r, *(uint16_t *)(r + 2), *(int32_t *)(r + 8), *(int32_t *)(r + 0xc), *(int32_t *)(r + 0x10), *(int32_t *)(r + 0x14),
                    *(int32_t *)(r + 0x18), *(int32_t *)(r + 0x1c), *(int32_t *)(r + 0x20), r[0x24], r[0x25], *(uint16_t *)(r + 0x26), *(uint16_t *)(r + 0x28), *(uint16_t *)(r + 0x2a), *(uint16_t *)(r + 0x2c), *(uint16_t *)(r + 0x2e), *(uint16_t *)(r + 0x30), ne);
            for (uint64_t k = 0; ent && k < ne && k < 64; k++) { uint8_t *e = (uint8_t *)(uintptr_t)(ent + k * 0x1c); uint16_t id = *(uint16_t *)(e + 8); char code[5] = "    ";
                if (!(e[0xc] & 4) && id < nitems) memcpy(code, (void *)(uintptr_t)(items + (uint64_t)id * 0x1c0 + 0x80), 4);
                fprintf(f, " [%d/%d id %u +a=%u 标志 %02x %.4s]", *(int32_t *)e, *(int32_t *)(e + 4), id, *(uint16_t *)(e + 0xa), e[0xc], code); }
            fprintf(f, "\n"); } }
    fclose(f);
}

void dm_elysium_pump(void) {
    static int sub, in_game; if (!sub) { sub = 1; uimsg_subscribe(on_uimsg); if (getenv("DM_TEST_ELYSIUM_ARM")) dm_elysium_arm(1); }
    uint8_t *b = main_base(); if (!b || !snapshot(b)) return;
    uint64_t pl = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(b + 0x2d20e4c)), pd = pl ? *(uint64_t *)(pl + 0x10) : 0;
    if (!pd) { if (in_game) { in_game = 0; restore(b); if (atomic_exchange(&g_active, 0)) LOG("极乐世界：退局，品质比例表与宝藏表已写回原值（校验和 %s）", tables_sum(b) == g_orig_sum ? "相符" : "【红】不符"); } return; }
    if (in_game) {   // 补齐做完后：等游戏把这次存盘提交了再把欠条划掉，并再存一次把「划掉」也落盘；没等到就留着欠条，下次进局再补（补到定数，不会多给） | After the top-up is done: wait for the game to commit this save before striking off the IOU, then save once more so the 'strike-off' is persisted too; if the wait fails keep the IOU and top up again on the next run entry (topped up to a fixed amount, never giving extra)
        static uint64_t c0, f0, cl, fl; static int w; uint64_t dm_fs_commit_count(void), dm_guest_frame(void); int dm_request_save(void); uint64_t c = dm_fs_commit_count(), f = dm_guest_frame();
        if (atomic_load(&g_topup_done) == 1) { atomic_store(&g_topup_done, 2); c0 = cl = c; f0 = fl = f; w = 1; dm_request_save(); }
        else if (w) { if (c != cl) { cl = c; fl = f; }   /* 共享仓库没变时游戏只提交角色那一次，所以判「有过提交且之后半秒没有新的」 | When the shared stash has not changed the game commits only the one time for the character, so the test is 'there has been a commit and no new one in the half second after it' */
            if (c > c0 && f - fl >= 30) { w = 0; atomic_store(&g_topup_done, 0); int ok = list_set(LIST_TOPUP, g_topup_name, 0); dm_request_save(); LOG("极乐世界：共享仓库补齐已随存档提交落盘（%llu 次提交），「%s」的欠条已划掉%s", (unsigned long long)(c - c0), g_topup_name, ok ? "" : "【红】欠条文件写失败"); }
            else if (f - f0 > 600) { w = 0; atomic_store(&g_topup_done, 0); LOG("【红】极乐世界：共享仓库补齐后 10 秒没等到存档提交，「%s」的欠条留着，下次进局再补", g_topup_name); } }
        return; }
    in_game = 1;
    char name[64]; strlcpy(name, (const char *)(uintptr_t)pd, sizeof name); if (!name[0] || strchr(name, '/')) return;
    if (atomic_exchange(&g_created, 0)) { int on = atomic_exchange(&g_arm, 0), ok = list_set(LIST_ELY, name, on); if (ok) list_set(LIST_TOPUP, name, on); if (on || !ok) LOG("极乐世界：新建角色「%s」%s%s", name, on ? "记为极乐世界角色" : "不是极乐世界角色", ok ? "" : "【红】名单文件写失败"); }
    restore(b);   // 保险：上一局没还原也从原值起算 | Safety: start from the original values even if the previous run did not restore
    const char *te = getenv("DM_TEST_ELYSIUM"); int on = list_has(LIST_ELY, name) || (te && !strcmp(te, name));   // 测试：DM_TEST_ELYSIUM=<角色名> 把这个角色当极乐世界角色（不写名单） | Test: DM_TEST_ELYSIUM=<character name> treats this character as an Elysium character (the list is not written)
    if (on && list_has(LIST_TOPUP, name)) { strlcpy(g_topup_name, name, sizeof g_topup_name); atomic_store(&g_topup_done, 0); atomic_store(&g_topup, 1); }   // 这个角色还欠一次共享仓库补齐（新建的，或上次补完没来得及落盘） | This character still owes one shared stash top-up (newly created, or the last top-up did not get persisted in time)
    if (on) { apply(b); tc_apply(b); mon_apply(b); atomic_store(&g_active, 1); LOG("极乐世界：角色「%s」进局，暗金机会值 ÷%d、套装 ÷%d、符文 ×%d、不掉落 ÷%d、boss 永远首杀（品质比例表、宝藏表、怪物表已改，退局还原）", name, UNIQUE_MUL, SET_MUL, RUNE_MUL, NODROP_DIV); }
    else if (tables_sum(b) != g_orig_sum) LOG("【红】极乐世界：普通角色「%s」进局后品质比例表 / 宝藏表校验和与启动时不符", name);
    else if (getenv("DM_ELYSIUM_LOG")) LOG("极乐世界：普通角色「%s」进局，品质比例表与宝藏表校验和与启动时相符", name);
    if (getenv("DM_ELYSIUM_LOG")) dump(b, on ? "极乐" : "普通");
    tc_dump(b);
}

// 新建极乐世界角色时补齐一次：共享仓库金币合计不足 250 万补到 250 万；赦免之证（共享页里的 + 这个角色身上的）不足 2 枚补到 2 枚，发到这个角色的背包；已有的一样不动。
// 游戏启动时就把共享仓库文件读进内存、之后不再读文件，所以不改文件，在新角色第一次进局后改游戏里的那份，由游戏自己存盘：
//   共享仓库每页是一个单位（类型 0，与玩家同一张表 游戏+0x2230），玩家物品栏 +0x68 是「我能用的仓库单位」链表：{+0 单位 id、+9 可用、+0xa 成叠页（宝石 / 材料 / 符文）、+0x10 下一个}；
//   3.3 资料片是 5 个共享页（id 2–6）加 1 个成叠页（id 7）。每页金币 = 该单位的属性 15，上限 250 万（界面「金币最大数」）。
//   金币：原版设属性 main+0x5bc1f0(单位, 15, 值, 0)，再照原版存取金币那条路把新值发给客户端（stash_gold_notify）。
//   赦免之证：原版「按物品码在单位旁掉一件」main+0x7c0b20(游戏, 玩家, 'toa ', 品质 2) 造出来，交一个真实的捡物品包 0x16 进这个角色的背包，就留在背包里。
//     原先还要再交一个「移进共享页」包 0x55 把它挪进共享页——那个包服务端只在人站在原版出生点附近时才收：进局落点改到小站旁之后，每个新建的极乐角色
//     两枚都挪不进去、「共享页里数不到 2 枚」→ 欠条永远不划 → 每次进局再造两枚、金币花掉多少补回多少（线上缺陷）。同一存档同一包两局对照：
//     人在原版出生点 → 两枚进共享页；人在小站旁 10 格 → 都不收；人从小站旁往回走 11 格 → 又收了。处理函数 main+0x6e0380 里格区、目标格、权限（0x6d01a0）、
//     物品版本门、「手上有物品就拒绝」都读数核过是通过的，卡在后面「从背包取出 0x64e620 / 放进仓库单位 0x648010」之一，具体哪条规则没读到（推断：只处理人所在 / 相邻房间里的仓库单位）。
//     所以不再挪：数「共享页里的 + 这个角色身上的」，不足 2 枚才造；背包满时留在脚边并提示一句。
//   包都交给游戏自己的收包分发 main+0x6e5b40(游戏, 玩家, 包, 长度)，前后置清「处理中」标志（游戏+0x19050），与 DM_TEST_GIVE_INV 同一做法。
// 落盘：游戏只在它自己的定时自动存盘里写共享仓库文件（建角后那次提交只写角色文件，关箱子也不写——mini 实测），
//   所以补完请它存一次（dm_request_save：把上次自动存盘时刻拨前，下一拍它自己存，角色与共享仓库各提交一次）。
//   「补过没有」以盘上为准：建角时把角色名记进欠条文件 dm_stash_topup.txt（随存档提交），补齐并等到存档提交后才划掉；
//   划欠条的条件 = 这一趟金币到数、该造的赦免之证都造出来了（不看它们最后在哪）；划掉之后这个角色永不再补。中途被杀则欠条还在，下次进局再补（补到定数，不会多给）。
// 改之前把盘上已提交的那份共享仓库文件拷到存档目录旁边的 stash_backup/（带时刻，每种留最近 5 份）。测试：DM_TEST_STASH_TOPUP=<拍> 对当前角色做一次。
// One-time top-up when a new Elysium character is created: if the shared stash gold totals less than 2.5 million, top it up to 2.5 million;
// if the Tokens of Absolution (those in the shared pages + those on this character) number fewer than 2, top up to 2, granted into this
// character's backpack; whatever is already there is left untouched.
// The game reads the shared stash file into memory at startup and never reads the file again, so the file is not modified; instead the in-game
// copy is changed after the new character first enters a run, and the game saves it itself:
//   Each shared stash page is a unit (type 0, in the same table as players, game+0x2230); the player inventory at +0x68 holds the linked list
//   of "stash units I can use": {+0 unit id, +9 usable, +0xa stacked-items page (gems / materials / runes), +0x10 next};
//   the 3.3 expansion has 5 shared pages (id 2–6) plus 1 stacked-items page (id 7). Gold per page = that unit's stat 15, capped at 2.5 million
//   (the UI's "maximum gold").
//   Gold: the original game's set-stat main+0x5bc1f0(unit, 15, value, 0), then the new value is sent to the client along the original game's
//   gold deposit/withdraw path (stash_gold_notify).
//   Token of Absolution: created with the original game's "drop one item by item code next to a unit" main+0x7c0b20(game, player, 'toa ',
//   quality 2), then a real pick-up-item packet 0x16 is submitted to put it into this character's backpack, where it stays.
//     Previously a second packet, "move into shared page" 0x55, was submitted to move it into a shared page -- the server only accepts that
//     packet while the player stands near the original spawn point: after the spawn position was moved next to the waypoint, for every newly
//     created Elysium character neither token could be moved in, "fewer than 2 counted in the shared pages" -> the pending-grant record was
//     never cleared -> every run created two more tokens and refilled whatever gold had been spent (a bug in the released build).
//     Two runs compared on the same save and the same build:
//     player at the original spawn point -> both tokens go into the shared page; player 10 tiles away next to the waypoint -> neither is
//     accepted; player walks 11 tiles back from the waypoint -> accepted again. In the handler main+0x6e0380, the grid, the target cell, the
//     permission check (0x6d01a0), the item version gate and "reject if an item is held on the cursor" were all verified by reading values to
//     pass; it gets stuck in one of the later steps "take out of backpack 0x64e620 / put into stash unit 0x648010", and which exact rule was
//     not read (inference: only stash units in the player's current / adjacent rooms are processed).
//     So it is no longer moved: count "those in the shared pages + those on this character" and create only if fewer than 2; when the
//     backpack is full the token stays at the player's feet and a one-line hint is shown.
//   All packets are handed to the game's own packet dispatcher main+0x6e5b40(game, player, packet, length), with the "processing" flag
//   (game+0x19050) set before and cleared after, the same approach as DM_TEST_GIVE_INV.
// Persistence: the game writes the shared stash file only in its own periodic autosave (the commit after character creation writes only the
//   character file, and closing the stash does not write it either -- measured on the second Mac),
//   so after the top-up it is asked to save once (dm_request_save: moves the last-autosave timestamp back so that it saves by itself on the
//   next tick, committing the character and the shared stash once each).
//   "Has it been topped up" is decided by what is on disk: at character creation the character name is recorded in the pending-grant file
//   dm_stash_topup.txt (committed with the save), and it is cleared only after the top-up is done and the save commit has completed;
//   the condition for clearing the record = on this pass the gold is at the target and every Token of Absolution that had to be created was
//   created (regardless of where they end up); once cleared, this character is never topped up again. If killed midway the record remains
//   and the top-up is redone on the next run (it tops up to the fixed amounts, never more).
// Before modifying, the committed on-disk shared stash file is copied to stash_backup/ next to the save directory (timestamped, the latest 5 of
// each kind are kept). Test: DM_TEST_STASH_TOPUP=<tick> performs it once for the current character.
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
const char *dm_save_commitdir(void);
#define STASH_GOLD 2500000
#define STASH_TOA 2
static void stash_backup(void) {
    const char *c = dm_save_commitdir(); if (!c) return; char dir[1100]; snprintf(dir, sizeof dir, "%s/../stash_backup", c); mkdir(dir, 0755);
    static const char *nm[2] = { "ModernSharedStashSoftCoreV2", "ModernSharedStashHardCoreV2" }; char stamp[32]; time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", &tm);
    for (int k = 0; k < 2; k++) { char src[1300], dst[1300]; snprintf(src, sizeof src, "%s/%s/%s.d2i", c, nm[k], nm[k]); snprintf(dst, sizeof dst, "%s/%s.%s.d2i", dir, nm[k], stamp);
        FILE *f = fopen(src, "rb"); if (!f) continue; FILE *o = fopen(dst, "wb"); char buf[4096]; size_t n, tot = 0; int ok = o != NULL;
        while (ok && (n = fread(buf, 1, sizeof buf, f)) > 0) { ok = fwrite(buf, 1, n, o) == n; tot += n; } fclose(f); if (o) ok = fclose(o) == 0 && ok;
        LOG("极乐世界：共享仓库补齐前备份 %s → %s（%zu 字节）%s", src, dst, tot, ok ? "" : "【红】备份写失败");
        char old[64][64]; int no = 0; DIR *dd = opendir(dir); struct dirent *e; size_t pl = strlen(nm[k]);   /* 每种只留最近 5 份：名字带时刻，字典序即时间序 | Keep only the latest 5 copies of each kind: the names carry the time, so lexicographic order is chronological order */
        while (dd && (e = readdir(dd))) if (no < 64 && !strncmp(e->d_name, nm[k], pl) && e->d_name[pl] == '.' && strlen(e->d_name) < 64) strlcpy(old[no++], e->d_name, 64);
        if (dd) closedir(dd);
        qsort(old, (size_t)no, 64, (int (*)(const void *, const void *))strcmp);
        for (int i = 0; i + 5 < no; i++) { char rm[1300]; snprintf(rm, sizeof rm, "%s/%s", dir, old[i]); remove(rm); } }
}
static uint64_t unit_inv(uint64_t u) { uint64_t a0 = *(uint64_t *)(u + 0xa0), i = a0 && *(int32_t *)(a0 + 8) && *(uint64_t *)(u + 0x98) ? *(uint64_t *)(u + 0x98) : *(uint64_t *)(u + 0x90); return i && *(uint32_t *)i == 0x01020304 ? i : 0; }
static int stash_pages(uint64_t G, uint64_t player, uint64_t pg[8], uint32_t id[8]) {   // 玩家能用的共享页单位，按链表次序 | Shared page units the player can use, in linked-list order
    uint64_t pi = unit_inv(player); int n = 0;
    for (uint64_t e = pi ? *(uint64_t *)(pi + 0x68) : 0; e && n < 8; e = *(uint64_t *)(e + 0x10)) { if (!*(uint8_t *)(e + 9) || *(uint8_t *)(e + 0xa)) continue;
        uint32_t i = *(uint32_t *)e; uint64_t u = *(uint64_t *)(G + 0x2230 + 8 * (i & 0x7f)); while (u && *(uint32_t *)(u + 8) != i) u = *(uint64_t *)(u + 0x158);
        if (u && *(uint32_t *)u == 0 && u != player && unit_inv(u)) { pg[n] = u; id[n++] = i; } }
    return n;
}
// 把某个仓库单位的金币新值告诉客户端：照原版存取金币处理函数（包 0x27，main+0x6d7fb0）改完属性后的那一段（0x6d8390–0x6d8484）——
// 连接对象 = [[玩家+0x10]+0xf0]，发送口 = 全局对象 main+0x2d7f3d8 的虚函数 +0x28(对象, 连接号 [连接], {包头, 包尾})；
// 先按它的条件补一个拍号包 2d [u32 连接+0x6c0]（连接 +0x6c4 记着上次发过的拍号），再发 20 [u32 属性号 15][u32 单位 id][u32 新值]。
// 只改服务端属性不发这个包，当局箱子界面上金币还是旧数（mini 实测显示 0），要等下次进局才对
// Tell the client the new gold value of a stash unit: follows the section (0x6d8390–0x6d8484) after the attribute change in the original game's deposit / withdraw gold handler (packet 0x27, main+0x6d7fb0) --
// connection object = [[player+0x10]+0xf0], send port = virtual function +0x28 of the global object main+0x2d7f3d8 (object, connection number [connection], {packet head, packet tail});
// first, under its condition, add a tick number packet 2d [u32 connection+0x6c0] (connection +0x6c4 records the tick number last sent), then send 20 [u32 stat number 15][u32 unit id][u32 new value].
// If only the server-side stat is changed without sending this packet, the gold in the stash UI still shows the old number for the current run (measured on the second Mac: shows 0) and is only right on the next run entry
static void stash_gold_notify(uint8_t *b, uint64_t G, uint64_t player, uint32_t uid, int value) {
    uint64_t pd = *(uint64_t *)(player + 0x10), cn = pd ? *(uint64_t *)(pd + 0xf0) : 0; if (!cn) return;
    uint64_t obj = (uint64_t)(uintptr_t)(b + 0x2d7f3d8); void (*send)(uint64_t, uint32_t, void *) = (void *)(uintptr_t)*(uint64_t *)(*(uint64_t *)obj + 0x28); struct { uint8_t *a, *z; } rg;
    uint32_t seq = *(uint32_t *)(cn + 0x6c0); *(uint32_t *)(G + 0x19050) = 1;
    if (*(uint32_t *)(cn + 0x6c4) != seq) { uint64_t g2 = *(uint64_t *)(cn + 0x2e0); int busy = g2 && *(uint32_t *)(g2 + 0x19050) != 0;
        if (!(*(int32_t *)(cn + 0x99c) >= 1 && *(uint8_t *)(cn + 0x9a4) && !busy)) { uint8_t p2[5] = { 0x2d }; memcpy(p2 + 1, &seq, 4); *(uint32_t *)(cn + 0x6c4) = seq; rg.a = p2; rg.z = p2 + 5; send(obj, *(uint32_t *)cn, &rg); } }
    uint8_t pk[13] = { 0x20 }; uint32_t st = 15; memcpy(pk + 1, &st, 4); memcpy(pk + 5, &uid, 4); memcpy(pk + 9, &value, 4); rg.a = pk; rg.z = pk + 13; send(obj, *(uint32_t *)cn, &rg);
    *(uint32_t *)(G + 0x19050) = 0;
}
static int inv_toa(uint8_t *b, uint64_t unit) {   // 这个单位的物品栏里有几枚赦免之证（玩家：背包 / 私人箱 / 魔盒 / 身上都在这一条链上） | how many Tokens of Absolution are in this unit's inventory (for a player: backpack / personal stash / Horadric Cube / equipped all sit on this one chain)
    uint64_t iv = unit_inv(unit); int c = 0, k = 0; if (!iv) return 0;
    for (uint64_t it = *(uint64_t *)(iv + 0x10); it && c < 600; c++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if ((((uint32_t (*)(uint64_t))(b + 0x570260))(it) & 0xffffff) == 0x616f74) k++; it = *(uint64_t *)(d + 0xb0); }
    return k;
}
// 高级仓库（宝石 / 材料 / 符文页，附属单位表里类 1 的那个单位）里的赦免之证：成叠存，一格算叠数（物品数据 +0x9c）。
// 一键存放会把赦免之证收进材料页，补齐不数这里就会当成不足、每次进局再发（同「落小站旁重发赦免之证」那类错）
// Tokens of Absolution in the advanced stash (gems / materials / runes pages, the class-1 unit in the attached unit table): stored as a stack, one cell counts as its stack count (item data +0x9c).
// One-tap deposit moves Tokens of Absolution into the materials page; if the top-up did not count them here it would judge them short and grant again on every game join (the same kind of bug as "re-granting Tokens of Absolution after spawning next to the waypoint")
static int adv_toa(uint8_t *b, uint64_t G, uint64_t player) {
    uint64_t pi = unit_inv(player); int k = 0;
    for (uint64_t e = pi ? *(uint64_t *)(pi + 0x68) : 0; e; e = *(uint64_t *)(e + 0x10)) { if (*(uint8_t *)(e + 0xa) != 1) continue;
        uint32_t i = *(uint32_t *)e; uint64_t u = *(uint64_t *)(G + 0x2230 + 8 * (i & 0x7f)); while (u && *(uint32_t *)(u + 8) != i) u = *(uint64_t *)(u + 0x158);
        uint64_t iv = u ? unit_inv(u) : 0; int c = 0;
        for (uint64_t it = iv ? *(uint64_t *)(iv + 0x10) : 0; it && c < 600; c++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break;
            if ((((uint32_t (*)(uint64_t))(b + 0x570260))(it) & 0xffffff) == 0x616f74) { int st = *(uint8_t *)(d + 0x9c); k += st > 0 ? st : 1; }
            it = *(uint64_t *)(d + 0xb0); } }
    return k;
}
static void stash_read(uint8_t *b, uint64_t pg[8], int n, long long *gold, int *toa) {
    *gold = 0; *toa = 0;
    for (int k = 0; k < n; k++) { *gold += ((int (*)(uint64_t, int, int))(b + 0x5b9290))(pg[k], 15, 0); *toa += inv_toa(b, pg[k]); }
}
void dm_elysium_stash_step(uint64_t G, uint64_t player, uint8_t *b) {
    static int st; static uint32_t at; static uint64_t made[STASH_TOA]; static int nmade, nwant; static int test = -1; uint32_t tq = *(uint32_t *)(G + 0x170);
    if (test < 0) { const char *e = getenv("DM_TEST_STASH_TOPUP"); test = e ? atoi(e) : 0; }
    if (test > 0 && tq >= (uint32_t)test) { test = 0; atomic_store(&g_topup, 1); }
    if (st == 0) { if (!atomic_exchange(&g_topup, 0)) return; st = 1; at = tq + 25; return; }   /* 进局后等 1 秒再动 | Wait 1 second after entering the run before acting */
    if (tq < at) return;
    uint64_t pg[8]; uint32_t id[8]; int n = stash_pages(G, player, pg, id); long long gold; int toa;
    void (*feed)(uint64_t, uint64_t, uint8_t *, uint32_t) = (void *)(b + 0x6e5b40);
    if (st == 1) { st = 0; nmade = 0;
        if (!n) { uint64_t pi = unit_inv(player); char o[400]; int c = 0; o[0] = 0;
            for (uint64_t e = pi ? *(uint64_t *)(pi + 0x68) : 0; e && c < 340; e = *(uint64_t *)(e + 0x10)) { uint32_t i = *(uint32_t *)e; uint64_t u = *(uint64_t *)(G + 0x2230 + 8 * (i & 0x7f)); while (u && *(uint32_t *)(u + 8) != i) u = *(uint64_t *)(u + 0x158);
                c += snprintf(o + c, sizeof o - c, " [id %u 标志 %d/%d/%d 单位%s 类型 %d 物品栏%s]", i, *(uint8_t *)(e + 8), *(uint8_t *)(e + 9), *(uint8_t *)(e + 0xa), u ? "有" : "无", u ? *(int32_t *)u : -1, u && unit_inv(u) ? "有" : "无"); }
            LOG("【红】极乐世界：共享仓库补齐——没找到玩家能用的共享页单位，不动（拍 %u，玩家物品栏%s，链表：%s）", tq, pi ? "有" : "无", o); return; }
        stash_backup(); stash_read(b, pg, n, &gold, &toa); int own = inv_toa(b, player), adv = adv_toa(b, G, player); long long need = STASH_GOLD - gold; int need_toa = STASH_TOA - toa - own - adv; nwant = need_toa > 0 ? need_toa : 0;
        LOG("极乐世界：共享仓库补齐——共享页 %d 个，现有金币合计 %lld、赦免之证 共享页 %d 枚 + 角色身上 %d 枚 + 材料页 %d 枚 → 要补金币 %lld、赦免之证 %d 枚（发到背包）", n, gold, toa, own, adv, need > 0 ? need : 0, nwant);
        for (int k = 0; k < n && need > 0; k++) { int g = ((int (*)(uint64_t, int, int))(b + 0x5b9290))(pg[k], 15, 0), room = STASH_GOLD - g; if (room <= 0) continue; int add = need < room ? (int)need : room;
            ((void (*)(uint64_t, int, int, int))(b + 0x5bc1f0))(pg[k], 15, g + add, 0); need -= add; stash_gold_notify(b, G, player, id[k], g + add); LOG("极乐世界：共享页 %d（单位 id %u）金币 %d → %d", k + 1, id[k], g, g + add); }
        for (int i = 0; i < need_toa && i < STASH_TOA; i++) { uint64_t u = ((uint64_t (*)(uint64_t, uint64_t, uint32_t, int))(b + 0x7c0b20))(G, player, 0x20616f74, 2); if (!u) { LOG("【红】极乐世界：赦免之证生成失败"); continue; }
            uint64_t ip = *(uint64_t *)(u + 0x38); uint8_t pk[17] = { 0x16 }; uint32_t one = 1; memcpy(pk + 1, (void *)(uintptr_t)(u + 8), 4); if (ip) { memcpy(pk + 5, (void *)(uintptr_t)(ip + 0x10), 4); memcpy(pk + 9, (void *)(uintptr_t)(ip + 0x14), 4); } memcpy(pk + 13, &one, 4);
            *(uint32_t *)(G + 0x19050) = 1; feed(G, player, pk, 17); *(uint32_t *)(G + 0x19050) = 0; made[nmade++] = u; }
        st = 3; at = tq + 25;
        return; }
    st = 0; if (!n) return; stash_read(b, pg, n, &gold, &toa);
    int own = inv_toa(b, player), ground = 0; for (int i = 0; i < nmade; i++) if (*(uint32_t *)(made[i] + 0xc) == 3) ground++;   /* 模式 3 = 还在地上（背包满，没捡进去） | mode 3 = still on the ground (inventory full, not picked up) */
    int ok = gold >= STASH_GOLD && nmade == nwant;
    LOG("极乐世界：共享仓库补齐后——金币合计 %lld、赦免之证 共享页 %d 枚 + 角色身上 %d 枚（本次发 %d 枚，其中 %d 枚背包放不下留在脚边）%s", gold, toa, own, nmade, ground, ok ? "" : "【红】没补齐");
    if (ground) { void dm_game_notice(uint8_t *, const char *); const char *dm_str(uint8_t *, const char *); dm_game_notice(b, dm_str(b, "dmElyToaGround")); }
    if (ok) atomic_store(&g_topup_done, 1);   // 金币到数、该发的赦免之证都造出来了就划欠条（不再以「数到 2 枚」为条件：数不到就永远重发）；金币没到数 / 物品没造出来才留欠条下次再试 | clear the pending-grant record once the gold is at the target and every Token of Absolution that was due has been created (no longer conditional on "counting 2 tokens": if the count never gets there it would re-grant forever); keep the record for a retry next time only if the gold is short or an item was not created
}

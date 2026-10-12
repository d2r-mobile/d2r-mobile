// 开背包自动补腰带（「双击腰带空格补满」点不准，改成开背包自动补；双击补满保留）。开关跟自定义页「自动拾取药水」（pot_pick）。
// 规则：
//   · 只在物品栏从关到开的那一下触发一次；开着期间不再补（不和玩家拖动抢）。
//   · 只动红（hp1–hp5）/ 蓝（mp1–mp5）/ 紫（rvs、rvl）；解毒 / 解冻 / 耐力和卷轴不进腰带。
//   · 每列认最底下那瓶的类别，从背包取同类往上补满（或背包没这类了为止）；最底下是别的东西（解毒、卷轴…）的列不动。
//   · 空列按「红、红、蓝、蓝」认领（第 1、2 列红，第 3、4 列蓝）；认领的颜色背包里没有就补紫；紫也没有，就让给背包里有的那种
//     （改：原先是「紫也没有就空着，不拿别的颜色顶」；服务端只肯把药放进最左空列，照旧规则会出现「有蓝、有空列、开背包不补」）。
//   · 同类先放大的：hp5 → hp1、mp5 → mp1、rvl → rvs。
// 放法 = 原版背包格「移到腰带」main+0x3c1cd0(物品, 玩家, 来源页, 1, {槽, 已定槽 = 1})：发 0x53 包。槽必须是原版腰带找位 0x561de0 对这瓶算出来的那个——
// 服务端收包时自己再找一次位，和包里的槽不相等就拒收（见 dm_beltauto_step 里的说明）。槽号 = 行 × 4 + 列。一次一瓶、等客户端看到它进了腰带（≤ 60 帧）再放下一瓶；没进的那瓶记红、本轮跳过。
// 腰带格数照原版找位里的算法：身上腰带（装备格 8）的腰带类型 0x5cc9b0（没腰带 = 2）→ 数据表（版本表 +0x1578，每行 0x120）+4 = 格数。
// Auto-fill the belt when the inventory opens ("double-tap an empty belt cell to fill" is hard to hit, so it became auto-fill on opening the inventory;
// double-tap fill is kept). The switch follows "Auto-pick potions" (pot_pick) on the Custom page.
// Rules:
//   - Triggers only once, at the moment the inventory goes from closed to open; no more filling while it stays open (does not fight with the player's
//     dragging).
//   - Only touches red (hp1–hp5) / blue (mp1–mp5) / purple (rvs, rvl); antidote / thawing / stamina potions and scrolls do not go into the belt.
//   - Each column takes the kind of its bottom potion and is filled upward with the same kind from the inventory (or until the inventory has none left);
//     a column whose bottom is something else (antidote, scroll...) is left alone.
//   - Empty columns are claimed as "red, red, blue, blue" (columns 1 and 2 red, columns 3 and 4 blue); if the claimed color is not in the inventory,
//     fill purple; if there is no purple either, give the column to whichever kind the inventory has
//     (changed: it used to be "no purple either, leave it empty, do not substitute another color"; the server only puts a potion into the leftmost empty
//     column, so the old rule produced "blue available, an empty column, opening the inventory fills nothing").
//   - Within a kind the larger goes first: hp5 → hp1, mp5 → mp1, rvl → rvs.
// Placement = the original inventory-cell "move to belt" main+0x3c1cd0(item, player, source page, 1, {slot, slot decided = 1}): sends packet 0x53. The
// slot must be the one the original belt slot finder 0x561de0 computes for this potion:
// on receiving the packet the server finds a slot again by itself and rejects the packet if it differs from the slot in the packet (see the note in
// dm_beltauto_step). Slot number = row × 4 + column. One potion at a time, waiting until the client sees it in the belt (≤ 60 frames) before placing the
// next; a potion that did not go in is logged as red and skipped for this round.
// Belt cell count follows the algorithm in the original finder: belt type 0x5cc9b0 of the equipped belt (equipment slot 8) (no belt = 2) → data table
// (version table +0x1578, 0x120 per row) +4 = cell count.
#include "hle.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int dm_custom_get(const char *key);
uint64_t dm_guest_frame(void);
enum { RED = 1, BLUE = 2, PURPLE = 3, OTHER = 4 };
static _Atomic int g_req;
static int g_inv_open;

void dm_beltauto_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息（diag.c 转来，游戏主线程）：物品栏从关到开记一次请求 | UI message (forwarded by diag.c, game main thread): record one request when the inventory panel goes from closed to open
    if (strcmp(cat, "PanelManager") || strcmp(arg, "PlayerInventoryExpansionLayout")) return;
    if (!strcmp(act, "OpenPanel") || !strcmp(act, "OpenExclusivePanel")) { if (!g_inv_open) atomic_store(&g_req, 1); g_inv_open = 1; }   // 重复的 Open 不算新的一次（面板消息不成对） | a repeated Open does not count as a new one (panel messages are not paired)
    else if (!strcmp(act, "ClosePanel") || !strcmp(act, "UnloadPanel")) g_inv_open = 0;
}

static uint64_t rd(uint64_t a) { return a ? *(uint64_t *)(uintptr_t)a : 0; }
static uint32_t code_of(uint8_t *b, uint64_t it) { return ((uint32_t (*)(uint64_t))(b + 0x570260))(it); }   // 物品码，小端 4 字节（'h' 'p' '5' ' '） | item code, 4 bytes little-endian ('h' 'p' '5' ' ')
static int kind_rank(uint32_t c, int *rank) {   // 类别 + 档位（大的数大） | kind + tier (larger ones have larger numbers)
    char s[4]; memcpy(s, &c, 4);
    if (s[0] == 'h' && s[1] == 'p' && s[2] >= '1' && s[2] <= '5') { *rank = s[2] - '0'; return RED; }
    if (s[0] == 'm' && s[1] == 'p' && s[2] >= '1' && s[2] <= '5') { *rank = s[2] - '0'; return BLUE; }
    if (s[0] == 'r' && s[1] == 'v' && (s[2] == 's' || s[2] == 'l')) { *rank = s[2] == 'l' ? 2 : 1; return PURPLE; }
    *rank = 0; return OTHER;
}
static const char *kind_name(int k) { return k == RED ? "红" : k == BLUE ? "蓝" : k == PURPLE ? "紫" : k == OTHER ? "别的" : "空"; }

typedef struct { uint64_t belt[16]; int boxes, nbelt; uint64_t best[4]; int n[4]; char bag[200]; } snap;   // nbelt = 腰带里的件数；bag = 背包药水清单（日志用）   // 腰带各格的物品；格数；背包里各类最大的那瓶与件数 | nbelt = number of items in the belt; bag = inventory potion list (for logging)   // items in each belt cell; cell count; the largest bottle of each kind in the inventory and the counts
// 放不进腰带的那几瓶（Issue：线上 诊断日志 29 次开背包都卡在同一瓶 hp5 上——客户端看它在背包、往空列放服务端不收；根因未定，等带下面诊断字段的线上日志）。
// 一瓶放不进不该让整轮停、下次还挑它：g_bad 记放失败过的（跨轮留着，挑药时排在没失败过的后面），g_tried 记本轮已经试过失败的（本轮不再挑）。
// The bottles that cannot be put into the belt (Reported case: in a diagnostic log from a released build, all 29 inventory opens got stuck on the same hp5 bottle -- the client sees it in the inventory, but the server does not accept placing it into an empty column; root cause undetermined, waiting for logs from released builds carrying the diagnostic fields below).
// One bottle that cannot be placed should not stop the whole round and get picked again next time: g_bad records the ones that failed to be placed before (kept across rounds, ranked after the never-failed ones when picking), g_tried records the ones already tried and failed this round (not picked again this round).
static uint32_t g_bad[8], g_tried[8]; static int g_nbad, g_ntried;
static int in_list(const uint32_t *l, int n, uint32_t id) { for (int i = 0; i < n && i < 8; i++) if (l[i] == id) return 1; return 0; }
static void item_where(uint64_t it, char *o, size_t cap) {   // 「容器 c 页 p 格 x,y」：物品 +0xc 方式（0 在容器里 / 2 腰带 / 3 地上…）、数据 +0x55 页、位置 +0x10 / +0x14 | 「容器 c 页 p 格 x,y」(container c page p cell x,y): item +0xc mode (0 in a container / 2 belt / 3 on the ground...), data +0x55 page, position +0x10 / +0x14
    uint64_t d = it ? *(uint64_t *)(uintptr_t)(it + 0x10) : 0, pp = it ? *(uint64_t *)(uintptr_t)(it + 0x38) : 0;
    if (!it || !d) { snprintf(o, cap, "没有这件"); return; }
    snprintf(o, cap, "容器 %u 页 %u 格 %d,%d", *(uint32_t *)(uintptr_t)(it + 0xc), *(uint8_t *)(uintptr_t)(d + 0x55), pp ? *(int32_t *)(uintptr_t)(pp + 0x10) : -1, pp ? *(int32_t *)(uintptr_t)(pp + 0x14) : -1);
}
static int take_snap(uint8_t *b, uint64_t pl, snap *S) {
    memset(S, 0, sizeof *S);
    uint64_t inv = rd(pl + 0x90); if (!inv || *(uint32_t *)(uintptr_t)inv != 0x01020304) return 0;
    uint64_t pages = rd(inv + 0x20); if (!pages || rd(inv + 0x28) < 2) return 0;
    uint64_t belt_item = rd(rd(pages + 0x18) + 0x40);   // 装备格 8 = 腰带 | equipment slot 8 = belt
    int type = belt_item ? ((int (*)(uint64_t))(b + 0x5cc9b0))(belt_item) : 2;
    uint8_t ver = *(uint8_t *)(uintptr_t)(pl + 0x1bd); if (ver < 1 || ver > 3) return 0;
    uint64_t T = rd((uint64_t)(uintptr_t)(b + 0x2d64a18 + ver * 16)), rows = T ? rd(T + 0x1578) : 0; if (!rows) return 0;
    S->boxes = *(uint8_t *)(uintptr_t)(rows + (uint64_t)type * 0x120 + 4); if (S->boxes > 16) S->boxes = 16;
    uint64_t bg = rd(pages + 0x20 + 0x18); for (int i = 0; i < S->boxes && bg; i++) { S->belt[i] = rd(bg + 8 * (uint64_t)i); S->nbelt += S->belt[i] != 0; }   // 页 1 = 腰带格 | page 1 = belt cells
    int brank[4] = { -1000, -1000, -1000, -1000 };
    for (uint64_t it = rd(inv + 0x10), k = 0; it && k < 1024; k++) { uint64_t d = rd(it + 0x10); if (!d) break;
        if (*(uint32_t *)(uintptr_t)(it + 0xc) == 0 && *(uint8_t *)(uintptr_t)(d + 0x55) == 0) { int r, kd = kind_rank(code_of(b, it), &r);   // 背包（页 0）里的 | the ones in the inventory (page 0)
            uint32_t iid = *(uint32_t *)(uintptr_t)(it + 8);
            if (kd != OTHER && !in_list(g_tried, g_ntried, iid) && r - (in_list(g_bad, g_nbad, iid) ? 100 : 0) > brank[kd]) { brank[kd] = r - (in_list(g_bad, g_nbad, iid) ? 100 : 0); S->best[kd] = it; }   // 同类先大的；放失败过的排到最后，本轮试过的不再挑 | within the same kind, larger ones first; ones that failed to be placed before are ranked last, and ones tried this round are not picked again
            if (kd != OTHER) { S->n[kd]++; size_t L = strlen(S->bag); uint32_t cd = code_of(b, it); if (L < sizeof S->bag - 6) snprintf(S->bag + L, sizeof S->bag - L, "%s%.3s", L ? "," : "", (char *)&cd); } }
        it = rd(d + 0xb0); }
    return 1;
}
static void belt_text(uint8_t *b, const snap *S, char *o, size_t cap) {   // 「列1 红红_ 列2 …」从下往上 | 「列1 红红_ 列2 …」(col 1 red red _ col 2 ...) from bottom to top
    size_t k = 0; o[0] = 0;
    for (int c = 0; c < 4; c++) { k += (size_t)snprintf(o + k, k < cap ? cap - k : 0, "%s列%d ", c ? " " : "", c + 1);
        for (int r = 0; r * 4 + c < S->boxes; r++) { uint64_t it = S->belt[r * 4 + c]; int rk, kd = it ? kind_rank(code_of(b, it), &rk) : 0;
            uint32_t cd = it ? code_of(b, it) : 0; (void)kd; k += (size_t)snprintf(o + k, k < cap ? cap - k : 0, "%s%.3s", r ? "," : "", it ? (char *)&cd : "_"); } }
}

void dm_beltauto_step(uint8_t *b) {   // 游戏主线程每帧（dm_world.c） | every frame on the game main thread (dm_world.c)
    static int active, moves, want_slot = -1; static uint64_t next, deadline; static uint32_t want_id; static char want_cli[64]; static int fails;   // fails = 本轮放失败的瓶数（moves 含它们） | fails = number of bottles that failed to be placed this round (moves includes them)
    if (!b) return;
    // 测试开关 DM_TEST_BELT_DRINK=<帧>:<列号…>（造现场：如 120:134 = 第 120 帧起把第 1、3、4 列的药喝光；写 0 = 背包里的红 / 蓝也喝光）：每 15 帧对这些列各喝最底下一瓶（dm_belt_use，原版喝药路径），喝空为止
    // Test switch DM_TEST_BELT_DRINK=<frame>:<column numbers...> (sets up a scenario: e.g. 120:134 = starting at frame 120 drink up the potions in columns 1, 3 and 4; writing 0 = also drink up the red / blue ones in the inventory): every 15 frames drink the bottom-most bottle of each of these columns (dm_belt_use, the original game's potion-drinking path), until empty
    { static int st = -1, at; static char cols[8]; static uint64_t nx; void dm_belt_use(int col, int row);
      if (st < 0) { const char *e = getenv("DM_TEST_BELT_DRINK"); st = e && sscanf(e, "%d:%7s", &at, cols) == 2 ? 0 : 2; }
      uint64_t f = dm_guest_frame(); if (!st && f >= (uint64_t)at && f >= nx) { nx = f + 15; snap D; uint64_t p = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(uintptr_t)(b + 0x2d20e4c)); int any = 0;
          if (p && take_snap(b, p, &D)) for (const char *q = cols; *q; q++) { int c = *q - '1';
              if (*q == '0' && (D.best[RED] || D.best[BLUE])) { ((void (*)(uint64_t, int))(b + 0x3c0098))(D.best[RED] ? D.best[RED] : D.best[BLUE], 3); any = 1; break; }   // 0 = 背包里的红 / 蓝也喝掉（原版背包格使用物品，同 dm_actions.c） | 0 = also drink up the red / blue ones in the inventory (the original game's inventory cell item use, same as dm_actions.c)
              if (c >= 0 && c < 4 && D.belt[c]) { dm_belt_use(c, 0); any = 1; break; } }
          if (!any) { st = 2; LOG("DM_TEST_BELT_DRINK：第 %s 列喝空了（帧 %llu）", cols, (unsigned long long)f); } } }
    if (atomic_exchange(&g_req, 0)) {
        static int env = -2; if (env == -2) { const char *e = getenv("DM_POT_PICK"); env = e ? e[0] != '0' : -1; }   // 同药水自动拾取：测试强制开关优先于自定义页 | same as potion auto-pickup: the test force switch takes priority over the custom settings page
        if (env >= 0 ? env : dm_custom_get("pot_pick")) { active = 1; moves = 0; want_slot = -1; next = 0; deadline = 0; g_ntried = 0; fails = 0; }
        else LOG("开背包补腰带：开关关着，不动");
    }
    if (!active) return;
    uint64_t fr = dm_guest_frame(); if (fr < next) return;
    uint64_t pl = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(uintptr_t)(b + 0x2d20e4c)); snap S; char t[160];
    if (!pl || !take_snap(b, pl, &S)) { active = 0; LOG("开背包补腰带：读不到玩家 / 物品栏，不动"); return; }
    if (rd(rd(pl + 0x90) + 0x40)) { active = 0; LOG("开背包补腰带：手上拿着东西，停（已放 %d 瓶）", moves); return; }
    int dm_ui_panel_shown(const char *name);
    if (!moves && want_slot < 0 && !dm_ui_panel_shown("PlayerInventoryExpansionLayout")) { if (!deadline) deadline = fr + 60; if (fr < deadline) return; active = 0; LOG("开背包补腰带：物品栏 60 帧没显示出来，不补"); return; }   // 等物品栏真显示出来再看同时开着的面板：底座 / 方块的开面板消息先到，但面板树里要晚几帧才可见 | wait until the inventory panel is really shown before looking at panels open at the same time: the pedestal / cube open-panel message arrives first, but it only becomes visible in the panel tree a few frames later
    if (dm_ui_panel_shown("ImbueItemsPanel") || dm_ui_panel_shown("HoradricCubeLayout")) { active = 0; LOG("开背包补腰带：开着底座 / 方块（物品栏随它一起开），不补（已放 %d 瓶）", moves); return; }   // 原版此时不收进腰带：0x3c1b28 照样放行，但发出的 0x53 不生效（dz 局，第 2 瓶 60 帧没进） | the original game does not accept items into the belt at this time: 0x3c1b28 still lets it through, but the 0x53 sent has no effect (dz run, the 2nd bottle did not go in within 60 frames)
    if (want_slot >= 0 && !S.belt[want_slot]) { if (fr < deadline) return;
        uint64_t dm_diag_srv_item(uint32_t id); char srv[64], now[64]; item_where(dm_diag_srv_item(want_id), srv, sizeof srv);
        { uint64_t ci = ((uint64_t (*)(uint64_t, uint32_t, uint32_t, uint32_t))(b + 0x45f5f0))((uint64_t)(uintptr_t)(b + 0x2d603a0 + 4 * 0x400), want_id & 0x7f, want_id, 4); item_where(ci, now, sizeof now); }
        LOG("【红】开背包补腰带：第 %d 瓶放进槽 %d 后 60 帧没见进腰带——物品 id %u，放之前客户端：%s；现在客户端：%s；服务端：%s。本轮跳过它，换下一瓶", moves, want_slot, want_id, want_cli, now, srv);
        if (g_ntried < 8) g_tried[g_ntried++] = want_id; if (!in_list(g_bad, g_nbad, want_id)) { if (g_nbad == 8) g_nbad = 7; g_bad[g_nbad++] = want_id; }
        fails++; want_slot = -1; if (!take_snap(b, pl, &S)) { active = 0; return; } }   // 重新挑（排除了刚失败的这瓶） | pick again (excluding the bottle that just failed)
    else if (want_slot >= 0 && in_list(g_bad, g_nbad, want_id)) { for (int i = 0; i < g_nbad; i++) if (g_bad[i] == want_id) { g_bad[i] = g_bad[--g_nbad]; break; } LOG("开背包补腰带：物品 id %u 上次放不进，这次进了，从失败名单拿掉", want_id); }
    if (!moves && want_slot < 0) { belt_text(b, &S, t, sizeof t); LOG("开背包补腰带：开始，腰带 %d 格（%s），背包 红 %d 蓝 %d 紫 %d（%s），药水合计 %d", S.boxes, t, S.n[RED], S.n[BLUE], S.n[PURPLE], S.bag, S.nbelt + S.n[RED] + S.n[BLUE] + S.n[PURPLE]); }
    want_slot = -1;
    // 落在哪个槽不由我们定：服务端收「移到腰带」包（0x53，main+0x6dfaf0）时自己用原版腰带找位 0x561de0 再算一遍，包里的槽和它算的不相等就整包拒收
    // （0x6dfd7c–0x6dfdc8）。原版找位的次序：第 1–4 列里底格与这瓶同类、还有空格的那一列（从左到右）→ 该列最低空格；都没有 → 最左边的空列底格。
    // 之前这里按「从左到右逐列」自己挑槽：左边有空列、右边有同类未满列时，我们去填空列、原版要放同类列，槽对不上 → 每次都被拒
    // （线上诊断日志 29 次都是 hp5 往槽 0 放：第 1、2 列空，第 4 列 hp4,hp4 还有空位）。现在先问原版找位这瓶会落在哪，我们的规则只决定这瓶放不放：
    // 落在有药的列（原版保证同类）→ 放；落在空列 → 该列认领的颜色（红红蓝蓝）就是它、或它是紫而认领色背包里没有 → 放；
    // 认领色和紫背包里都没有 → 这一列让给背包里有的那种（认：原版找位只肯给最左空列，指定不了列——「手上 → 腰带指定格」0x23 实测会把药丢地上；
    // 那一列空着也是空着，玩家手动「移到腰带」得到的也是这个落点）。
    // We do not decide which slot it lands in: when the server receives the "move to belt" packet (0x53, main+0x6dfaf0) it recomputes with the original belt
    // slot finder 0x561de0, and rejects the whole packet if the slot in the packet differs from its own result
    // (0x6dfd7c–0x6dfdc8). Order of the original finder: among columns 1 to 4, a column whose bottom cell holds the same kind as this potion and that still
    // has an empty cell (left to right) → the lowest empty cell of that column; if none → the bottom cell of the leftmost empty column.
    // Previously this code picked the slot itself "column by column, left to right": with an empty column on the left and a non-full same-kind column on the
    // right, we filled the empty column while the original wanted the same-kind column, so the slots mismatched → rejected every time
    // (all 29 entries in the diagnostic log from a released build were hp5 going to slot 0: columns 1 and 2 empty, column 4 holding hp4,hp4 with room left). Now we
    // first ask the original finder where this potion would land, and our rules only decide whether to place it:
    // lands in a column that has potions (the original guarantees same kind) → place; lands in an empty column → place if that column's claimed color (red
    // red blue blue) is this potion's, or if it is purple and the claimed color is not in the inventory;
    // neither the claimed color nor purple is in the inventory → the column is given to whichever kind the inventory has (accepted: the original finder only
    // offers the leftmost empty column and a column cannot be specified; "cursor → specific belt cell" 0x23 was measured to drop the potion on the ground;
    // the column would otherwise just sit empty, and a player's manual "move to belt" gets this same landing slot).
    uint64_t inv_c = rd(pl + 0x90);
    static const int order[3] = { RED, BLUE, PURPLE };
    for (int oi = 0; oi < 3; oi++) {
        int kd = order[oi]; uint64_t it = S.best[kd]; uint32_t ns = 0; if (!it) continue;
        if (!((int (*)(uint64_t, uint64_t, uint32_t *, int))(b + 0x561de0))(inv_c, it, &ns, 1) || (int)ns >= S.boxes) continue;   // 原版说这瓶进不了腰带（没有同类空位、也没有空列） | The original says this potion cannot go into the belt (no free slot of the same kind and no empty column)
        int c = (int)ns % 4, r = (int)ns / 4;
        if (!r) { int want = c < 2 ? RED : BLUE;   // 空列：先认领色；认领色背包里没有 → 紫；紫也没有 → 让给背包里有的那种 | Empty column: claimed color first; claimed color not in the inventory → purple; no purple either → give the column to whichever kind the inventory has
            if (kd != want && S.best[want]) continue;                       // 认领色有货：别的颜色不顶 | Claimed color in stock: other colors do not substitute
            if (kd != want && kd != PURPLE && S.best[PURPLE]) continue; }   // 认领色没货但有紫：先补紫 | Claimed color out of stock but purple available: fill purple first
        if (!(((int (*)(uint64_t))(b + 0x3c1b28))(it) & 1)) { active = 0; LOG("开背包补腰带：原版此刻不收这瓶进腰带（如开着底座 / 交易面板），停（已放 %d 瓶）", moves - fails); return; }   // 同双击补满的前置：原版判这件此刻能进腰带（dz：底座面板开着时第 2 瓶原版不收，等满 60 帧记了红） | same precondition as double-tap fill: the original game judges that this item can go into the belt right now (dz: with the pedestal panel open the original game did not accept the 2nd bottle, and a failure line was logged after waiting the full 60 frames)
        int slot = (int)ns; struct { uint32_t slot; uint8_t fixed; } tgt = { (uint32_t)slot, 1 };
        static int t_noput = -1; if (t_noput < 0) t_noput = getenv("DM_TEST_BELT_NOPUT") != NULL;   // 测试开关（造「这瓶服务端不收」的现场）：进程里挑中的第一瓶不真发，看失败后换下一瓶与诊断 | test switch (sets up the "the server does not accept this bottle" scenario): the first bottle picked in the process is not actually sent, to observe switching to the next bottle after the failure and the diagnostics
        if (t_noput == 1) { t_noput = 2; LOG("测试 DM_TEST_BELT_NOPUT：这一瓶不真发"); }
        else ((void (*)(uint64_t, uint64_t, uint8_t, int, void *))(b + 0x3c1cd0))(it, pl, *(uint8_t *)(uintptr_t)(rd(it + 0x10) + 0x55), 1, &tgt);
        moves++; want_slot = slot; next = fr + 6; deadline = fr + 60; want_id = *(uint32_t *)(uintptr_t)(it + 8); item_where(it, want_cli, sizeof want_cli);
        LOG("开背包补腰带：背包「%.4s」→ 列 %d 第 %d 格（槽 %d，%s列）；物品 id %u，%s", (char *)&(uint32_t){ code_of(b, it) }, c + 1, r + 1, slot, r ? "同类" : "空", want_id, want_cli);
        return;
    }
    active = 0; belt_text(b, &S, t, sizeof t);
    LOG("开背包补腰带：完成，放了 %d 瓶；腰带（%s），背包剩 红 %d 蓝 %d 紫 %d（%s），药水合计 %d", moves - fails, t, S.n[RED], S.n[BLUE], S.n[PURPLE], S.bag, S.nbelt + S.n[RED] + S.n[BLUE] + S.n[PURPLE]);
}

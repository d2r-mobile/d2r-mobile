// 「光标下物品是什么」（长按药水 = 放进腰带，要先知道指着的是什么）。全部走游戏自己的函数与表，地址为 3.3.94012 原映像偏移：
//   界面根 [main+0x2f05458] → 焦点管理 [+0xd0] → [+0x190] 手柄焦点控件 / [+0x178] 指针悬停控件；
//   main+0x3cc87c(物品格控件, NULL) = 该控件里光标指着的客户端物品单位（底部按键提示函数 0x3cf578 在 0x3cf640 调它；多格物品每格存同一指针）。
//   它按控件虚表取格、取物，所以先验虚表是三种已认出的物品格之一：多格格子 0x2089ab8（背包 / 箱子 / 方块 / 商店 / 交易）、腰带 0x2088fc8、装备位 0x2089fa0、高级仓库成叠格 0x20892f8（一格一种物品，物品指针直接在控件 +0x608）。
//   格子控件：[+0x5c8] 属主单位类型（1 = NPC → 商店）、[+0x630] 页（0 背包、1 / 2 交易、3 方块、4 大箱子）。
// 'What is the item under the cursor' (long-press on a potion = put it into the belt, which first needs to know what is being pointed at). Everything goes through the game's own functions and tables; addresses are offsets in the original 3.3.94012 image:
//   UI root [main+0x2f05458] -> focus manager [+0xd0] -> [+0x190] controller focus widget / [+0x178] pointer hover widget;
//   main+0x3cc87c(item cell widget, NULL) = the client item unit the cursor points at in that widget (the bottom key hint function 0x3cf578 calls it at 0x3cf640; every cell of a multi-cell item stores the same pointer).
//   It fetches the cell and the item via the widget vtable, so first verify that the vtable is one of the three recognized item cell kinds: multi-cell grid 0x2089ab8 (inventory / stash / cube / shop / trade), belt 0x2088fc8, equipment slot 0x2089fa0, advanced stash stacked cell 0x20892f8 (one kind of item per cell, the item pointer is directly at widget +0x608).
//   Grid widget: [+0x5c8] owner unit type (1 = NPC -> shop), [+0x630] page (0 inventory, 1 / 2 trade, 3 cube, 4 big stash).
#include "dm_cursor_item.h"
#include "hle.h"
#include "dm.h"
#include <string.h>

static uint8_t *main_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static void item_size(uint8_t *b, uint64_t item, int *w, int *h) {
    uint8_t ver = *(uint8_t *)(item + 0x1bd); uint64_t t = ver >= 1 && ver <= 3 ? *(uint64_t *)(b + 0x2d64a18 + (uint64_t)ver * 16) : 0; uint32_t ci = *(uint32_t *)(item + 4);
    if (t && ci < *(uint32_t *)(t + 0x15c8)) { uint64_t rec = *(uint64_t *)(t + 0x15c0) + (uint64_t)ci * 0x1c0; *w = *(uint8_t *)(rec + 0x11e); *h = *(uint8_t *)(rec + 0x11f); }
}

int dm_cursor_item(dm_cursor_item_info *out) {
    uint8_t *b = main_base();
    if (!b || !out) return 0;
    memset(out, 0, sizeof *out);
    uint64_t pm = *(uint64_t *)(b + 0x2f05458), fm = pm ? *(uint64_t *)(pm + 0xd0) : 0, item = 0, w = 0, vt = 0; if (!fm) return 0;
    const uint64_t cand[2] = { *(uint64_t *)(fm + 0x190), *(uint64_t *)(fm + 0x178) };
    for (int i = 0; i < 2 && !item; i++) {
        if (!(w = cand[i])) continue;
        vt = *(uint64_t *)w - (uint64_t)(uintptr_t)b;
        if (vt != 0x2089ab8 && vt != 0x2088fc8 && vt != 0x2089fa0 && vt != 0x20892f8) continue;   // 0x20892f8 = 高级仓库（宝石 / 材料 / 符文页）的成叠格 | 0x20892f8 = stacked cell of the advanced stash (gem / material / rune pages)
        item = vt == 0x20892f8 ? *(uint64_t *)(w + 0x608) : ((uint64_t (*)(uint64_t, void *))(b + 0x3cc87c))(w, NULL);
        if (item && *(uint32_t *)item != 4) item = 0;
    }
    if (!item) return 0;
    out->id = *(uint32_t *)(item + 8); out->cls = *(uint32_t *)(item + 4);
    uint32_t code = ((uint32_t (*)(uint64_t))(b + 0x570260))(item);
    for (int i = 0; i < 3; i++) { char c = (char)(code >> (8 * i)); out->code[i] = c == ' ' ? 0 : c; }
    if (vt == 0x2088fc8) out->container = DM_ITEM_AT_BELT;
    else if (vt == 0x2089fa0) out->container = DM_ITEM_AT_EQUIP;
    else if (vt == 0x20892f8) out->container = DM_ITEM_AT_STASH;
    else if (*(uint32_t *)(w + 0x5c8) == 1) out->container = DM_ITEM_AT_SHOP;
    else switch (*(uint8_t *)(w + 0x630)) { case 0: out->container = DM_ITEM_AT_INVENTORY; break; case 3: out->container = DM_ITEM_AT_CUBE; break; case 4: out->container = DM_ITEM_AT_STASH; break; default: out->container = DM_ITEM_AT_TRADE; }
    int (*is_type)(uint64_t, int) = (void *)(b + 0x574bc0);   // 类型号 = 物品类型表行号：9 药水（实测 hp5 属于 0x9 / 0x34 / 0x4d：药水 / 杂物 / 治疗药水）、0x16 卷轴、0x12 书 | Type number = row number in the item type table: 9 potion (measured: hp5 belongs to 0x9 / 0x34 / 0x4d: potion / miscellaneous / healing potion), 0x16 scroll, 0x12 book
    out->potion = is_type(item, 9) != 0; out->scroll = is_type(item, 0x16) != 0; out->book = is_type(item, 0x12) != 0;
    // 类型上可放腰带：物品表记录（步长 0x1c0）+0x12e 主类型 → 类型表（步长 0xe8）+0x17，与 main+0x3c1b28 前半段同一读法
    // Beltable by type: item table record (stride 0x1c0) +0x12e main type -> type table (stride 0xe8) +0x17, read the same way as the first half of main+0x3c1b28
    uint8_t ver = *(uint8_t *)(item + 0x1bd);
    if (ver >= 1 && ver <= 3) {
        uint64_t t = *(uint64_t *)(b + 0x2d64a18 + (uint64_t)ver * 16); uint64_t recs = *(uint64_t *)(t + 0x15c0), types = *(uint64_t *)(t + 0x1368);
        if (recs && types && out->cls < *(uint32_t *)(t + 0x15c8)) { int16_t ty = *(int16_t *)(recs + (uint64_t)out->cls * 0x1c0 + 0x12e); if (ty >= 0 && (uint64_t)ty < *(uint64_t *)(t + 0x1370)) out->beltable = *(uint8_t *)(types + (uint64_t)ty * 0xe8 + 0x17) != 0; }
    }
    out->belt_room = ((int (*)(uint64_t))(b + 0x3c1b28))(item) & 1;
    uint64_t player = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(b + 0x2d20e4c));
    if (player) { uint32_t slot = 0; out->equippable = ((int (*)(uint64_t, uint64_t, uint32_t *, int, int))(b + 0x583b60))(player, item, &slot, 1, 0) && ((int (*)(uint64_t, uint32_t, uint64_t, int, int))(b + 0x565950))(player, slot, item, 1, 0); }
    // 格位：[物品+0x38] → +0x10 / +0x14；占格：物品表记录 +0x11e 宽、+0x11f 高（与 dm_actions.c 商人货架读的同一处）
    // Cell position: [item+0x38] -> +0x10 / +0x14; cells occupied: item table record +0x11e width, +0x11f height (the same place dm_actions.c reads for the vendor shelf)
    { uint64_t d = *(uint64_t *)(item + 0x10); out->stack = d ? *(uint8_t *)(d + 0x9c) : 0; out->unid = d && !(*(uint32_t *)(d + 0x18) & 0x10); }
    out->gw = out->gh = 1;
    if (out->container != DM_ITEM_AT_EQUIP) { uint64_t pp = *(uint64_t *)(item + 0x38); if (pp) { out->gx = *(int32_t *)(pp + 0x10); out->gy = out->container == DM_ITEM_AT_BELT ? 0 : *(int32_t *)(pp + 0x14); } }
    if (out->container != DM_ITEM_AT_EQUIP && out->container != DM_ITEM_AT_BELT) item_size(b, item, &out->gw, &out->gh);
    return 1;
}

// 手上拿着的 = 玩家物品栏 +0x40（与 dm_actions.c 同）。光标状态 [main+0x2d12928] == 5 是「用了物品等目标」（main+0x471510 进入：待用物品记在 0x2d12920、目标类型记在 0x2d12930），那时物品不在手上
// Held in hand = player inventory +0x40 (same as dm_actions.c). Cursor state [main+0x2d12928] == 5 is 'item used, waiting for a target' (entered at main+0x471510: the item to be used is recorded at 0x2d12920 and the target type at 0x2d12930); the item is not in hand then
int dm_held_size(int *w, int *h) {
    uint8_t *b = main_base(); if (!b) return 0;
    uint64_t pl = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(b + 0x2d20e4c)), inv = pl ? *(uint64_t *)(pl + 0x90) : 0, held = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x40) : 0;
    if (!held || *(uint32_t *)held != 4) return 0;
    int gw = 1, gh = 1; item_size(b, held, &gw, &gh); if (w) *w = gw; if (h) *h = gh; return 1;
}

// 手上那件此刻有没有「待确认的操作」（main+0x474990，原版放回背包 / 装备前判的同一个前置；dm_actions.c 收进背包也判它）：
// 1 = 上一个指令服务端还没答完，这时再对它按确认键会落空或落到别处。只许游戏主线程调
// Whether the item in hand has an 'operation pending confirmation' right now (main+0x474990, the same precondition the original game checks before putting back into the inventory / equipping; stowing into the inventory in dm_actions.c checks it too):
// 1 = the server has not finished answering the previous command, and pressing the confirm button on it now would miss or land somewhere else. May only be called from the game main thread
int dm_held_pending(void) {
    uint8_t *b = main_base(); if (!b) return 0;
    uint64_t pl = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(b + 0x2d20e4c)), inv = pl ? *(uint64_t *)(pl + 0x90) : 0, held = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x40) : 0;
    if (!held || *(uint32_t *)held != 4) return 0;
    return ((int (*)(uint64_t))(b + 0x474990))(held) & 1;
}

// 商人面板的光标模式：界面属性表 [main+0x2f05450] 键 0x838e27f92c679132 的值（0 空闲 / 1 买 / 2 卖 / 3 修理 / 4 刚成交；商人面板消息处理 main+0x408a4c 的
// Repair 分支 0x408eb0 写 3，复位 0x40a8ec 写 0）。查表照抄物品格点击处理 0x3b3cf0–0x3b3d48、0x3b3f74–0x3b3f7c。修理态 = 值 3 且交易面板开着（[0x2d5ff53]，0x3b3f84 同判）。只许游戏主线程调
// Cursor mode of the vendor panel: value of key 0x838e27f92c679132 in the UI property table [main+0x2f05450] (0 idle / 1 buy / 2 sell / 3 repair / 4 just traded; in the vendor panel message handler main+0x408a4c the
// Repair branch 0x408eb0 writes 3, and the reset 0x40a8ec writes 0). The table lookup is copied from the item cell click handler 0x3b3cf0–0x3b3d48, 0x3b3f74–0x3b3f7c. Repair state = value 3 and the trade panel is open ([0x2d5ff53], same check as 0x3b3f84). May only be called from the game main thread
int dm_vendor_repair_mode(void) {
    uint8_t *b = main_base(); if (!b || !*(uint8_t *)(b + 0x2d5ff53)) return 0;
    uint64_t t = *(uint64_t *)(b + 0x2f05450), bk = t ? *(uint64_t *)(t + 8) : 0; if (!bk) return 0;
    for (uint64_t n = *(uint64_t *)(bk + ((*(uint64_t *)t - 1) & 0x0cfa4cd5c8419dedull) * 8); n; n = *(uint64_t *)n)
        if (*(uint64_t *)(n + 8) == 0x838e27f92c679132ull) return *(uint32_t *)*(uint64_t *)(*(uint64_t *)(n + 0x10) + 0x10) == 3;
    return 0;
}
int dm_item_targeting(void) {
    uint8_t *b = main_base(); if (!b) return 0;
    return *(uint32_t *)(b + 0x2d12928) == 5 && *(uint64_t *)(b + 0x2d12920) ? (int)*(uint32_t *)(b + 0x2d12930) + 1 : 0;
}

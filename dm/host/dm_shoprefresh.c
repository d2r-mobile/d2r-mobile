// 商人交易页的「刷新货架」（不用反复进出游戏刷商店）。只调原版的货单机制，不自己造物品、不改货单规则。
// 读码依据（主程序 3.3.94012）：
//   打开交易 / 赌博都是客户端包 0x38 [u32 类型][u32 商人单位号]（1 = 交易，2 = 赌博），服务端处理 0x6db140：先 0x7f3c40 记下交易状态
//   （玩家数据 +0x100 = 2 交易 / 3 赌博，+0xf8 = 商人单位号），再进 0x7ea4d0 开店。
//   每个商人一条店铺记录：游戏 +0x3838 起，条数在 +0x3840，每条 0x78 字节，+0 = 商人类号（u16），+0x34 = 货单已生成，+0x35 = 待刷新。
//   0x7ea4d0：交易页打开时若 +0x35 已置（且这个商人没有第二个玩家在交易，0x688710 ≤ 1），先 0x7f3ad0 清空旧货、再 0x7e9b70 重新生成——
//   与第一次打开用的是同一个生成函数。原版自己置 +0x35 的地方是换幕回城刷新 0x7f3630（有人正在交易就只置标志，等下次开店再刷）。
//   赌博页「刷新」（0x408e78）就是客户端再发一次 0x38 类型 2；不扣钱（金币只在买的时候扣）、客户端与服务端都没有冷却。
// 做法：按钮（touch_layout.py 交易页右格，借原版面板按名字管显示的两颗控件 button_repair / button_blank，见那里的注释）发空操作消息 PanelManager:ClosePanel:TouchShopRefresh →
//   客户端主线程照 0x408e78 的写法发 0x38 类型 1，并记下要刷的商人；服务端命令分派入口（观测门 OBS_C2S）认出这一包，把该商人店铺记录的 +0x35 置 1，
//   原版处理照常往下走：清空、重生成，删货 / 加货的包也由原版函数发给客户端。单人游戏里客户端与服务端在同一进程，用一个原子量交接
// 「刷新货架」(Refresh Stock) on the vendor trade page (no need to keep leaving and re-entering the game to refresh the shop). Only invokes the original game's stock list mechanism; does not create items itself or change stock rules.
// Basis from reading the code (main program 3.3.94012):
//   Opening trade / gambling are both client packet 0x38 [u32 type][u32 vendor unit id] (1 = trade, 2 = gambling), handled on the server by 0x6db140: first 0x7f3c40 records the trade state
//   (player data +0x100 = 2 trade / 3 gambling, +0xf8 = vendor unit id), then enters 0x7ea4d0 to open the shop.
//   One shop record per vendor: starting at game +0x3838, count at +0x3840, 0x78 bytes each, +0 = vendor class id (u16), +0x34 = stock list generated, +0x35 = pending refresh.
//   0x7ea4d0: when the trade page opens, if +0x35 is set (and no second player is trading with this vendor, 0x688710 ≤ 1), first 0x7f3ad0 clears the old stock, then 0x7e9b70 regenerates it -
//   the same generation function used on the first open. The place where the original game itself sets +0x35 is the act-change return-to-town refresh 0x7f3630 (if someone is trading it only sets the flag and refreshes on the next shop open).
//   The gambling page's 「刷新」(Refresh) (0x408e78) is just the client sending 0x38 type 2 again; no gold is charged (gold is only deducted on purchase), and neither client nor server has a cooldown.
// Approach: the button (right cell of the trade page in touch_layout.py, borrowing two widgets whose visibility the original panel manages by name, button_repair / button_blank, see the comment there) sends the no-op message PanelManager:ClosePanel:TouchShopRefresh →
//   the client main thread sends 0x38 type 1 the way 0x408e78 does and records the vendor to refresh; the server command dispatch entry (observation gate OBS_C2S) recognizes this packet and sets +0x35 of that vendor's shop record to 1,
//   and the original handling proceeds as usual: clear, regenerate, with the remove-stock / add-stock packets also sent to the client by the original functions. In single-player the client and server are in the same process, handed over through one atomic
#include "hle.h"
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

static _Atomic uint32_t g_req;   // 客户端已发「刷新」包、等服务端认领的商人单位号（0 = 没有） | Unit id of the vendor for which the client has sent the "refresh" packet and is waiting for the server to claim it (0 = none)

void dm_shoprefresh_on_uimsg(const char *cat, const char *act, const char *arg, uint8_t *b) {   // b = 主程序装载基址（diag.c 给） | b = main program load base (given by diag.c)
    if (strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel") || strcmp(arg, "TouchShopRefresh")) return;
    if (!b) return;
    uint32_t npc = *(volatile uint32_t *)(b + 0x2d5ebfc);   // 客户端当前交易的商人单位号（0x408e84 同一处） | Unit id of the vendor the client is currently trading with (same place as 0x408e84)
    if (!npc) { LOG("商人刷新：点了，但客户端没有在交易的商人，不发"); return; }
    if (*(volatile uint32_t *)(b + 0x2d5eca8)) {   // 赌博页（面板打开函数 0x409b90 按这个字定模式 2）：照原版赌博刷新 0x408e78 发 0x38 类型 2，不置待刷新 | Gambling page (the panel open function 0x409b90 sets mode 2 from this word): send 0x38 type 2 like the original game's gambling refresh 0x408e78, without setting pending refresh
        uint8_t g[9] = {0x38, 2, 0, 0, 0}; memcpy(g + 5, &npc, 4); LOG("商人刷新：赌博页 → 照原版发 0x38 类型 2（商人 %u）", npc);
        ((void (*)(const uint8_t *, int))(b + 0x2ceab0))(g, 9); return; }
    uint8_t pkt[9] = {0x38, 1, 0, 0, 0}; memcpy(pkt + 5, &npc, 4);
    atomic_store(&g_req, npc);
    LOG("商人刷新：点了 → 发 0x38 类型 1（商人 %u）", npc);
    ((void (*)(const uint8_t *, int))(b + 0x2ceab0))(pkt, 9);   // 客户端发包（0x408ea8 同一个函数） | Client send packet (the same function 0x408ea8 uses)
}

void dm_shoprefresh_c2s(uint64_t G, uint64_t player, const uint8_t *pkt, uint32_t len) {
    if (len != 9 || pkt[0] != 0x38 || !G || !player) return;
    uint32_t type, npc; memcpy(&type, pkt + 1, 4); memcpy(&npc, pkt + 5, 4);
    uint32_t want = atomic_load(&g_req);
    if (type != 1 || !want || npc != want || !atomic_compare_exchange_strong(&g_req, &want, 0)) return;
    if (*(uint32_t *)(uintptr_t)player) { LOG("【红】商人刷新：发包的单位不是玩家，不刷"); return; }
    uint64_t pd = *(uint64_t *)(uintptr_t)(player + 0x10); if (!pd) return;
    uint8_t mode = *(uint8_t *)(uintptr_t)(pd + 0x100); uint32_t cur = *(uint32_t *)(uintptr_t)(pd + 0xf8), cls = *(uint32_t *)(uintptr_t)(pd + 0xfc);
    if (mode != 2 || cur != npc) { LOG("商人刷新：玩家不在跟商人 %u 交易（状态 %u，商人 %u），不刷", npc, mode, cur); return; }
    uint8_t *rec = *(uint8_t **)(uintptr_t)(G + 0x3838); uint64_t n = *(uint64_t *)(uintptr_t)(G + 0x3840);
    for (uint64_t i = 0; rec && i < n; i++, rec += 0x78) if (*(uint16_t *)rec == (uint16_t)cls) {
        rec[0x35] = 1; LOG("商人刷新：商人 %u（类号 %u）店铺记录置待刷新，原版接着清空并重生成货单", npc, cls); return; }
    LOG("【红】商人刷新：没找到商人类号 %u 的店铺记录（共 %llu 条）", cls, (unsigned long long)n);
}

// 物品面板里「手柄光标 / 指针当前指着的物品」：按游戏自己的悬停对象取（底部按键提示用的那个函数），不按格子坐标反算。
// 只许游戏主线程调（取手柄处 / 界面消息回调）：读的是客户端界面控件和客户端单位表，同步、只读、不加锁。
// 'The item the controller cursor / pointer is currently pointing at' in an item panel: taken from the game's own hover object (the function used by the bottom key hints), not back-computed from cell coordinates.
// May only be called from the game main thread (where the controller is read / UI message callbacks): it reads client UI widgets and the client unit table, synchronously, read-only, without locking.
#pragma once
#include <stdint.h>
enum { DM_ITEM_AT_NONE, DM_ITEM_AT_INVENTORY, DM_ITEM_AT_BELT, DM_ITEM_AT_EQUIP, DM_ITEM_AT_STASH, DM_ITEM_AT_CUBE, DM_ITEM_AT_SHOP, DM_ITEM_AT_TRADE };
typedef struct {
    uint32_t id, cls;       // 物品单位 id、物品表行号 | Item unit id, item table row number
    char code[4];           // 物品码（去尾空格，如 "hp1"、"box"），以 0 结尾 | Item code (trailing spaces removed, e.g. "hp1", "box"), 0-terminated
    int container;          // DM_ITEM_AT_*
    int potion, scroll, book;   // 物品类型（游戏的类型归属函数）：药水 / 卷轴 / 书 | Item type (the game's type membership function): potion / scroll / book
    int beltable;           // 类型上可放腰带（物品类型表的腰带标志） | Beltable by type (the belt flag in the item type table)
    int belt_room;          // 可放腰带且此刻腰带有位（游戏「移到腰带」提示前的同一个检查） | Beltable and the belt has room right now (the same check that precedes the game's 「移到腰带」(Move to Belt) hint)
    int equippable;         // 此刻能装备到身上（游戏「装备」提示前的同一个检查） | Can be equipped right now (the same check that precedes the game's 「装备」(Equip) hint)
    int stack;              // 高级仓库（宝石 / 材料 / 符文页）里这一格的叠数；不是成叠物品 = 0（物品数据 +0x9c，原版长按取多件前判的同一个字节） | Stack count of this cell in the advanced stash (gem / material / rune pages); not a stacked item = 0 (item data +0x9c, the same byte the original game checks before a long-press takes multiple items)
    int unid;               // 未鉴定：物品数据（[物品+0x10]）+0x18 位 0x10 为 0（原版「已鉴定」标志；测试代码 dm_actions.c 反着改过两端核实） | Unidentified: bit 0x10 of item data ([item+0x10]) +0x18 is 0 (the original game's 'identified' flag; verified at both ends by flipping it in the test code dm_actions.c)
    int gx, gy, gw, gh;     // 物品在容器里的左上格与占格（背包 / 箱子 / 方块 / 商店 / 交易）；腰带给 (格号, 0, 1, 1)，装备位给 (0, 0, 1, 1) | Top-left cell and cells occupied of the item in its container (inventory / stash / cube / shop / trade); for the belt gives (cell number, 0, 1, 1), for an equipment slot gives (0, 0, 1, 1)
} dm_cursor_item_info;
int dm_cursor_item(dm_cursor_item_info *out);   // 1 = 指着物品并已填 out；0 = 没有（面板没开 / 指着空格 / 焦点不在物品格上） | 1 = pointing at an item and out has been filled; 0 = none (panel not open / pointing at an empty cell / focus not on an item cell)
int dm_held_size(int *w, int *h);              // 手上拿着的那件的占格（宽、高）；没拿着返回 0。「用了鉴定卷轴等目标」那种不算拿着 | Cells occupied by the item held in hand (width, height); returns 0 if nothing is held. The 'identify scroll used, waiting for a target' case does not count as held
int dm_held_pending(void);                     // 手上那件此刻有没有待确认的操作（上一个指令还没答完）；没拿着返回 0 | Whether the item in hand has an operation pending confirmation right now (the previous command has not been answered yet); returns 0 if nothing is held
int dm_vendor_repair_mode(void);                // 商人页此刻是否处在「修理」光标模式（点了修理钮、锤子光标）。只许游戏主线程调 | Whether the vendor page is in the 'repair' cursor mode right now (repair button tapped, hammer cursor). May only be called from the game main thread
int dm_item_targeting(void);                    // 此刻是否处在「用了一件物品、等待选目标物品」的状态：0 无；非 0 = 物品表里该物品的目标类型 + 1（鉴定卷轴 / 鉴定书都走这个状态） | Whether the 'an item was used, waiting to select a target item' state is in effect right now: 0 none; non-zero = the target type of that item in the item table + 1 (identify scroll / identify tome both go through this state)

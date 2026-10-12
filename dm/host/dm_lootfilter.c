// 掉落过滤（用原版自带的战利品过滤器，不自造一套）：按难度自动选档——普通难度不藏、噩梦「轻筛」、地狱（秘境只在地狱开）「重筛」。
// 原版的机制（Switch 版主程序里完整存在，只是暂停菜单里的入口被我们的布局藏着）：
//   · 规则文件 <存档>/Settings/<配置名>.fltr 在启动时全部载入；<存档>/Settings/lootfilter.json 的 Profiles 段记「角色名 → 配置名」，
//     每次进局收到自己的玩家单位时重读一次这个文件、按角色名选当前配置（main+0x464130 → 0x382280 → 0x381d10）。
//   · 判定 main+0x37f3d4(过滤器 main+0x2d58520, 物品) 返回 1 可见 / 0 隐藏，全映像只有三处调用：地上名字标签两处（不建标签）、
//     手柄每帧选目标一处（主钮不去捡）。只在术士君临模式生效（函数开头的模式门）。任一「显示」规则命中即可见，否则有「隐藏」规则命中才藏。
//   · 一条规则里：非装备看 itemCategory（界面类别名，父类 misc 含全部子类）或 itemCode（物品三字码）；装备看 equipmentRarity（稀有度）、
//     equipmentQuality（普通 / 扩展 / 精英档，只对分档的类别有效——戒指项链护身符珠宝头环不分档）、equipmentCategory / equipmentItemCode。
//     投掷药水算装备（武器），要用 equipmentItemCode 点名（实测：写在 itemCode 里藏不住）。
// 宿主做的事：挂存档时把四份规则文件写进 Settings/（原版只在启动时载入规则文件，所以要赶在那之前）；之后主线程每半秒看一眼
// 「现在该用哪一份」（难度、开关、身上有没有弓弩），和过滤器对象里的「当前配置名」不一样就改过去。
// 当前配置名 = 过滤器对象 main+0x2d58520 的 +0x20：原版自己的字符串（+0 字符指针、+8 长度、+0x10 容量，最高位是标志；15 字以内存在对象自带的缓冲里）。
// 判定函数每次都按这个名字去已载入的配置里查（0x37d5fc），所以改名字即时生效；我们的名字都不超过 10 个字符，只在现有容量够时原地改写。
// lootfilter.json 不碰：实测原版只在启动时读它一次，进局时用的是内存里那份「角色 → 配置」，改文件不管用；进局时原版按它把名字设回去，下一次巡检再改过来。
// 身上装着弓 / 弩（含副手那套）时箭矢弩矢不藏，局中换上 / 卸下弓半秒内跟着变。
// 开关：「自定义」设置 loot_filter（缺省开 = 按难度自动；关 = 全显示）。测试强制：DM_LOOTFILTER=off / lite / heavy。
// Loot filter (uses the game's built-in loot filter rather than building our own): the tier is picked by difficulty: nothing hidden on Normal, "lite" on Nightmare, "heavy" on Hell (Greater Rifts only open on Hell).
// The game's mechanism (fully present in the Switch main executable; only its pause-menu entry is hidden by our layout):
//   · Rule files <save>/Settings/<profile name>.fltr are all loaded at startup; the Profiles section of <save>/Settings/lootfilter.json records "character name → profile name",
//     and each time a game is entered and our own player unit arrives, this file is re-read and the current profile chosen by character name (main+0x464130 → 0x382280 → 0x381d10).
//   · The check main+0x37f3d4(filter main+0x2d58520, item) returns 1 visible / 0 hidden, with only three call sites in the whole image: two for ground name labels (no label is built),
//     one for the controller's per-frame target pick (the primary button will not pick it up). Only active in Reign of the Warlock mode (mode gate at the top of the function). Any matching "show" rule makes it visible; otherwise it is hidden only if a "hide" rule matches.
//   · Within a rule: non-equipment goes by itemCategory (UI category name; parent misc includes all subcategories) or itemCode (three-letter item code); equipment goes by equipmentRarity (rarity),
//     equipmentQuality (normal / exceptional / elite tier, only valid for tiered categories: rings, amulets, charms, jewels and circlets have no tiers), equipmentCategory / equipmentItemCode.
//     Throwing potions count as equipment (weapons) and must be named with equipmentItemCode (measured: putting them in itemCode does not hide them).
// What the host does: on save mount, write the four rule files into Settings/ (the game only loads rule files at startup, so it must happen before that); after that the main thread looks every half second at
// "which one should be in use now" (difficulty, switch, bow or crossbow equipped) and changes the filter object's "current profile name" if it differs.
// Current profile name = +0x20 of the filter object main+0x2d58520: the game's own string (+0 char pointer, +8 length, +0x10 capacity, top bit is a flag; up to 15 chars live in the object's inline buffer).
// The check looks the name up among the loaded profiles every time (0x37d5fc), so changing the name takes effect at once; our names are all at most 10 characters, and we only rewrite in place when the existing capacity is enough.
// lootfilter.json is not touched: measured, the game reads it only once at startup and on entering a game uses the in-memory "character → profile" map, so editing the file does nothing; on entry the game sets the name back from that map, and the next poll changes it again.
// With a bow / crossbow equipped (including the off-hand set) arrows and bolts are not hidden; equipping / removing a bow mid-game is followed within half a second.
// Switch: "Custom" setting loot_filter (default on = automatic by difficulty; off = show everything). Test override: DM_LOOTFILTER=off / lite / heavy.
#include "dm.h"
#include "dm_custom.h"
#include <dirent.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define T_MISSILE 47   // 物品类型表行号：miss（弓、弩、亚马逊弓的父类型） | item type table row: miss (parent type of bows, crossbows, Amazon bows)
int dm_diag_difficulty(void);

// 轻筛藏：精力 / 解毒 / 解冻药，红蓝药最低两档，回城与鉴定的卷轴和书，全部投掷药，低质量（破损 / 粗糙）装备，箭矢弩矢（没装弓弩时）。
// 重筛再藏：红蓝药第三档；普通档的白装（带孔 / 无形的留着，头环不分档整类留着）；普通档的蓝装（饰品、头环、各职业专属留着）。
// 任何档都不藏：紫药、钥匙、暗金、套装、稀有、符文、宝石、护身符、珠宝、任务物品与各种材料、金币——规则里压根不提它们。
// Lite hides: Stamina / Antidote / Thawing potions, the two lowest tiers of Healing and Mana potions, Town Portal and Identify scrolls and tomes, all throwing potions, low quality (Cracked / Crude) gear, arrows and bolts (when no bow or crossbow is equipped).
// Heavy also hides: the third tier of Healing and Mana potions; normal-tier white items (socketed / ethereal ones are kept, circlets have no tiers and are kept as a whole); normal-tier magic items (jewelry, circlets and class-specific items are kept).
// Never hidden at any tier: Rejuvenation potions, keys, Unique, Set, Rare, runes, gems, charms, jewels, quest items and all kinds of materials, gold. The rules simply never mention them.
static void write_fltr(const char *dir, const char *name, int heavy, int ammo) {
    char p[1300], t[1320]; snprintf(p, sizeof p, "%s/%s.fltr", dir, name); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w"); if (!f) { LOG("【红】掉落过滤：规则文件写不了 %s", p); return; }
    fprintf(f, "{\n \"name\": \"%s\",\n \"rules\": [\n", name);
    fprintf(f, "  {\"name\": \"HIDE consumables\", \"enabled\": true, \"ruleType\": \"hide\", \"filterEtherealSocketed\": false, \"itemCode\": [\"vps\", \"yps\", \"wms\", \"hp1\", \"hp2\", \"mp1\", \"mp2\", %s\"tsc\", \"isc\", \"tbk\", \"ibk\"]},\n", heavy ? "\"hp3\", \"mp3\", " : "");
    fprintf(f, "  {\"name\": \"HIDE throwing potions\", \"enabled\": true, \"ruleType\": \"hide\", \"filterEtherealSocketed\": false, \"equipmentItemCode\": [\"gps\", \"gpm\", \"gpl\", \"ops\", \"opm\", \"opl\"]},\n");
    if (ammo) fprintf(f, "  {\"name\": \"HIDE ammo\", \"enabled\": true, \"ruleType\": \"hide\", \"filterEtherealSocketed\": false, \"itemCategory\": [\"ammo\"]},\n");
    if (heavy) {
        fprintf(f, "  {\"name\": \"SHOW socketed or ethereal\", \"enabled\": true, \"ruleType\": \"show\", \"filterEtherealSocketed\": true},\n");
        fprintf(f, "  {\"name\": \"SHOW normal circlets\", \"enabled\": true, \"ruleType\": \"show\", \"filterEtherealSocketed\": false, \"equipmentRarity\": [\"normal\", \"hiQuality\"], \"equipmentCategory\": [\"circl\"]},\n");
        fprintf(f, "  {\"name\": \"SHOW magic accessories\", \"enabled\": true, \"ruleType\": \"show\", \"filterEtherealSocketed\": false, \"equipmentRarity\": [\"magic\"], \"equipmentCategory\": [\"acce\"]},\n");
        fprintf(f, "  {\"name\": \"SHOW magic circlets\", \"enabled\": true, \"ruleType\": \"show\", \"filterEtherealSocketed\": false, \"equipmentRarity\": [\"magic\"], \"equipmentCategory\": [\"circl\"]},\n");
        fprintf(f, "  {\"name\": \"SHOW magic class items\", \"enabled\": true, \"ruleType\": \"show\", \"filterEtherealSocketed\": false, \"equipmentRarity\": [\"magic\"], \"equipmentCategory\": [\"barbh\", \"druid\", \"palad\", \"necro\", \"warlo\", \"amazo\", \"assas\", \"sorce\"]},\n");
        fprintf(f, "  {\"name\": \"HIDE normal-tier white and magic\", \"enabled\": true, \"ruleType\": \"hide\", \"filterEtherealSocketed\": false, \"equipmentRarity\": [\"normal\", \"hiQuality\", \"magic\"], \"equipmentQuality\": [\"normal\"]},\n"); }
    fprintf(f, "  {\"name\": \"HIDE low quality\", \"enabled\": true, \"ruleType\": \"hide\", \"filterEtherealSocketed\": false, \"equipmentRarity\": [\"lowQuality\"]}\n ]\n}\n");
    int ok = fclose(f) == 0 && rename(t, p) == 0; if (!ok) LOG("【红】掉落过滤：规则文件没写成 %s", p);
}
// 四份：轻 / 重 ×（藏箭矢 / 身上有弓弩不藏箭矢）。挂存档时调（hle_fs.c），内容是固定的，每次启动重写一遍（规则改了老存档也跟上）
// Four files: lite / heavy × (hide arrows / do not hide arrows with a bow or crossbow equipped). Called on save mount (hle_fs.c); the contents are fixed and rewritten on every launch (so old saves follow rule changes)
void dm_lootfilter_seed(const char *save_dir) {
    char d[1100]; snprintf(d, sizeof d, "%s/Settings", save_dir); mkdir(d, 0755);
    write_fltr(d, "DmLite", 0, 1); write_fltr(d, "DmLiteBow", 0, 0); write_fltr(d, "DmHeavy", 1, 1); write_fltr(d, "DmHeavyBow", 1, 0);
}
__attribute__((constructor)) static void lootfilter_hook_install(void) { extern void (*g_dm_mount_seed_hook)(const char *); g_dm_mount_seed_hook = dm_lootfilter_seed; }
static const char *want(int bow, int *why_diff) {   // 现在该用哪一份；"" = 不过滤 | which one should be in use now; "" = no filtering
    int diff = dm_diag_difficulty(), lvl = diff == 1 ? 1 : diff == 2 ? 2 : 0; *why_diff = diff;
    const char *e = getenv("DM_LOOTFILTER");
    if (e && !strcmp(e, "off")) lvl = 0; else if (e && !strcmp(e, "lite")) lvl = 1; else if (e && !strcmp(e, "heavy")) lvl = 2;
    else if (!dm_custom_get("loot_filter")) lvl = 0;
    return lvl == 0 ? "" : lvl == 1 ? (bow ? "DmLiteBow" : "DmLite") : (bow ? "DmHeavyBow" : "DmHeavy");
}
// 主线程每帧调（dm_potpick.c 的每帧入口带过来；pl = 客户端玩家，不在局内为 0）：每 30 帧核一次当前配置名
// Called every frame on the main thread (via the per-frame entry in dm_potpick.c; pl = client player, 0 when not in a game): checks the current profile name every 30 frames
uint64_t dm_guest_frame(void);
void dm_lootfilter_pump(uint8_t *b, uint64_t pl) {
    static uint64_t next; uint64_t fr = dm_guest_frame(); if (!pl || fr < next) return; next = fr + 30;
    uint64_t F = (uint64_t)(uintptr_t)(b + 0x2d58520); if (!*(uint64_t *)(F + 0x10)) return;   // 还没有载入任何配置 | no profile loaded yet
    uint64_t inv = *(uint64_t *)(pl + 0x90), it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0; int bow = 0;   // 身上装备着的（方式 1，含副手那套）有没有弓 / 弩 | is there a bow / crossbow among equipped items (mode 1, including the off-hand set)
    for (int k = 0; it && k < 512 && !bow; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 1 && ((int (*)(uint64_t, int))(b + 0x574bc0))(it, T_MISSILE)) bow = 1;
        it = *(uint64_t *)(d + 0xb0); }
    if (getenv("DM_TEST_LOOT_BOW")) bow = 1;   // 测试开关：当作身上有弓弩（验「不藏箭矢」那两份规则） | test switch: act as if a bow / crossbow is equipped (to verify the two "do not hide arrows" rule sets)
    int diff = -1; const char *w = want(bow, &diff); size_t wl = strlen(w);
    char *cur = (char *)(uintptr_t)*(uint64_t *)(F + 0x20); uint64_t len = *(uint64_t *)(F + 0x28), cap = *(uint64_t *)(F + 0x30) & 0x7fffffffffffffffull;
    if (!cur || (len == wl && !memcmp(cur, w, wl))) return;
    if (wl > cap || len > cap) { static int n; if (n++ < 3) LOG("【红】掉落过滤：当前配置名的缓冲放不下（容量 %llu、现长 %llu），没改", (unsigned long long)cap, (unsigned long long)len); return; }
    char old[24]; size_t ol = len < sizeof old - 1 ? (size_t)len : sizeof old - 1; memcpy(old, cur, ol); old[ol] = 0;
    memcpy(cur, w, wl); cur[wl] = 0; *(uint64_t *)(F + 0x28) = wl;
    LOG("掉落过滤：难度 %d、身上%s弓弩 → 当前配置「%s」→「%s」", diff, bow ? "有" : "没有", old, w[0] ? w : "（不过滤）");
}
// 这件地上的物品是不是被原版过滤器藏着（客户端物品单位；主线程调）。我们自己的自动拾取据此跳过：屏上没名字的东西不该被悄悄捡进背包
// Is this ground item hidden by the game's filter (client item unit; called on the main thread). Our own auto-pickup skips on this: something with no name on screen should not be silently picked into the inventory
int dm_lootfilter_hidden(uint8_t *b, uint64_t item) { return item && !(((int (*)(uint64_t, uint64_t))(b + 0x37f3d4))((uint64_t)(uintptr_t)(b + 0x2d58520), item) & 1); }
// 测试开关 DM_TEST_LOOTDUMP=<帧>（验规则用）：到这一帧把客户端地上的每件物品列一遍——物品码、稀有度（物品数据 +0）、被藏 / 可见（原版判定的结果）。主线程调
// Test switch DM_TEST_LOOTDUMP=<frame> (for verifying rules): at that frame list every item on the ground on the client: item code, rarity (item data +0), hidden / visible (the result of the game's check). Called on the main thread
uint64_t dm_guest_frame(void);
void dm_lootfilter_test_dump(uint8_t *b) {
    static long at = -1; if (at < 0) { const char *e = getenv("DM_TEST_LOOTDUMP"); at = e ? atol(e) : 0; } if (at <= 0 || dm_guest_frame() < (uint64_t)at) return; at = 0;
    int n = 0, hid = 0;
    for (int k = 0; k < 128; k++) for (uint64_t it = *(uint64_t *)(b + 0x2d603a0 + 4 * 0x400 + k * 8); it; it = *(uint64_t *)(it + 0x158)) {
        if (*(uint32_t *)(it + 0xc) != 3) continue; uint64_t d = *(uint64_t *)(it + 0x10); uint32_t code = ((uint32_t (*)(uint64_t))(b + 0x570260))(it); int h = dm_lootfilter_hidden(b, it); n++; hid += h;
        LOG("掉落过滤读数：%.3s 稀有度 %u 标志 %#x → %s", (const char *)&code, d ? *(uint32_t *)d : 0, d ? *(uint32_t *)(d + 0x18) : 0, h ? "藏" : "显示"); }
    LOG("掉落过滤读数：地上 %d 件，其中藏 %d 件", n, hid);
}

// 符文之语清单「需要等级」：找列与取最大值。期望值全部取自原版数据表手算（armor.txt / misc.txt 的 levelreq、runes.txt 的配方），不跑被测实现算。
//   底材：塔盔 uhm 需求 59（物品等级 79）、翼盔 xhm 25（51）、权冠 ci3 64（85）、宝冠 urn 66（85）、武装盔 ulm 51（68）、头盔 hlm 0（11）
//   符文：塔尔 r07 17、书尔 r10 23、索尔 r12 27、沙伊 r13 29、艾欧 r16 35、蓝姆 r20 43、伊斯特 r24 51
// "Required level" in the runeword list: finding the column and taking the maximum. All expected values are worked out by hand from the original data tables (levelreq in armor.txt / misc.txt, recipes in runes.txt), not by running the implementation under test.
//   Bases: Spired Helm uhm requires 59 (item level 79), Winged Helm xhm 25 (51), Diadem ci3 64 (85), Corona urn 66 (85), Armet ulm 51 (68), Helm hlm 0 (11)
//   Runes: Tal r07 17, Thul r10 23, Sol r12 27, Shael r13 29, Io r16 35, Lem r20 43, Ist r24 51
#include "../dm_rwlv_core.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define ROW 0x1c0
static uint8_t tab[16][ROW]; static int nrow;
// 造一行：物品码在 +0x80，「物品等级」放在 lv_off，「需求等级」放在 req_off；其余字节填与等级无关的值
// Build one row: item code at +0x80, "item level" at lv_off, "required level" at req_off; the remaining bytes are filled with values unrelated to levels
static void row(const char *code, uint8_t level, uint8_t req, int lv_off, int req_off) { uint8_t *r = tab[nrow++]; memset(r, 0xee, ROW); memset(r + 0x80, 0, 4); memcpy(r + 0x80, code, 3); r[lv_off] = level; r[req_off] = req; }
static void build(int lv_off, int req_off) { nrow = 0; memset(tab, 0, sizeof tab);
    row("hlm", 11, 0, lv_off, req_off); row("xhm", 51, 25, lv_off, req_off); row("uhm", 79, 59, lv_off, req_off); row("ci3", 85, 64, lv_off, req_off); row("urn", 85, 66, lv_off, req_off); row("ulm", 68, 51, lv_off, req_off);
    row("r05", 15, 15, lv_off, req_off); row("r07", 17, 17, lv_off, req_off); row("r13", 29, 29, lv_off, req_off); }
int main(void) {
    // 一、找列：物品等级那一列排在需求等级前面（线上缺陷的现场：旧找法只看符文，先撞上物品等级）——必须返回需求等级那一列
    // Part one, finding the column: the item-level column comes before the required-level column (the scene of the reported defect: the old search looked only at runes and hit item level first) - the required-level column must be returned
    build(0x57, 0x5b); CHECK(dm_rw_find_lvoff(&tab[0][0], (uint32_t)nrow, ROW) == 0x5b);
    // 两列次序反过来也一样
    // The same with the two columns in the opposite order
    build(0x5b, 0x57); CHECK(dm_rw_find_lvoff(&tab[0][0], (uint32_t)nrow, ROW) == 0x57);
    // 找到的那一列读塔盔必须是 59，不是 79
    // The column found must read 59 for the Spired Helm, not 79
    build(0x57, 0x5b); { int o = dm_rw_find_lvoff(&tab[0][0], (uint32_t)nrow, ROW); CHECK(o >= 0 && tab[2][o] == 59); }
    // 负对照：表里缺锚点物品（只剩符文）→ 找不到；需求等级那一列被改坏一行 → 找不到；空表 → 找不到
    // Negative controls: anchor items missing from the table (only runes left) -> not found; one row of the required-level column corrupted -> not found; empty table -> not found
    build(0x57, 0x5b); CHECK(dm_rw_find_lvoff(&tab[6][0], 3, ROW) == -1);
    build(0x57, 0x5b); tab[2][0x5b] = 60; CHECK(dm_rw_find_lvoff(&tab[0][0], (uint32_t)nrow, ROW) == -1);
    CHECK(dm_rw_find_lvoff(NULL, 0, ROW) == -1);
    // 二、需要等级 = max(底材需求, 各符文需求)。线上图里的四条（塔盔 3 孔）：
    // Part two: required level = max(base requirement, each rune's requirement). The four entries in the reported screenshot (3-socket Spired Helm):
    { int hearth[] = {29, 35, 23}, cure[] = {29, 35, 17}, bulwark[] = {29, 35, 27}, delirium[] = {43, 51, 35};
      CHECK(dm_rw_need_level(59, hearth, 3) == 59);     // 壁炉：夏 + 艾欧 + 书尔 | Hearth: Shael + Io + Thul
      CHECK(dm_rw_need_level(59, cure, 3) == 59);       // 解药：夏 + 艾欧 + 塔尔 | Cure: Shael + Io + Tal
      CHECK(dm_rw_need_level(59, bulwark, 3) == 59);    // 壁垒：夏 + 艾欧 + 索尔 | Bulwark: Shael + Io + Sol
      CHECK(dm_rw_need_level(59, delirium, 3) == 59);   // 迷狂：蓝姆 + 伊斯特 + 艾欧 | Delirium: Lem + Ist + Io
      // 别的底材：符文盖过底材 / 底材盖过符文各两条
      // Other bases: two cases each of runes outweighing the base / the base outweighing the runes
      CHECK(dm_rw_need_level(25, hearth, 3) == 35);     // 壁炉做在翼盔（25）上：艾欧 35 | Hearth made in a Winged Helm (25): Io 35
      CHECK(dm_rw_need_level(25, delirium, 3) == 51);   // 迷狂做在翼盔上：伊斯特 51 | Delirium made in a Winged Helm: Ist 51
      CHECK(dm_rw_need_level(66, delirium, 3) == 66);   // 迷狂做在宝冠（66）上：底材 66 | Delirium made in a Corona (66): base 66
      CHECK(dm_rw_need_level(64, cure, 3) == 64); }     // 解药做在权冠（64）上：底材 64 | Cure made in a Diadem (64): base 64
    CHECK(dm_rw_need_level(0, NULL, 0) == 0);
    if (fails) { printf("t_rwlv: %d 处失败\n", fails); return 1; }
    printf("t_rwlv OK\n"); return 0;
}

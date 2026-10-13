// 见 dm_rwlv_core.h。
// 「需求等级」的偏移没有写死，按已知物品的原版值在行里找——锚点必须含「物品等级 ≠ 需求等级」的行：
// 原先只用三颗符文（塔尔 17 / 艾斯 15 / 沙伊 29）当锚点，而符文的物品等级（misc.txt level）与需求等级（levelreq）33 颗里 32 颗是同一个数，
// 先撞上的是排在前面的物品等级那一列；读符文没差，读底材就成了物品等级（塔盔 uhm：需求 59 被读成 79），清单里整页的需要等级偏高。
// See dm_rwlv_core.h.
// The offset of "required level" is not hard-coded; it is found in the row by the original values of known items - the anchors must include rows where "item level != required level":
// previously only three runes (Tal 17 / Eth 15 / Shael 29) served as anchors, and for runes the item level (misc.txt level) and the required level (levelreq) are the same number for 32 of the 33,
// so the first hit was the item-level column that comes earlier; reading runes made no difference, but reading a base item gave its item level (Spired Helm uhm: required 59 read as 79), and the whole page of required levels in the list was too high.
#include "dm_rwlv_core.h"
#include <string.h>

// 原版 armor.txt / misc.txt 的 levelreq（括号里是同一行的 level，两者不同的才分得开两列）
// levelreq from the original armor.txt / misc.txt (in parentheses the level of the same row; only rows where the two differ can tell the two columns apart)
static const struct { const char *code; uint8_t req; } k_anchor[] = {
    { "uhm", 59 },   // 塔盔（79） | Spired Helm (79)
    { "xhm", 25 },   // 翼盔（51） | Winged Helm (51)
    { "ci3", 64 },   // 权冠（85） | Diadem (85)
    { "urn", 66 },   // 宝冠（85） | Corona (85)
    { "ulm", 51 },   // 武装盔（68） | Armet (68)
    { "r07", 17 },   // 塔尔（17） | Tal (17)
    { "r13", 29 },   // 沙伊（29） | Shael (29)
};
#define NANCHOR (int)(sizeof k_anchor / sizeof k_anchor[0])

int dm_rw_find_lvoff(const uint8_t *rows, uint32_t n, uint32_t rowsize) {
    const uint8_t *at[NANCHOR]; memset(at, 0, sizeof at);
    if (!rows || rowsize < 0x84) return -1;
    for (uint32_t i = 0; i < n; i++) { const uint8_t *r = rows + (uint64_t)i * rowsize;
        for (int k = 0; k < NANCHOR; k++) if (!at[k] && !memcmp(r + 0x80, k_anchor[k].code, 3) && (r[0x83] == 0 || r[0x83] == ' ')) at[k] = r; }
    for (int k = 0; k < NANCHOR; k++) if (!at[k]) return -1;
    int found = -1;
    for (uint32_t o = 0; o < rowsize; o++) { int ok = 1; for (int k = 0; k < NANCHOR && ok; k++) ok = at[k][o] == k_anchor[k].req;
        if (ok) { if (found >= 0) return -1; found = (int)o; } }
    return found;
}

int dm_rw_need_level(int base_req, const int *rune_req, int n) { int lv = base_req; for (int i = 0; i < n; i++) if (rune_req[i] > lv) lv = rune_req[i]; return lv; }

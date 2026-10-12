// 起名页「留空 = 随机取名」的名字来源（原版「随机名字」键按职业在 12 个名字里依次轮，容易和已有角色撞）。
// 改成按音节拼：开头 × 中段（可空）× 结尾，约 1.8 万种，取随机数用 arc4random_uniform。
// 拼出来只含英文字母、首字母大写、4–10 个字母，天然满足游戏的起名规则（main+0x7fed78：2–15 字节、首尾是字母、- _ 至多一个）。
// taken(名字) 非 0 = 已有同名角色，再抽，至多 64 次。
// The name source for "leave blank = random name" on the naming page (the original game's 「随机名字」(Random Name) button cycles through 12 names per class in turn, which easily collides with existing characters).
// Changed to assembling from syllables: start × middle (may be empty) × ending, about 18 thousand combinations; random numbers come from arc4random_uniform.
// The result contains only English letters, first letter capitalized, 4–10 letters, which naturally satisfies the game's naming rules (main+0x7fed78: 2–15 bytes, first and last are letters, at most one of - _).
// taken(name) non-zero = a character with that name already exists; draw again, at most 64 times.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_head[] = {
    "Al", "Ar", "Bel", "Bran", "Cael", "Cor", "Dar", "Dra", "El", "Eri", "Fen", "Gal", "Gor", "Hal", "Is", "Jor",
    "Kal", "Kor", "Lor", "Ma", "Mor", "Nar", "Ny", "Or", "Ra", "Rho", "Sar", "Sha", "Tal", "Thar", "Ul", "Val",
    "Vex", "Wy", "Xan", "Yr", "Zar", "Ze",
};
static const char *const k_mid[] = { "", "", "", "a", "e", "i", "o", "u", "an", "en", "ri", "la", "mo", "da", "ve", "th", "go", "ka" };
static const char *const k_tail[] = {
    "ric", "dor", "wyn", "eth", "ias", "mir", "nar", "ros", "ven", "dra", "lia", "ra", "na", "us", "is", "on",
    "gar", "thas", "mund", "ane", "iel", "ok", "ash", "ius", "ard", "ex", "ys", "orn", "ald", "ica",
};
#define N(a) (sizeof(a) / sizeof(a)[0])

int dm_name_gen(char *out, int cap, int (*taken)(const char *)) {
    for (int k = 0; k < 64; k++) {
        snprintf(out, (size_t)cap, "%s%s%s", k_head[arc4random_uniform(N(k_head))], k_mid[arc4random_uniform(N(k_mid))], k_tail[arc4random_uniform(N(k_tail))]);
        if (!taken || !taken(out)) return 1;
    }
    return 0;
}

// 起名随机名单测（秒级）：dm_namegen.c。判据：连续留空建 20 次角色，名字全不同、都合法。
// 「建过的角色」用 taken 桩模拟：每抽出一个就记进已有名单，下一次抽到它算重名。合法 = 游戏起名规则（main+0x7fed78）：2–15 字节、首尾是字母、- _ 至多一个；我们的名字只该含英文字母。
// Unit test for random names at character naming (takes seconds): dm_namegen.c. Criterion: create a character 20 times in a row with the name left blank, and the names are all different and all valid.
// "Characters already created" are simulated with the taken stub: each name drawn is recorded in the existing list, and drawing it the next time counts as a duplicate. Valid = the game's naming rule (main+0x7fed78): 2–15 bytes, first and last are letters, at most one of - _; our names should contain only English letters.
#include <stdio.h>
#include <string.h>
int dm_name_gen(char *out, int cap, int (*taken)(const char *));

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char have[20][32]; static int nhave;
static int taken(const char *n) { for (int i = 0; i < nhave; i++) if (!strcasecmp(have[i], n)) return 1; return 0; }   // 存档目录在大小写不敏感的文件系统上 | the save directory is on a case-insensitive file system
static int all_taken(const char *n) { (void)n; return 1; }
static int legal(const char *n) { size_t l = strlen(n); if (l < 2 || l > 15) return 0; for (size_t i = 0; i < l; i++) if (!((n[i] >= 'A' && n[i] <= 'Z') || (n[i] >= 'a' && n[i] <= 'z'))) return 0; return 1; }

int main(void) {
    for (int i = 0; i < 20; i++) { char nm[32]; int ok = dm_name_gen(nm, sizeof nm, taken); CHECK(ok, "第 %d 次没抽到", i + 1); CHECK(legal(nm), "第 %d 次「%s」不合法", i + 1, nm); CHECK(!taken(nm), "第 %d 次「%s」与已建角色重名", i + 1, nm); strcpy(have[nhave++], nm); }
    printf("  20 次：%s %s %s … %s\n", have[0], have[1], have[2], have[19]);
    int bad = 0; for (int i = 0; i < 100000; i++) { char nm[32]; dm_name_gen(nm, sizeof nm, NULL); bad += !legal(nm); } CHECK(!bad, "10 万次里 %d 个不合法", bad);
    char nm[32]; CHECK(!dm_name_gen(nm, sizeof nm, all_taken), "全部重名时应返回 0");
    printf("t_namegen: %d 项，%d 失败\n", checks, fails); return fails != 0;
}

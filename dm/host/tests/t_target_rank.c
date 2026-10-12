// 自动选怪单测：在本进程内存里摆假单位 / 假怪物数据，调观测门回调，核分数调整（期望值手算，不调被测实现算）。
// 布局：单位 +0 类型、+4 类号、+0x10 → 怪物数据（+0x1A TypeFlag：0x02 超级独特 / 0x04 冠军 / 0x08 独特 / 0x10 随从）。
// Automatic monster targeting unit test: lay out fake units / fake monster data in this process's memory, call the observation gate callback, and check the score adjustment (expected values computed by hand, not by calling the implementation under test).
// Layout: unit +0 type, +4 class number, +0x10 -> monster data (+0x1A TypeFlag: 0x02 super unique / 0x04 champion / 0x08 unique / 0x10 minion).
#include <stdio.h>
#include <string.h>
#include <stdint.h>
int gate_obs_present(int id) { (void)id; return 1; }
static void (*g_fn)(const uint64_t *);
void gate_obs_set(int id, void (*fn)(const uint64_t *)) { (void)id; g_fn = fn; }
void target_rank_install(void);
static int fails, cases;
static float run(const void *unit, float score) {
    uint64_t frame[100] = {0}; frame[1] = (uint64_t)(uintptr_t)unit; frame[2] = 1;
    memcpy(&frame[32], &score, 4); g_fn(frame);
    float out; memcpy(&out, &frame[32], 4); return out;
}
static void expect(const char *what, float got, float want) {
    cases++; if (got == want) printf("✓ %s（%g）\n", what, got); else { fails++; printf("✗ %s：得 %g，期望 %g\n", what, got, want); }
}
int main(void) {
    target_rank_install();
    uint8_t data[0x40] = {0};
    struct { uint32_t type, cls, id, mode; uint64_t data; } unit = {1, 5, 77, 0, (uint64_t)(uintptr_t)data};
    expect("普通僵尸（类 5、TypeFlag 0）分数不变", run(&unit, 0.5f), 0.5f);
    data[0x1a] = 0x10; expect("随从（0x10）算普通", run(&unit, 0.5f), 0.5f);
    data[0x1a] = 0x04; expect("冠军（0x04）+1000", run(&unit, 0.5f), 1000.5f);
    data[0x1a] = 0x08; expect("独特（0x08）+1000", run(&unit, 0.25f), 1000.25f);
    data[0x1a] = 0x02; expect("超级独特（0x02）+1000", run(&unit, 0.75f), 1000.75f);
    data[0x1a] = 0; unit.cls = 156; expect("安达利尔（类 156，boss=1）+2000", run(&unit, 0.5f), 2000.5f);
    unit.cls = 745; expect("3.3 新 Boss 巨像（类 745）+2000", run(&unit, 0.125f), 2000.125f);
    unit.cls = 5; unit.type = 0; data[0x1a] = 0x08; expect("玩家单位（类型 0）不动", run(&unit, 0.5f), 0.5f);
    unit.type = 1; unit.data = 0; data[0x1a] = 0; expect("怪物数据指针空：读不到，不动", run(&unit, 0.5f), 0.5f);
    expect("候选单位空指针不动", run(NULL, 0.5f), 0.5f);
    // 跨档：精英最差分（0.01+1000）仍高于普通最好分（0.99）；Boss 最差分高于精英最好分
    // Across tiers: the worst elite score (0.01+1000) is still higher than the best normal score (0.99); the worst Boss score is higher than the best elite score
    unit.data = (uint64_t)(uintptr_t)data; data[0x1a] = 0x04; float elite = run(&unit, 0.01f);
    data[0x1a] = 0; unit.cls = 211; float boss = run(&unit, 0.01f);
    cases++; if (elite > 0.99f && boss > 1000.99f) printf("✓ 跨档优先：精英 %g > 普通 0.99，Boss %g > 精英 1000.99\n", elite, boss); else { fails++; printf("✗ 跨档优先\n"); }
    printf("%s：%d/%d\n", fails ? "【红】自动选怪" : "自动选怪 全绿", cases - fails, cases);
    return fails != 0;
}

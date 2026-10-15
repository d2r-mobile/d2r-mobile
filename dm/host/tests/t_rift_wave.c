// 守护者延迟波回归（公开仓库，hubeiqiao 原样移植，dm 前缀换成 dm）：包含真的 dm_rift.c，以日志里的拍数检查回城、返回及结算边界；不调用原版入口。
// 现场：21843 拍第一波全倒、安排 75 拍后刷第二波；玩家此时已回城（关卡 109），21918 拍到点。期望：人在城里 / 别的图 / 读取中（0）都不刷、待刷状态不动，回到本场秘境图才刷。
// Guardian delayed-wave regression (public repository, ported as is from hubeiqiao with the dm prefix replaced by dm): includes the real dm_rift.c and checks the town return, return, and settlement boundaries using tick counts from the log; does not call original game entries.
// Scene: at tick 21843 the first wave is all down and a second wave is scheduled 75 ticks later; the player has already returned to town (level 109), and tick 21918 is the due time. Expected: no spawn while in town / another level / loading (0) and the pending state stays unchanged; spawn only after returning to this Rift level.
#include "../dm_rift.c"
void (*g_dm_pre_commit_hook[5])(const char *);
uint64_t dm_fs_commit_count(void) { return 0; }
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
    K.nmaps = 2; K.maps[0] = 111; K.maps[1] = 112;
    K.wave = 1; K.nwave = 3; K.wave_t = 21843; atomic_store(&K.st, ST_RUN);
    CHECK(!guard_wave_due(21917, 111));
    CHECK(!guard_wave_due(21918, LV_TOWN));
    CHECK(!guard_wave_due(23000, LV_TOWN));
    CHECK(!guard_wave_due(23000, 1));
    CHECK(!guard_wave_due(23000, 0));
    CHECK(!guard_wave_due(23000, 110));
    CHECK(K.wave_t == 21843 && K.wave == 1);
    CHECK(guard_wave_due(23000, 111));
    CHECK(guard_wave_due(23000, 112));
    CHECK(K.wave_t == 21843 && K.wave == 1);
    atomic_store(&K.st, ST_DONE); CHECK(!guard_wave_due(23000, 111));
    atomic_store(&K.st, ST_IDLE); CHECK(!guard_wave_due(23000, 111));
    atomic_store(&K.st, ST_RUN); K.wave_t = 0; CHECK(!guard_wave_due(23001, 111));
    K.wave = 2; K.wave_t = 24000;
    CHECK(!guard_wave_due(24075, LV_TOWN)); CHECK(guard_wave_due(24100, 111));
    printf("t_rift_wave: %d checks, %d failures\n", checks, fails); return fails != 0;
}

// 换图诊断回归：执行实际观察回调，普通门目的地保持不变，秘境已有改道规则不变；无原生入口调用。
// Warp diagnostic regression: execute the real observer; preserve ordinary destinations and existing Rift rerouting, without native calls.
// Warp diagnostic regression: execute the real observer; preserve ordinary destinations and existing Rift rerouting, without native calls.
#include "../dm_rift.c"
void (*g_dm_pre_commit_hook[5])(const char *);
uint64_t dm_fs_commit_count(void) { return 0; }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int main(void) {
    _Alignas(8) unsigned char game[512] = {0}, player[512] = {0}, path[128] = {0}, room[64] = {0}, ex[160] = {0}, level[512] = {0};
    uint64_t g = (uint64_t)(uintptr_t)game, p = (uint64_t)(uintptr_t)player;
    *(uint64_t *)(player + 0x38) = (uint64_t)(uintptr_t)path; *(uint64_t *)(path + 0x20) = (uint64_t)(uintptr_t)room;
    *(uint64_t *)(room + 0x18) = (uint64_t)(uintptr_t)ex; *(uint64_t *)(ex + 0x90) = (uint64_t)(uintptr_t)level;
    *(int *)(level + 0x1f8) = 40; *(uint32_t *)(player + 8) = 123; *(uint32_t *)(player + 0xc) = 5;
    uint64_t regs[9] = { g, p, 47, 11, 4, 5, 6, 7, 8 }, before[9]; memcpy(before, regs, sizeof regs);
    atomic_store(&K.st, ST_IDLE); rift_level_obs(regs); CHECK(!memcmp(before, regs, sizeof regs));
    regs[2] = 40; memcpy(before, regs, sizeof regs); rift_level_obs(regs); CHECK(!memcmp(before, regs, sizeof regs));
    atomic_store(&K.st, ST_RUN); L.G = g; L.player = p; K.nmaps = 2; K.maps[0] = 111; K.maps[1] = 112; K.cur = 0;
    *(int *)(level + 0x1f8) = 111; *(uint32_t *)(player + 0xc) = 1;
    regs[2] = 109; memcpy(before, regs, sizeof regs); rift_level_obs(regs); CHECK(!memcmp(before, regs, sizeof regs));
    regs[2] = 113; memcpy(before, regs, sizeof regs); rift_level_obs(regs); CHECK(regs[2] == 112 && K.cur == 1);
    for (int i = 0; i < 9; i++) if (i != 2) CHECK(regs[i] == before[i]);
    printf("t_warp_diag: %d checks, %d failures\n", checks, fails); return fails != 0;
}

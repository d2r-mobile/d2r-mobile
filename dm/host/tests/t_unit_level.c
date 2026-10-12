// 「单位 → 所在关卡」读法的单测（dm_unit.h，秒级）。
// 现场：换幕的读取画面期间客户端玩家「有路径、没有房间」。原先的写法一口气连读四层，房间为空就去读地址 0x18 → sig=11 addr=0x18。
// 红 → 绿：同一组输入，旧写法（old_level，照抄修前那一行）在子进程里跑，必须死于 SIGSEGV；新写法返回 0（未知关卡）。
// Unit test for reading "unit -> the level it is in" (dm_unit.h, takes seconds).
// Situation: during the loading screen of an act change the client player "has a path but no room". The old code read four levels in one go, and with a null room went on to read address 0x18 -> sig=11 addr=0x18.
// Fail -> pass: with the same set of inputs, the old code (old_level, copied verbatim from the pre-fix line) runs in a child process and must die of SIGSEGV; the new code returns 0 (unknown level).
#include "dm_unit.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t rd(uint64_t a) { return a ? *(volatile uint64_t *)(uintptr_t)a : 0; }
static int old_level(uint64_t u) { uint64_t r = rd(rd(rd(rd(u + 0x38) + 0x20) + 0x18) + 0x90); return r ? *(int32_t *)(uintptr_t)(r + 0x1f8) : 0; }   // 修前的原样 | exactly as it was before the fix
// 在子进程里跑旧写法：返回它死于哪个信号（0 = 正常返回）
// Run the old code in a child process: returns which signal it died of (0 = returned normally)
static int old_dies(uint64_t u) {
    fflush(stdout); pid_t p = fork(); if (p == 0) { signal(SIGSEGV, SIG_DFL); signal(SIGBUS, SIG_DFL); volatile int v = old_level(u); (void)v; _exit(0); }
    int st = 0; waitpid(p, &st, 0); return WIFSIGNALED(st) ? WTERMSIG(st) : 0;
}

int main(void) {
    static uint8_t unit[0x200], path[0x100], room[0x100], ex[0x100], level[0x300];
    // 完整的一条链：关卡号 59
    // A complete chain: level id 59
    *(uint64_t *)(unit + 0x38) = (uint64_t)(uintptr_t)path; *(uint64_t *)(path + 0x20) = (uint64_t)(uintptr_t)room;
    *(uint64_t *)(room + 0x18) = (uint64_t)(uintptr_t)ex; *(uint64_t *)(ex + 0x90) = (uint64_t)(uintptr_t)level; *(int32_t *)(level + 0x1f8) = 59;
    uint64_t u = (uint64_t)(uintptr_t)unit;
    CHECK(dm_unit_level(u) == 59, "完整链应读到 59，读到 %d", dm_unit_level(u));
    CHECK(dm_unit_level_ptr(u) == (uint64_t)(uintptr_t)level, "完整链应返回关卡结构指针");
    CHECK(old_level(u) == 59, "旧写法在完整链上也应读到 59（对照）");

    // ① 有路径、房间为空（换幕读取期间的客户端玩家）
    // (1) Has a path, room is null (the client player during an act-change loading)
    *(uint64_t *)(path + 0x20) = 0;
    int sg = old_dies(u); CHECK(sg == SIGSEGV || sg == SIGBUS, "旧写法在「有路径、房间为空」上应崩（红），实际信号 %d", sg);
    CHECK(dm_unit_level(u) == 0 && dm_unit_level_ptr(u) == 0, "新写法在「有路径、房间为空」上应返回 0");
    // ② 路径为空
    // (2) Path is null
    *(uint64_t *)(path + 0x20) = (uint64_t)(uintptr_t)room; *(uint64_t *)(unit + 0x38) = 0;
    sg = old_dies(u); CHECK(sg == SIGSEGV || sg == SIGBUS, "旧写法在「路径为空」上应崩（红），实际信号 %d", sg);
    CHECK(dm_unit_level(u) == 0 && dm_unit_level_ptr(u) == 0, "新写法在「路径为空」上应返回 0");
    // ③ 单位为空
    // (3) Unit is null
    sg = old_dies(0); CHECK(sg == SIGSEGV || sg == SIGBUS, "旧写法在「单位为空」上应崩（红），实际信号 %d", sg);
    CHECK(dm_unit_level(0) == 0 && dm_unit_level_ptr(0) == 0, "新写法在「单位为空」上应返回 0");
    // ④ 房间在、房间 +0x18 为空；⑤ +0x90 为空（链的后两步）
    // (4) Room present, room +0x18 null; (5) +0x90 null (the last two steps of the chain)
    *(uint64_t *)(unit + 0x38) = (uint64_t)(uintptr_t)path; *(uint64_t *)(room + 0x18) = 0;
    CHECK(dm_unit_level(u) == 0, "房间 +0x18 为空应返回 0");
    *(uint64_t *)(room + 0x18) = (uint64_t)(uintptr_t)ex; *(uint64_t *)(ex + 0x90) = 0;
    CHECK(dm_unit_level(u) == 0, "关卡指针为空应返回 0");
    *(uint64_t *)(ex + 0x90) = (uint64_t)(uintptr_t)level;
    CHECK(dm_unit_level(u) == 59, "链接回去后应再读到 59");
    // 关卡指针为空时取房间链头：返回 0（原先各处直接 rd(lv + 0x10) 去读地址 0x10）
    // Taking the room chain head when the level pointer is null: returns 0 (previously each place did rd(lv + 0x10) directly and read address 0x10)
    CHECK(dm_level_rooms(0) == 0, "关卡指针为空时房间链头应为 0");
    *(uint64_t *)(level + 0x10) = (uint64_t)(uintptr_t)room; CHECK(dm_level_rooms((uint64_t)(uintptr_t)level) == (uint64_t)(uintptr_t)room, "关卡非空时应读到 +0x10 的房间");
    printf("t_unit_level: %d 项，%d 失败\n", checks, fails); return fails != 0;
}

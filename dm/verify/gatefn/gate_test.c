// 宿主门功能回归：逐个门执行宿主现行 _dm_gate（entry.S），核对寄存器回写契约。
// 用法：gate_test <gates.tsv>...   每行 {返回点, 种类, arg, 原址}；退出码 0 = 全过
// 每个门：x0..x29（x18 除外）设成各不相同的图案 → blr _dm_gate → 桩 gate_dispatch 按门种类写标记值：
//   cntpct / cntvct：regs[arg] = 标记（原指令 mrs x<arg>, cntpct_el0）；svc：x0..x7 = 标记 + i（出参位）；obs：不写
// 返回后核对：桩写过的寄存器 == 标记，其余 == 原图案（桩故意破坏 x9–x17 与 NZCV），NZCV == 调用前，x30 == 调用返回点。任何一项不符报红。
// Host gate functional regression: run the host's current _dm_gate (entry.S) for each gate and check the register write-back contract.
// Usage: gate_test <gates.tsv>...   each line is {return point, kind, arg, original address}; exit code 0 = all pass
// For each gate: set x0..x29 (except x18) to mutually distinct patterns → blr _dm_gate → the stub gate_dispatch writes marker values according to the gate kind:
//   cntpct / cntvct: regs[arg] = marker (original instruction mrs x<arg>, cntpct_el0); svc: x0..x7 = marker + i (output argument positions); obs: writes nothing
// After return, check: registers written by the stub == marker, the rest == original pattern (the stub deliberately clobbers x9–x17 and NZCV), NZCV == value before the call, x30 == the call's return point. Any mismatch reports a failure.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void dm_gate(void);
void gate_call(uint64_t *in, uint64_t *out);       // gate_tramp.S：in[0..29] 装进 x0..x29（跳过 x18）→ blr dm_gate → 全部存进 out[0..30] | gate_tramp.S: load in[0..29] into x0..x29 (skipping x18) → blr dm_gate → store everything into out[0..30]

static int g_kind, g_arg;                           // 当前被测门 | The gate currently under test
enum { K_CNT = 1, K_SVC = 2, K_OBS = 3 };
static const uint64_t MARK = 0x5157000000000000ull;
static uint64_t g_ret_seen;

uint64_t gate_dispatch(uint64_t ret_addr, uint64_t *regs) {
    g_ret_seen = ret_addr;
    uint64_t r = 0;
    if (g_kind == K_CNT) { regs[g_arg] = MARK | (uint64_t)g_arg; r = regs[g_arg]; }
    if (g_kind == K_SVC) { for (int i = 0; i < 8; i++) regs[i] = MARK | 0x100 | (uint64_t)i; r = regs[0]; }
    // 宿主 C 代码按 AAPCS 可随意破坏 x9–x17 与 NZCV：桩在返回前最后一步故意全破坏，核 _dm_gate 从帧里恢复（观测门 / 分支门可放在函数中间的前提）
    // Under AAPCS, host C code may freely clobber x9–x17 and NZCV: the stub deliberately clobbers all of them as its last step before returning, to check that _dm_gate restores them from the frame (the precondition for placing observation gates / branch gates in the middle of a function)
    __asm__ volatile("mov x9, #0xdead\n mov x10, x9\n mov x11, x9\n mov x12, x9\n mov x13, x9\n mov x14, x9\n mov x15, x9\n mov x16, x9\n mov x17, x9\n"
                     "mov x9, #0x90000000\n msr nzcv, x9\n mov x9, #0xdead" ::: "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "cc");
    return r;
}
void dm_thread_returned(void) { abort(); }
void unimpl_c(void) { abort(); }

int main(int argc, char **argv) {
    int n = 0, bad = 0, nc = 0, ns = 0, no = 0;
    for (int f = 1; f < argc; f++) {
        FILE *fp = fopen(argv[f], "r"); if (!fp) { perror(argv[f]); return 2; }
        char kind[16]; unsigned long long ret, orig; int arg;
        while (fscanf(fp, "%llx %15s %d %llx", &ret, kind, &arg, &orig) == 4) {
            g_kind = !strcmp(kind, "svc") ? K_SVC : !strcmp(kind, "obs") ? K_OBS : (!strcmp(kind, "cntpct") || !strcmp(kind, "cntvct")) ? K_CNT : 0;
            if (!g_kind) { printf("FAIL 未知门种类 %s @%llx\n", kind, orig); bad++; continue; }
            g_arg = arg;
            uint64_t in[30], out[32], want[30];
            for (int i = 0; i < 30; i++) in[i] = 0xA000000000000000ull | ((uint64_t)i << 32) | (uint64_t)(n * 31 + i);
            memcpy(want, in, sizeof want);
            if (g_kind == K_CNT) want[arg] = MARK | (uint64_t)arg;
            if (g_kind == K_SVC) for (int i = 0; i < 8; i++) want[i] = MARK | 0x100 | (uint64_t)i;
            gate_call(in, out);
            int ok = 1;
            for (int i = 0; i < 30; i++) {
                if (i == 18) continue;
                if (out[i] != want[i]) {
                    if (ok) printf("FAIL %s 门 @%#llx arg=%d：", kind, orig, arg);
                    printf(" x%d=%#llx(应 %#llx)", i, (unsigned long long)out[i], (unsigned long long)want[i]); ok = 0;
                }
            }
            if (out[31] != 0x60000000ull) { printf("%sNZCV=%#llx(应 0x60000000)", ok ? "FAIL NZCV " : " ", (unsigned long long)out[31]); ok = 0; }   // 调用前 cmp xzr,xzr 置 Z/C | cmp xzr,xzr before the call sets Z/C
            if (out[30] != g_ret_seen) { printf("%s x30=%#llx(应 %#llx)", ok ? "FAIL x30 " : "", (unsigned long long)out[30], (unsigned long long)g_ret_seen); ok = 0; }
            if (!ok) { printf("\n"); bad++; }
            n++; nc += g_kind == K_CNT; ns += g_kind == K_SVC; no += g_kind == K_OBS;
        }
        fclose(fp);
    }
    printf("GATEFN %s 门=%d（svc %d / 计时 %d / 观测 %d）失败=%d\n", bad ? "FAIL" : "OK", n, ns, nc, no, bad);
    return bad ? 1 : 0;
}

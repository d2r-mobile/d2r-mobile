// #4 自有反例：普通函数返回（RET）不是监视器清除事件；只有 CLREX／成功的 STXR／异常或上下文切换等事件会清除。
// 本夹具只比较同一函数形状下 RET 与显式 CLREX，不证明其他清除事件、调度或通用保证。合成 ARM64，无游戏数据。
// #4 owned counterexample: a normal function return (RET) is not a monitor-clearing event; CLREX, a successful STXR,
// and exception/context-switch events can clear it. This fixture compares only RET versus an explicit CLREX in the same
// function shape; it does not prove other clearing events, scheduling or any universal guarantee. Synthetic ARM64 only.
#include <stdint.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#if defined(__aarch64__)
// 保留返回结果，防止调用被优化掉；不影响监视器。
// Consume the result so the call cannot be optimised away; this does not touch the monitor.
static volatile uint32_t sink;

// 读取独占后直接返回：没有 STXR、没有 CLREX，预留状态跨函数返回仍然有效。
// Load-exclusive then return: no STXR and no CLREX, so the reservation survives the function return.
__attribute__((naked, noinline)) static uint32_t load_then_return(void *p) {
    __asm__ volatile("ldxr w0, [x0]\nret");
}

// 读取独占后显式 CLREX 再返回：对照组，预留必须被清除。
// Load-exclusive then explicit CLREX before returning: the control that must clear the reservation.
__attribute__((naked, noinline)) static uint32_t load_then_clear(void *p) {
    __asm__ volatile("ldxr w0, [x0]\nclrex\nret");
}

// 返回 0 表示存储成功（预留仍在），1 表示失败；失败时不得写内存。
// Returns 0 when the store succeeded (reservation still live), 1 when it failed; a failed store must not write.
static int store_exclusive(uint32_t *p, uint32_t value) {
    uint32_t status;
    __asm__ volatile("stxr %w0, %w2, [%1]" : "=&r"(status) : "r"(p), "r"(value) : "memory");
    return (int)status;
}
#endif

int main(void) {
#if !defined(__aarch64__)
    // 没有执行任何 AArch64 指令：返回专用 skip 码，CTest 记为 SKIP 而不是 PASS。
    // No AArch64 instruction was executed: return the dedicated skip code so CTest records SKIP, not PASS.
    puts("SKIP (77): exclusive-monitor counterexample requires AArch64");
    return 77;
#else
    enum { ATTEMPTS = 64 };
    uint32_t word = 0;
    int preserved = 0, failed_stores = 0;
    for (int i = 0; i < ATTEMPTS; i++) {
        uint32_t before = 0x11111111u + (uint32_t)i;
        word = before;
        sink = load_then_return(&word);
        int status = store_exclusive(&word, 0xa5a5a5a5u);
        if (status == 0) {
            preserved++;
            CHECK(word == 0xa5a5a5a5u);
        } else {
            failed_stores++;
            CHECK(status == 1);
            CHECK(word == before);          // 失败存储不得写入 | a failed store must not write
        }
    }
    // 至少一次观察到“返回后预留仍有效”。其余未能观察到保留的尝试原因未知：中断、调度、上下文切换等架构事件
    // 都可能清除监视器；本夹具不建立因果，也不声称每次或通用成立。
    // Observe at least one "reservation live after return". The remaining attempts where survival was not observed have
    // an unknown cause: interrupts, scheduling or context switches can all clear the monitor. This fixture establishes no
    // causation and claims neither every run nor any universal behaviour.
    CHECK(preserved > 0);
    printf("exclusive_return: ret_preserved=%d/%d (cause-unknown non-survivals=%d)\n", preserved, ATTEMPTS, failed_stores);

    int blocked = 0, unchanged = 0;
    for (int i = 0; i < ATTEMPTS; i++) {
        uint32_t before = 0x01010101u + (uint32_t)i;
        word = before;
        sink = load_then_clear(&word);
        int status = store_exclusive(&word, 0x5a5a5a5au);
        if (status == 1) blocked++;
        if (word == before) unchanged++;
    }
    // 对照组：同一函数形状，仅多一条 CLREX，预留必须每次都被清除。
    // Control: same function shape plus one CLREX; the reservation must be cleared every time.
    CHECK(blocked == ATTEMPTS);
    CHECK(unchanged == ATTEMPTS);
    printf("exclusive_return: clrex_blocked=%d/%d unchanged=%d/%d\n", blocked, ATTEMPTS, unchanged, ATTEMPTS);
    puts("PASS: RET preserves the exclusive reservation; CLREX clears it");
    return 0;
#endif
}

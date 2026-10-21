// 合成映像测试：实际页保护与 ARM64 原生调用宿主门，不包含游戏指令或 CPU 模拟器。
// Synthetic image test: real page protections and ARM64 native host-gate calls, no game instructions or CPU emulator.
#include "../image_map.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static void protected_fault(int signal_number) { (void)signal_number; _exit(77); }

static void rejects_writes_when_code_protected(uint8_t *base) {
    // Given：已经设为 RX 的代码页。 | Given: a code page already protected as RX.
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        signal(SIGBUS, protected_fault); signal(SIGSEGV, protected_fault);
        // When：子进程尝试写代码页。 | When: the child attempts to write the code page.
        *(volatile uint8_t *)base = 0; _exit(0);
    }
    int status; CHECK(waitpid(child, &status, 0) == child);
    // Then：必须触发保护故障。 | Then: a protection fault is required.
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 77);
}

#if defined(__aarch64__)
void dm_gate(void);
void dm_thread_returned(void) { abort(); }
void unimpl_c(void) { abort(); }
static uintptr_t expected_return;
static int calls;

static void rejects_execution_when_data_protected(uint8_t *data) {
    // Given：RW 数据页中放入独立 ret 指令。 | Given: a standalone ret instruction in an RW data page.
    *(uint32_t *)data = 0xd65f03c0;
    __builtin___clear_cache((char *)data, (char *)data + 4);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        signal(SIGBUS, protected_fault); signal(SIGSEGV, protected_fault);
        // When：子进程尝试执行数据页。 | When: the child attempts to execute the data page.
        ((void (*)(void))(void *)data)(); _exit(0);
    }
    int status; CHECK(waitpid(child, &status, 0) == child);
    // Then：必须触发执行保护故障。 | Then: an execution protection fault is required.
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 77);
}

uint64_t gate_dispatch(uint64_t return_address, uint64_t *registers) {
    CHECK(return_address == expected_return);
    struct timespec now;
    CHECK(!clock_gettime(CLOCK_MONOTONIC, &now));
    CHECK(now.tv_sec > 0 || now.tv_nsec > 0);
    calls++; registers[0]++;
    return registers[0];
}

static void calls_host_when_arm64_image_entered(dm_image_mapping *mapping) {
    // Given：原生 ARM64 小程序，x1 为宿主门；输入 x0=41。
    // Given: a native ARM64 program with the host gate in x1 and x0=41.
    typedef uint64_t (*entry_fn)(uint64_t, void (*)(void));
    entry_fn entry = (entry_fn)(void *)mapping->base;
    expected_return = (uintptr_t)mapping->base + 12;
    // When：直接进入 mmap 的代码，不经指令模拟。 | When: enter mmap code directly without instruction emulation.
    uint64_t result = entry(41, dm_gate);
    // Then：真实门进入 C / 系统计时服务并回写 x0=42。 | Then: the real gate enters C / the system clock service and writes x0=42 back.
    CHECK(result == 42 && calls == 1);
}
#endif

int main(void) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    CHECK(page > 0 && page <= DM_NSO_MAX_IMAGE / 8);
    dm_nso_image image = {.header = {.image_size = page * 5, .bss_size = (uint32_t)page}};
    image.header.segments[0] = (dm_nso_segment){.size = 20};
    image.header.segments[1] = (dm_nso_segment){.memory_offset = (uint32_t)(page * 2), .size = 16};
    image.header.segments[2] = (dm_nso_segment){.memory_offset = (uint32_t)(page * 4), .size = 0};
    image.bytes = calloc(1, image.header.image_size); CHECK(image.bytes);
    // 手工小程序：stp fp,lr; mov fp,sp; blr x1; ldp fp,lr; ret。
    // Handwritten program: stp fp,lr; mov fp,sp; blr x1; ldp fp,lr; ret.
    const uint32_t program[] = {0xa9bf7bfd, 0x910003fd, 0xd63f0020, 0xa8c17bfd, 0xd65f03c0};
    for (unsigned i = 0; i < 5; i++) for (unsigned b = 0; b < 4; b++)
        image.bytes[i * 4 + b] = (uint8_t)(program[i] >> (b * 8));
    image.bytes[page * 2] = 19;
    dm_image_mapping mapping = {0};
    CHECK(dm_image_map(&image, &mapping) == DM_IMAGE_OK);
    CHECK(mapping.base[page * 2] == 19);
    CHECK(mapping.base[page * 4] == 0);
    mapping.base[page * 4] = 23;
    CHECK(mapping.base[page * 4] == 23);
    rejects_writes_when_code_protected(mapping.base);
#if defined(__aarch64__)
    rejects_execution_when_data_protected(mapping.base + page * 4);
    calls_host_when_arm64_image_entered(&mapping);
#endif
    CHECK(!dm_image_unmap(&mapping)); CHECK(!mapping.base && !mapping.size);
    image.header.segments[2].memory_offset = 0;
    CHECK(dm_image_map(&image, &mapping) == DM_IMAGE_WX_CONFLICT);
    CHECK(!mapping.base && !mapping.size);
    image.header.segments[2].memory_offset = (uint32_t)image.header.image_size;
    CHECK(dm_image_map(&image, &mapping) == DM_IMAGE_LAYOUT);
    dm_nso_free(&image);
    puts("PASS: runtime page protections, read-only code, writable BSS, W^X rejection, bounds");
#if defined(__aarch64__)
    puts("PASS: ARM64 native mapped-code -> host gate -> system clock -> guest return");
#endif
    return 0;
}

// 自有 IPC 传输夹具：真实 SVC 0x1f/0x21 语义、句柄代际、畸形包、耗尽回滚与关闭竞态。
// Owned IPC transport fixture: real SVC 0x1f/0x21 semantics, handle generations, malformed packets, exhaustion rollback and close races.
#include "../runtime/host.h"
#include "../runtime/ipc.h"
#include "../runtime/lm.h"
#include "../runtime/lm_wire.h"
#include "../runtime/sm.h"
#include "../runtime/internal.h"
#include <errno.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define UNIT (2u * 1024u * 1024u)
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static dm_host *host;
static dm_guest_context *context;
static uint8_t *tls;

extern int test_output_pin_fail;
extern dm_host *test_output_pin_host;
extern uintptr_t test_output_pin_tls;
extern int test_output_pin_calls;
extern int test_output_pin_injections;
extern uint32_t test_created_handle;

typedef struct {
    dm_host *host;
    uint32_t handle;
    int calls;
    uint32_t reentrant;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int entered, release;
} fixture;
static fixture state;

static uint32_t get32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void put32(void *p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(void *p, uint16_t v) { memcpy(p, &v, 2); }

// 夹具端点：真实有界请求/响应，命令 3 阻塞以便关闭竞态，命令 4 重入同一句柄。
// Fixture endpoint: real bounded request/response; command 3 blocks for the close race, command 4 re-enters the same handle.
static int endpoint(void *user, uint64_t client, unsigned flags, uint32_t command,
                    const uint8_t *request, size_t size,
                    uint8_t *reply, size_t capacity, size_t *reply_size, uint32_t *out_handle) {
    fixture *f = user;
    (void)client; (void)flags; (void)out_handle;
    f->calls++;
    if (command == 1) {
        if (size != 8 || capacity < 8) return -1;
        uint64_t value; memcpy(&value, request, 8); value++;
        memcpy(reply, &value, 8); *reply_size = 8; return 0;
    }
    if (command == 2) return -1;
    if (command == 3) {
        pthread_mutex_lock(&f->lock); f->entered = 1; pthread_cond_broadcast(&f->condition);
        while (!f->release) pthread_cond_wait(&f->condition, &f->lock);
        pthread_mutex_unlock(&f->lock);
        uint32_t value = 0x13572468u; memcpy(reply, &value, 4); *reply_size = 4; return 0;
    }
    if (command == 4) {
        uint64_t registers[31] = {0};
        registers[0] = f->handle;
        uint64_t result = dm_host_svc(f->host, 0x21, registers);
        f->reentrant = (uint32_t)result;
        uint32_t value = 0x24681357u; memcpy(reply, &value, 4); *reply_size = 4; return 0;
    }
    if (command == 5) { *reply_size = 0; return 0; }
    return -1;
}

// 客户端桩行为：x0 保存 C 出参指针，x1 传名称；返回后库桩把 w1 写到 [x0]。
// Client stub behaviour: x0 holds the C out-pointer, x1 the name; after return the library stub stores w1 to [x0].
static uint64_t connect_raw(uintptr_t out_pointer, uintptr_t name_pointer, uint32_t *returned) {
    uint64_t registers[31] = {0};
    registers[0] = out_pointer; registers[1] = name_pointer;
    uint64_t result = dm_host_svc(host, 0x1f, registers);
    if (returned) *returned = (uint32_t)registers[1];
    return result;
}
static uint32_t connect_name(const char *name) {
    memset(tls, 0, 0x200);
    strcpy((char *)tls + 0x100, name);
    uint32_t handle = 0;
    uint64_t result = connect_raw(0x11111111u, (uintptr_t)(tls + 0x100), &handle);
    CHECK(result == 0);
    CHECK(handle != 0);
    return handle;
}
static uint64_t send_request(uint32_t handle) {
    uint64_t registers[31] = {0};
    registers[0] = handle;
    return dm_host_svc(host, 0x21, registers);
}
static uint32_t build(uint32_t command, const void *payload, size_t size, uint32_t token) {
    memset(tls, 0x5a, DM_IPC_BUFFER_SIZE);
    uint32_t words = (uint32_t)((24u + size + 3) / 4);
    CHECK(32u + size <= DM_IPC_BUFFER_SIZE);
    put32(tls, 4u);
    put32(tls + 4, words);
    memset(tls + 8, 0xa5, 8); // 库构造器不初始化 8..16 对齐字节。 / the library builder leaves 8..16 alignment bytes untouched.
    put32(tls + 16, 0x49434653u);
    put32(tls + 20, 0);
    put32(tls + 24, command);
    put32(tls + 28, token);
    if (size) memcpy(tls + 32, payload, size);
    return words;
}
static void expect_reply(uint32_t token, const void *payload, size_t size) {
    uint32_t words = (uint32_t)((24u + size + 3) / 4);
    CHECK(get32(tls) == 0);
    CHECK((get32(tls + 4) & 0x3ffu) == words);
    CHECK((get32(tls + 4) >> 10) == 0);
    CHECK(get32(tls + 16) == 0x4F434653u);
    CHECK(get32(tls + 20) == 0);
    CHECK(get32(tls + 24) == 0);
    CHECK(get32(tls + 28) == token);
    if (size) CHECK(!memcmp(tls + 32, payload, size));
    for (unsigned i = 8; i < 16; i++) CHECK(tls[i] == 0);
    CHECK(tls[8 + words * 4] == 0x5a); // 已发布长度之外保持原值。 / bytes beyond the published extent stay untouched.
}
static uint64_t close_handle(uint32_t handle) {
    uint64_t registers[31] = {0};
    registers[0] = handle;
    return dm_host_svc(host, 0x16, registers);
}
static uint64_t session_control(unsigned number, uint32_t handle) {
    uint64_t registers[31] = {0};
    registers[0] = handle; registers[1] = handle;
    return dm_host_svc(host, number, registers);
}

// 控制消息：type7 / SFCI v1 / 8 数据字；8..15 与 32..39 故意填非零，验证不按保留零拒绝。
// Control messages: type7 / SFCI v1 / 8 data words; bytes 8..15 and 32..39 are deliberately nonzero.
static void build_control(uint32_t command, uint32_t token) {
    memset(tls, 0x5a, DM_IPC_BUFFER_SIZE);
    put32(tls, 7u);
    put32(tls + 4, 8u);
    memset(tls + 8, 0xa5, 8);
    put32(tls + 16, 0x49434653u);
    put32(tls + 20, 1u);
    put32(tls + 24, command);
    put32(tls + 28, token);
    memset(tls + 32, 0x3c, 8);
}
static void expect_control_reply(uint32_t token, const void *payload, size_t size) {
    CHECK(get32(tls) == 0);                    // 客户端不检查响应 type。 / response type is not checked by the client.
    CHECK((get32(tls + 4) & 0x3ffu) == 7u);    // 8-byte pad + 16-byte CMIF + payload. / includes the 8-byte pad.
    CHECK(get32(tls + 16) == 0x4F434653u);
    CHECK(get32(tls + 24) == 0);
    CHECK(get32(tls + 28) == token);
    if (size) CHECK(!memcmp(tls + 32, payload, size));
    for (unsigned i = 8; i < 16; i++) CHECK(tls[i] == 0);
    CHECK(tls[36] == 0x3c);                    // 请求填充未被覆盖，只发布 36 字节。 / request padding survives; exactly 36 bytes published.
}

// 控制测试块。
// Control test block.
static void control_tests(void) {
    uint32_t session = connect_name("dm:fixture");
    build_control(3, 0x11223344u);
    CHECK(send_request(session) == 0);
    { uint16_t zero = 0; expect_control_reply(0x11223344u, &zero, sizeof zero); }

    build_control(0, 0x55667788u);
    CHECK(send_request(session) == 0);
    uint32_t id = get32(tls + 32);
    CHECK(id != 0);
    expect_control_reply(0x55667788u, &id, sizeof id);
    // 重复转换返回同一 ID，不重复分配。 / duplicate conversion returns the same id, no new allocation.
    build_control(0, 0x99aabbccu);
    CHECK(send_request(session) == 0);
    CHECK(get32(tls + 32) == id);
    // 转换后的普通请求必须显式失败：不触达原处理器、不发布响应、旧句柄失效。
    // After conversion an ordinary request must fail explicitly: no handler, no reply, stale handle.
    {
        int calls = state.calls;
        build(5, NULL, 0, 0x1234u);
        uint8_t before[DM_IPC_BUFFER_SIZE];
        memcpy(before, tls, sizeof before);
        CHECK(send_request(session) == 0x4201);
        CHECK(state.calls == calls);
        CHECK(!memcmp(before, tls, sizeof before));
        CHECK(close_handle(session) == 0xe401);
    }

    // 另一会话得到不同 ID。 / a second session gets a different id.
    uint32_t other = connect_name("dm:fixture");
    build_control(0, 0x01020304u);
    CHECK(send_request(other) == 0);
    uint32_t id2 = get32(tls + 32);
    CHECK(id2 != 0); // 会话作用域，不同会话可同为 1。 / session-scoped; equal values across sessions are allowed.
    CHECK(close_handle(other) == 0);

    // 未知命令、错误版本、带描述符、错误数据字数均明确失败。
    // Unknown command, wrong version, descriptor bit, wrong data-word count fail explicitly.
    uint32_t bad = connect_name("dm:fixture");
    build_control(4, 1); CHECK(send_request(bad) == 0x4201);
    bad = connect_name("dm:fixture");
    build_control(0, 1); put32(tls + 20, 0); CHECK(send_request(bad) == 0x4201);
    bad = connect_name("dm:fixture");
    build_control(0, 1); put32(tls, get32(tls) | 0x10000u); CHECK(send_request(bad) == 0x4201);
    bad = connect_name("dm:fixture");
    build_control(0, 1); put32(tls + 4, 7u); CHECK(send_request(bad) == 0x4201);

    // 关闭后旧句柄/旧转换状态不可用。 / closed handle and its converted state are not reusable.
    CHECK(close_handle(bad) == 0xe401);
}

static void build_openlogger(uint32_t root, uint32_t token, uint64_t arg) {
    memset(tls, 0x5a, DM_IPC_BUFFER_SIZE);
    put32(tls, 6u); put32(tls + 4, 14u | 0x80000000u);
    put32(tls + 8, 1u); memset(tls + 12, 0xee, 8);       // PID 槽故意不可信。 / untrusted PID slot.
    memset(tls + 20, 0xa5, 12);                           // 数据起始前的未写对齐字节。 / unwritten alignment bytes.
    memset(tls + 32, 0, 16);
    tls[32] = 1; put32(tls + 34, 0x00180000u);            // type1,num0,data_size0x18
    put32(tls + 34, 24u); put32(tls + 36, root); put32(tls + 44, token);
    put32(tls + 48, 0x49434653u); put32(tls + 52, 1u); put32(tls + 56, 0u); put32(tls + 60, 0u);
    memcpy(tls + 64, &arg, 8);
    memset(tls + 72, 0x3c, 4);                            // 声明范围内的未写填充。 / unwritten padding inside extent.
}
static void build_domain_close(uint32_t id, uint32_t token) {
    memset(tls, 0x5a, DM_IPC_BUFFER_SIZE);
    put32(tls, 6u); put32(tls + 4, 8u);
    memset(tls + 8, 0xa5, 8);
    tls[16] = 2; put32(tls + 20, id); put32(tls + 28, token);
    memset(tls + 32, 0x3c, 8);
}
static void domain_tests(void) {
    uint32_t lm = connect_name("lm");
    build_control(0, 0x21u);
    CHECK(send_request(lm) == 0);
    uint32_t root = get32(tls + 32);
    CHECK(root != 0);
    CHECK(root == 1u);                       // 新会话根=1。 / fresh session root = 1.

    build_openlogger(root, 0x31u, 0x1122334455667788ull);
    CHECK(send_request(lm) == 0);
    CHECK(get32(tls) == 0);
    CHECK(get32(tls + 4) == 11u);
    CHECK(get32(tls + 16) == 1u);                 // domain out count
    CHECK(get32(tls + 32) == 0x4F434653u);
    CHECK(get32(tls + 40) == 0);
    CHECK(get32(tls + 44) == 0x31u);
    uint32_t child = get32(tls + 48);
    CHECK(child == 2u);                       // 根与子对象共享命名空间。 / root and child share the namespace.
    CHECK(child != root);
    CHECK(child != 0 && tls[52] == 1); // 52 字节发布，后面不动。 / exactly 52 bytes.

    build_domain_close(child, 0x32u);
    CHECK(send_request(lm) == 0);
    CHECK(get32(tls) == 0 && get32(tls + 4) == 0); // empty standard reply

    // 重复关闭必须失败，且不发布；错误关闭会话。
    build_domain_close(child, 0x33u);
    CHECK(send_request(lm) == 0x4201);
    CHECK(close_handle(lm) == 0xe401);

    // 错误根 ID 拒绝。 / wrong root id rejected.
    lm = connect_name("lm"); build_control(0, 0x22u); CHECK(send_request(lm) == 0);
    root = get32(tls + 32);
    build_openlogger(root + 1u, 0x34u, 0);
    CHECK(send_request(lm) == 0x4201);

    // 未转换会话的域请求拒绝。 / domain request on an unconverted session rejected.
    lm = connect_name("lm");
    build_openlogger(1u, 0x35u, 0);
    CHECK(send_request(lm) == 0x4201);

    // 域帧错误：数据字数、CMIF 版本、描述符位均明确失败。
    // Domain frame errors: word count, CMIF version, descriptor bit fail explicitly.
    lm = connect_name("lm"); build_control(0, 0x25u); CHECK(send_request(lm) == 0);
    root = get32(tls + 32);
    build_openlogger(root, 0x36u, 0); put32(tls + 4, 13u | 0x80000000u); CHECK(send_request(lm) == 0x4201);
    lm = connect_name("lm"); build_control(0, 0x26u); CHECK(send_request(lm) == 0);
    root = get32(tls + 32);
    build_openlogger(root, 0x37u, 0); put32(tls + 52, 0u); CHECK(send_request(lm) == 0x4201);
    lm = connect_name("lm"); build_control(0, 0x27u); CHECK(send_request(lm) == 0);
    root = get32(tls + 32);
    build_openlogger(root, 0x38u, 0); put32(tls, get32(tls) | 0x10000u); CHECK(send_request(lm) == 0x4201);

    // 资源耗尽与回滚。 / exhaustion and rollback.
    lm = connect_name("lm"); build_control(0, 0x23u); CHECK(send_request(lm) == 0);
    root = get32(tls + 32);
    uint32_t ids[8];
    for (unsigned i = 0; i < 8; i++) {
        build_openlogger(root, 0x40u + i, i);
        CHECK(send_request(lm) == 0);
        ids[i] = get32(tls + 48);
        CHECK(ids[i] != 0);
        for (unsigned j = 0; j < i; j++) CHECK(ids[i] != ids[j]);
    }
    build_openlogger(root, 0x50u, 9);
    CHECK(send_request(lm) == 0xce01);
    // 失败回复关闭会话；重建并只创建两个后关闭一个，再创建应成功。
    lm = connect_name("lm"); build_control(0, 0x24u); CHECK(send_request(lm) == 0);
    root = get32(tls + 32);
    build_openlogger(root, 0x51u, 0); CHECK(send_request(lm) == 0); uint32_t a = get32(tls + 48);
    build_openlogger(root, 0x52u, 1); CHECK(send_request(lm) == 0);
    build_domain_close(a, 0x53u); CHECK(send_request(lm) == 0);
    build_openlogger(root, 0x54u, 2); CHECK(send_request(lm) == 0); // slot reuse
    CHECK(close_handle(lm) == 0);

    // 根关闭后子对象仍可关闭；根不可再转换/打开；重复根关闭失败。
    // Root retirement keeps live children closable; no reconversion/OpenLogger; duplicate root close fails.
    lm = connect_name("lm"); build_control(0, 0x28u); CHECK(send_request(lm) == 0);
    root = get32(tls + 32);
    build_openlogger(root, 0x60u, 0); CHECK(send_request(lm) == 0);
    uint32_t live = get32(tls + 48);
    build_domain_close(root, 0x61u); CHECK(send_request(lm) == 0);
    build_domain_close(live, 0x62u); CHECK(send_request(lm) == 0); // child after root
    build_domain_close(root, 0x63u); CHECK(send_request(lm) == 0x4201); // duplicate root
}

static uint8_t log_rest[DM_IPC_BUFFER_SIZE - 48u];
#define WIRE_ADDRESS_LIMIT (1ull << 39)
// Linux x86_64 ASAN's application LowMem ends at 0x7fff7fff; its shadow gap
// starts at 0x8fff7000 and extends far above. Hints at 0x1_00000000 or higher
// can therefore be mapped but still fault on the first instrumented access.
// Use only bounded, non-fixed hints inside LowMem on that target. macOS has a
// different layout and accepts the 4 GiB hint range, so keep it separate.
static uint8_t *wire_page(unsigned slot) {
#if defined(__linux__) && defined(__x86_64__)
    const uintptr_t hints[] = {
        0x10000000ull + (uintptr_t)slot * 0x1000000ull,
        0x20000000ull + (uintptr_t)slot * 0x1000000ull,
        0x40000000ull + (uintptr_t)slot * 0x1000000ull,
    };
    const uintptr_t low_max = 0x80000000ull;
#else
    const uintptr_t hints[] = {
        0x100000000ull + (uintptr_t)slot * 0x10000ull,
        0x100010000ull + (uintptr_t)slot * 0x10000ull,
    };
    const uintptr_t low_max = WIRE_ADDRESS_LIMIT;
#endif
    for (unsigned h = 0; h < sizeof hints / sizeof hints[0]; h++) {
        for (unsigned retry = 0; retry < 4; retry++) {
            uintptr_t hint = hints[h] + (uintptr_t)retry * 0x1000ull;
            uint8_t *p = mmap((void *)hint, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANON, -1, 0);
            if (p == MAP_FAILED) continue;
            uintptr_t address = (uintptr_t)p;
#if defined(__linux__) && defined(__x86_64__)
            if (address >= hint && address < low_max &&
                low_max - address >= 4096) return p;
#else
            if (address >= hint && address - hint < 0x40000000ull &&
                address < low_max && low_max - address >= 4096) return p;
#endif
            CHECK(munmap(p, 4096) == 0);
        }
    }
    return NULL;
}
static void build_log(uint32_t child, uint32_t token, uintptr_t pointer, uint32_t length) {
    CHECK(pointer != 0 && pointer < WIRE_ADDRESS_LIMIT);
    CHECK(length != 0 && (uint64_t)length <= WIRE_ADDRESS_LIMIT - pointer);
    memset(tls, 0x5a, DM_IPC_BUFFER_SIZE);
    put32(tls, 0x00110006u); put32(tls + 4, 12u);
    memset(tls + 8, 0, 8);                         // null static descriptor
    put32(tls + 16, length);
    put32(tls + 20, (uint32_t)pointer);
    uint32_t packed = ((uint32_t)((pointer >> 32) & 0xfu) << 28) |
                      ((uint32_t)((pointer >> 34) & 0x1cu)) |
                      (uint32_t)((length >> 8) & 0x0f000000u);
    put32(tls + 24, packed);
    memset(tls + 28, 0xcc, 4);                     // ignored padding
    tls[32] = 1; tls[33] = 0; put16(tls + 34, 0x10u);
    put32(tls + 36, child); put32(tls + 40, 0); put32(tls + 44, token);
    put32(tls + 48, 0x49434653u); put32(tls + 52, 1u);
    put32(tls + 56, 0u); put32(tls + 60, 0u);
    memset(tls + 64, 0x3c, 12);                    // ignored trailing padding
    dm_lm_wire_log_request decoded;
    CHECK(dm_lm_wire_decode_log_request(tls, 76u, &decoded) == DM_LM_WIRE_OK);
    CHECK(decoded.pointer == pointer && decoded.length == length &&
          decoded.object_id == child && decoded.token == token);
    memcpy(log_rest, tls + 48, sizeof log_rest);   // bytes beyond the 48-byte reply
}
static void expect_log_reply(uint32_t token) {
    CHECK(get32(tls) == 0);
    CHECK(get32(tls + 4) == 10u);
    CHECK(get32(tls + 16) == 0u);
    for (unsigned i = 20; i < 32; i++) CHECK(tls[i] == 0);
    CHECK(get32(tls + 32) == 0x4f434653u);
    CHECK(get32(tls + 36) == 1u);
    CHECK(get32(tls + 40) == 0u);
    CHECK(get32(tls + 44) == token);
    CHECK(!memcmp(tls + 48, log_rest, sizeof log_rest));
}
static uint32_t log_session(uint32_t token, uint32_t *root, uint32_t *child) {
    uint32_t h = connect_name("lm");
    build_control(0, token); CHECK(send_request(h) == 0);
    *root = get32(tls + 32); CHECK(*root != 0);
    build_openlogger(*root, token + 1, 0); CHECK(send_request(h) == 0);
    *child = get32(tls + 48); CHECK(*child != 0);
    return h;
}
static void log_tests(uint8_t *base, uint8_t *ro) {
    // 真实映射由调用方在 attach 前登记；pin/复制必须走真实宿主路径。
    // Caller registers the real mapping before attach; pin/copy uses real host paths.
    CHECK(base != NULL && ro != NULL);
    uint8_t original[DM_LM_PACKET_MAX];
    for (unsigned i = 0; i < sizeof original; i++) base[i] = original[i] = (uint8_t)(i * 7u + 3u);

    uint32_t root = 0, child = 0, h = log_session(0x101u, &root, &child);
    dm_lm_packet packet;
    build_log(child, 0x201u, (uintptr_t)base, 1);
    CHECK(send_request(h) == 0); expect_log_reply(0x201u);
    CHECK(!memcmp(base, original, 1));
    CHECK(dm_lm_packet_count(host) == 1);
    CHECK(dm_lm_take_packet(host, &packet) == 0);
    CHECK(packet.client == h && packet.object_id == child && packet.length == 1);
    CHECK(packet.data[0] == original[0]);
    CHECK(dm_lm_packet_count(host) == 0);
    // len1024 and source immutability / post-48 sentinel.
    build_log(child, 0x202u, (uintptr_t)base, 1024);
    CHECK(send_request(h) == 0); expect_log_reply(0x202u);
    CHECK(!memcmp(base, original, sizeof original));
    CHECK(dm_lm_take_packet(host, &packet) == 0);
    CHECK(packet.length == 1024 && !memcmp(packet.data, original, sizeof original));
    // Read-only registered executable-style mapping is accepted by the strict READ pin.
    build_log(child, 0x20bu, (uintptr_t)ro, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_packet_count(host) == 1);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.data[0] == 0x99);
    // Root is not a child.
    build_log(root, 0x203u, (uintptr_t)base, 1);
    CHECK(send_request(h) != 0); CHECK(dm_lm_packet_count(host) == 0);
    h = log_session(0x102u, &root, &child);
    // Closed child rejects.
    build_domain_close(child, 0x204u); CHECK(send_request(h) == 0);
    build_log(child, 0x205u, (uintptr_t)base, 1);
    CHECK(send_request(h) != 0); CHECK(dm_lm_packet_count(host) == 0);
    // Identical numeric ids are session-scoped and must work independently.
    uint32_t root2 = 0, child2 = 0;
    uint32_t h2 = log_session(0x103u, &root2, &child2);
    build_openlogger(root2, 0x109u, 0); CHECK(send_request(h2) == 0);
    uint32_t foreign = get32(tls + 48);             // id 3 exists only in h2.
    h = log_session(0x104u, &root, &child);
    build_log(child, 0x206u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    build_log(child2, 0x207u, (uintptr_t)base, 1); CHECK(send_request(h2) == 0);
    CHECK(dm_lm_packet_count(host) == 2);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.client == h && packet.object_id == child);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.client == h2 && packet.object_id == child2);
    // Foreign numeric id absent from target session rejects.
    build_log(foreign, 0x208u, (uintptr_t)base, 1);
    CHECK(send_request(h) != 0); CHECK(dm_lm_packet_count(host) == 0);
    CHECK(close_handle(h2) == 0);
    // Root retirement leaves a live child usable for Log.
    h = log_session(0x105u, &root, &child);
    build_domain_close(root, 0x209u); CHECK(send_request(h) == 0);
    build_log(child, 0x20au, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_packet_count(host) == 1);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.object_id == child);
    // FIFO across undrained same-child packets and slot reuse.
    base[0] = 0x11; build_log(child, 0x230u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    base[0] = 0x22; build_log(child, 0x231u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    base[0] = 0x33; build_log(child, 0x232u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_packet_count(host) == 3);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.data[0] == 0x11);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.data[0] == 0x22);
    base[0] = 0x44; build_log(child, 0x233u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.data[0] == 0x33);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.data[0] == 0x44);
    base[0] = original[0];
    CHECK(close_handle(h) == 0);
    // Unreadable and cross-gap source reject without retention.
    h = log_session(0x106u, &root, &child);
    build_log(child, 0x207u, (uintptr_t)base + 0x100000u, 1);
    CHECK(send_request(h) != 0); CHECK(dm_lm_packet_count(host) == 0);
    h = log_session(0x107u, &root, &child);
    build_log(child, 0x208u, (uintptr_t)base + 4096u - 512u, 1024);
    CHECK(send_request(h) != 0); CHECK(dm_lm_packet_count(host) == 0);
    // Queue capacity: 4 retained; fifth fails, TLS unchanged and cleanup drops all.
    h = log_session(0x108u, &root, &child);
    for (unsigned i = 0; i < DM_LM_QUEUE_PACKETS; i++) {
        base[0] = (uint8_t)(0x50u + i);
        build_log(child, 0x210u + i, (uintptr_t)base, 1);
        CHECK(send_request(h) == 0); expect_log_reply(0x210u + i);
    }
    CHECK(dm_lm_packet_count(host) == DM_LM_QUEUE_PACKETS);
    base[0] = 0x60; build_log(child, 0x220u, (uintptr_t)base, 1);
    uint8_t before[DM_IPC_BUFFER_SIZE]; memcpy(before, tls, sizeof before);
    CHECK(send_request(h) == 0xce01);
    CHECK(!memcmp(before, tls, sizeof before));
    CHECK(dm_lm_packet_count(host) == 0);           // closer drops staged/ready on error close
    CHECK(dm_lm_take_packet(host, &packet) == ENOENT);
    CHECK(close_handle(h) == 0xe401);                // already closed by error rule
    // Close cleanup on a normal session with a packet.
    h = log_session(0x10au, &root, &child);
    build_log(child, 0x221u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_packet_count(host) == 1);
    CHECK(close_handle(h) == 0);
    CHECK(dm_lm_packet_count(host) == 0);
    CHECK(dm_lm_take_packet(host, &packet) == ENOENT);
    base[0] = original[0];
}

static unsigned sink_calls, sink_fail_at, sink_seen_n, sink_reentrant_rc, sink_take_rc;
static uint8_t sink_seen[16];
static pthread_mutex_t block_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t block_cond = PTHREAD_COND_INITIALIZER;
static int block_entered, block_release, block_take_rc, block_set_rc, consume_result;

static int consumer_sink(void *user, const dm_lm_packet *packet) {
    sink_calls++;
    // 锁外回调可安全查询宿主状态；不得在此销毁宿主。 | Out-of-lock callback may query host state; must not destroy it.
    (void)dm_lm_packet_count((dm_host *)user);
    if (sink_fail_at && sink_calls == sink_fail_at) {
        dm_lm_packet stolen;
        sink_take_rc = (unsigned)dm_lm_take_packet((dm_host *)user, &stolen);
        return -1;
    }
    if (sink_seen_n < sizeof sink_seen) sink_seen[sink_seen_n++] = packet->data[0];
    return 0;
}

static int reentrant_sink(void *user, const dm_lm_packet *packet) {
    (void)packet;
    sink_calls++;
    sink_reentrant_rc = (unsigned)dm_lm_consume((dm_host *)user, 1);
    return -1;   // 保留该包，证明重入不吞包 | retain the packet; proves reentry cannot consume it
}

static int blocking_sink(void *user, const dm_lm_packet *packet) {
    (void)packet;
    dm_lm_packet stolen;
    pthread_mutex_lock(&block_mutex);
    block_entered = 1;
    pthread_cond_broadcast(&block_cond);
    while (!block_release) pthread_cond_wait(&block_cond, &block_mutex);
    pthread_mutex_unlock(&block_mutex);
    block_take_rc = dm_lm_take_packet((dm_host *)user, &stolen);
    block_set_rc = dm_lm_set_sink((dm_host *)user, NULL, NULL);
    return 0;
}

static void *consume_thread(void *arg) {
    consume_result = dm_lm_consume((dm_host *)arg, 4);
    return NULL;
}

static void sink_reset(void) { sink_calls = sink_seen_n = sink_reentrant_rc = sink_take_rc = 0; sink_fail_at = 0; memset(sink_seen, 0, sizeof sink_seen); }

static void consumer_tests(uint8_t *base) {
    uint32_t root = 0, child = 0;
    uint32_t h = log_session(0x401u, &root, &child);
    dm_lm_packet packet;
    sink_reset();
    CHECK(dm_lm_consume(NULL, 1) == -EINVAL);
    CHECK(dm_lm_consume(host, 0) == -EINVAL);

    // 未配置 sink：显式背压，不丢包。 | No sink: explicit backpressure, no loss.
    CHECK(dm_lm_set_sink(host, NULL, NULL) == 0);
    base[0] = 1; build_log(child, 0x410u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_consume(host, 1) == -EAGAIN);
    CHECK(dm_lm_packet_count(host) == 1);

    // >4 包严格按 seq 顺序；满队列第五个失败。 | More than four packets in exact seq order; fifth fails when full.
    CHECK(dm_lm_set_sink(host, consumer_sink, host) == 0);
    CHECK(dm_lm_consume(host, 1) == 1);                       // 1
    base[0] = 2; build_log(child, 0x411u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    base[0] = 3; build_log(child, 0x412u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    base[0] = 4; build_log(child, 0x413u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_consume(host, 2) == 2);                       // 2,3
    base[0] = 5; build_log(child, 0x414u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    base[0] = 6; build_log(child, 0x415u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_consume(host, 3) == 3);                       // 4,5,6
    CHECK(sink_seen_n == 6);
    for (unsigned i = 0; i < 6; i++) CHECK(sink_seen[i] == i + 1);
    CHECK(dm_lm_packet_count(host) == 0);

    // sink 失败：保留并重试，不丢失、不重复。 | Sink failure: retained and retried, no loss/duplication.
    sink_reset(); sink_fail_at = 1;
    base[0] = 7; build_log(child, 0x416u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    base[0] = 8; build_log(child, 0x417u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_consume(host, 10) == -EIO);
    CHECK(dm_lm_packet_count(host) == 2 && sink_seen_n == 0);
    CHECK(sink_take_rc == EBUSY);                    // 失败包未被下一包绕过 | failed first packet not bypassed
    CHECK(dm_lm_consume(host, 10) == 2);
    CHECK(sink_seen_n == 2 && sink_seen[0] == 7 && sink_seen[1] == 8);
    CHECK(dm_lm_packet_count(host) == 0);

    // 停止/恢复：暂停只保留，不丢弃。 | Stop/resume: pause retains, never drops.
    base[0] = 9; build_log(child, 0x418u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_set_sink(host, NULL, NULL) == 0);
    CHECK(dm_lm_consume(host, 1) == -EAGAIN && dm_lm_packet_count(host) == 1);
    CHECK(dm_lm_set_sink(host, consumer_sink, host) == 0);
    CHECK(dm_lm_consume(host, 1) == 1 && sink_seen_n == 3 && sink_seen[2] == 9);

    // 回调重入消费被拒绝，包仍保留。 | Reentrant consumption is refused and the packet stays pending.
    sink_reset();
    base[0] = 10; build_log(child, 0x419u, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_set_sink(host, reentrant_sink, host) == 0);
    CHECK(dm_lm_consume(host, 1) == -EIO);
    CHECK(sink_reentrant_rc == (unsigned)-EBUSY);
    CHECK(dm_lm_packet_count(host) == 1);
    CHECK(dm_lm_set_sink(host, consumer_sink, host) == 0);
    CHECK(dm_lm_consume(host, 1) == 1 && sink_seen_n == 1 && sink_seen[0] == 10);
    CHECK(dm_lm_take_packet(host, &packet) == ENOENT);

    // 会话关闭时待处理包被清理，不投递、不泄漏。 | Session close drops pending packets; nothing is delivered/leaked.
    sink_reset();
    base[0] = 11; build_log(child, 0x41au, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    base[0] = 12; build_log(child, 0x41bu, (uintptr_t)base, 1); CHECK(send_request(h) == 0);
    CHECK(dm_lm_packet_count(host) == 2);
    CHECK(close_handle(h) == 0);
    CHECK(dm_lm_packet_count(host) == 0);
    CHECK(dm_lm_consume(host, 4) == -ENOENT && sink_calls == 0);

    // 满队列：既有 IPC 明确报错并关闭会话，包由 closer 清理；consumer 不静默丢弃。
    // Full queue: existing IPC returns an explicit error and closes the session; the closer drops packets.
    // The consumer never discards silently.
    sink_reset();
    h = log_session(0x402u, &root, &child);
    CHECK(dm_lm_set_sink(host, consumer_sink, host) == 0);
    for (unsigned i = 0; i < DM_LM_QUEUE_PACKETS; i++) {
        base[0] = (uint8_t)(0x30u + i);
        build_log(child, 0x420u + i, (uintptr_t)base, 1);
        CHECK(send_request(h) == 0);
    }
    CHECK(dm_lm_packet_count(host) == DM_LM_QUEUE_PACKETS);
    base[0] = 0x34;
    build_log(child, 0x424u, (uintptr_t)base, 1);
    CHECK(send_request(h) == 0xce01);
    CHECK(dm_lm_packet_count(host) == 0);
    CHECK(dm_lm_consume(host, 4) == -ENOENT && sink_calls == 0);
}

static void build_register(uint32_t command, uint32_t token, int special, uint32_t words);
static void expect_empty_reply(uint32_t token);
static void build_get_service(const char *name, uint32_t token);
// Actual product discovery path: connect sm:, RegisterClient, GetServiceHandle
// for "lm", domain conversion and OpenLogger. The test uses the real runtime
// handlers; it does not register a substitute lm/sm service.
static uint32_t log_session_from_sm(uint32_t token, uint32_t *root, uint32_t *child) {
    uint32_t sm = connect_name("sm:");
    build_register(0, token, 1, 10);
    CHECK(send_request(sm) == 0);
    expect_empty_reply(token);
    build_get_service("lm", token + 1u);
    CHECK(send_request(sm) == 0);
    CHECK(get32(tls) == 0);
    CHECK((get32(tls + 4) & 0x3ffu) == 4u);
    CHECK((get32(tls + 4) >> 31) == 1u);
    CHECK(get32(tls + 8) == (1u << 5));
    uint32_t lm = get32(tls + 12);
    CHECK(lm != 0);
    CHECK(get32(tls + 16) == 0x4F434653u);
    CHECK(get32(tls + 24) == 0);
    CHECK(get32(tls + 28) == token + 1u);
    CHECK(close_handle(sm) == 0);                 // sm registration retires; lm session survives.
    build_control(0, token + 2u);
    CHECK(send_request(lm) == 0);
    *root = get32(tls + 32);
    CHECK(*root != 0);
    build_openlogger(*root, token + 3u, 0);
    CHECK(send_request(lm) == 0);
    CHECK(get32(tls + 16) == 1u);
    CHECK(get32(tls + 32) == 0x4F434653u);
    CHECK(get32(tls + 40) == 0u);
    CHECK(get32(tls + 44) == token + 3u);
    *child = get32(tls + 48);
    CHECK(*child != 0);
    CHECK(tls[52] == 1);                          // exact 52-byte OpenLogger reply.
    return lm;
}

static void sm_lm_discovery_tests(uint8_t *base) {
    CHECK(base != NULL);
    size_t baseline_sessions = dm_host_ipc_open_sessions(host);
    size_t baseline_clients = dm_sm_registered_clients(host);
    uint8_t before[DM_IPC_BUFFER_SIZE];
    dm_lm_packet packet;

    // 1. GetServiceHandle before RegisterClient: explicit failure, no reply,
    // no service session and no leaked sm registration.
    uint32_t sm = connect_name("sm:");
    build_get_service("lm", 0x501u);
    memcpy(before, tls, sizeof before);
    CHECK(send_request(sm) == 0x4201);
    CHECK(!memcmp(before, tls, sizeof before));
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions);
    CHECK(dm_sm_registered_clients(host) == baseline_clients);
    CHECK(close_handle(sm) == 0xe401);

    // 2. RegisterClient without the special header must fail; no registration,
    // no reply, no service session and no leaked handle.
    sm = connect_name("sm:");
    build_register(0, 0x504u, 0, 10);
    memcpy(before, tls, sizeof before);
    CHECK(send_request(sm) != 0);
    CHECK(!memcmp(before, tls, sizeof before));
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions);
    CHECK(dm_sm_registered_clients(host) == baseline_clients);
    CHECK(close_handle(sm) == 0xe401);

    // 3. Register the sm client, then an unknown service must fail without
    // creating a service session or a partial reply.
    sm = connect_name("sm:");
    build_register(0, 0x502u, 1, 10);
    CHECK(send_request(sm) == 0);
    expect_empty_reply(0x502u);
    CHECK(dm_sm_registered_clients(host) == baseline_clients + 1);
    build_get_service("nope:", 0x503u);
    memcpy(before, tls, sizeof before);
    CHECK(send_request(sm) == 0x4201);
    CHECK(!memcmp(before, tls, sizeof before));
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions);
    CHECK(dm_sm_registered_clients(host) == baseline_clients);
    CHECK(close_handle(sm) == 0xe401);

    // 4. Real service discovery: sm: RegisterClient -> GetServiceHandle("lm")
    // move handle -> domain conversion -> OpenLogger.
    uint32_t root1 = 0, child1 = 0;
    uint32_t lm1 = log_session_from_sm(0x510u, &root1, &child1);
    CHECK(root1 == 1u && child1 == 2u);
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions + 1);
    uint32_t root2 = 0, child2 = 0;
    uint32_t lm2 = log_session_from_sm(0x520u, &root2, &child2);
    CHECK(root2 == root1 && child2 == child1); // equal numeric ids, session scoped
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions + 2);

    // 5. Log through each discovered session; equal child ids remain isolated.
    base[0] = 0x31u;
    build_log(child1, 0x521u, (uintptr_t)base, 1);
    CHECK(send_request(lm1) == 0);
    expect_log_reply(0x521u);
    CHECK(dm_lm_packet_count(host) == 1);
    base[0] = 0x42u;
    build_log(child2, 0x522u, (uintptr_t)base, 1);
    CHECK(send_request(lm2) == 0);
    expect_log_reply(0x522u);
    CHECK(dm_lm_packet_count(host) == 2);
    CHECK(dm_lm_take_packet(host, &packet) == 0);
    CHECK(packet.client == lm1 && packet.object_id == child1 && packet.data[0] == 0x31u);
    CHECK(dm_lm_take_packet(host, &packet) == 0);
    CHECK(packet.client == lm2 && packet.object_id == child2 && packet.data[0] == 0x42u);
    CHECK(dm_lm_packet_count(host) == 0);

    // 6. Domain close on the discovered child retires only that child. The
    // service session remains usable for the remaining failure-path checks.
    build_domain_close(child1, 0x526u);
    CHECK(send_request(lm1) == 0);

    // 7. Wrong-session numeric id: allocate an extra child in lm2, then send
    // that id through lm1. Failure closes lm1 and publishes no reply; lm2's
    // child remains independently valid. Equal root/child values above are
    // intentionally ambiguous by number and are resolved by the session handle.
    build_openlogger(root2, 0x525u, 0);
    CHECK(send_request(lm2) == 0);
    uint32_t foreign = get32(tls + 48);
    CHECK(foreign != 0 && foreign != child2);
    build_log(foreign, 0x523u, (uintptr_t)base, 1);
    memcpy(before, tls, sizeof before);
    CHECK(send_request(lm1) != 0);
    CHECK(!memcmp(before, tls, sizeof before));
    CHECK(dm_lm_packet_count(host) == 0);
    CHECK(close_handle(lm1) == 0xe401);
    base[0] = 0x43u;
    build_log(child2, 0x524u, (uintptr_t)base, 1);
    CHECK(send_request(lm2) == 0);
    CHECK(dm_lm_take_packet(host, &packet) == 0 && packet.client == lm2 && packet.data[0] == 0x43u);

    // 8. OpenLogger before conversion/with a wrong root fails explicitly and
    // leaves no logger child. Discover a raw lm handle first.
    uint32_t raw_lm = 0;
    sm = connect_name("sm:");
    build_register(0, 0x530u, 1, 10);
    CHECK(send_request(sm) == 0);
    build_get_service("lm", 0x531u);
    CHECK(send_request(sm) == 0);
    raw_lm = get32(tls + 12);
    CHECK(raw_lm != 0);
    CHECK(close_handle(sm) == 0);
    build_openlogger(1u, 0x532u, 0);
    memcpy(before, tls, sizeof before);
    CHECK(send_request(raw_lm) == 0x4201);
    CHECK(!memcmp(before, tls, sizeof before));
    CHECK(dm_lm_packet_count(host) == 0);
    CHECK(close_handle(raw_lm) == 0xe401);

    // 9. Invalid Log child fails with no partial reply and no retained packet;
    // then the real child is closed and the session is reclaimed cleanly.
    build_log(child2 + 100u, 0x533u, (uintptr_t)base, 1);
    memcpy(before, tls, sizeof before);
    CHECK(send_request(lm2) != 0);
    CHECK(!memcmp(before, tls, sizeof before));
    CHECK(dm_lm_packet_count(host) == 0);
    CHECK(close_handle(lm2) == 0xe401);
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions);
    CHECK(dm_sm_registered_clients(host) == baseline_clients);
    CHECK(dm_lm_packet_count(host) == 0);
}

// Output-pin failure after GetServiceHandle has allocated its real lm session
// but before the move handle is published. The test-only object library renames
// dm_host_memory_pin_locked and dm_host_ipc_create_session; canonical SM/IPC
// code and the real SVC 0x21 path remain in use.
static void sm_getservice_pin_rollback_tests(void) {
    size_t baseline_sessions = dm_host_ipc_open_sessions(host);
    size_t baseline_clients = dm_sm_registered_clients(host);
    uint32_t sm = connect_name("sm:");
    build_register(0, 0x540u, 1, 10);
    CHECK(send_request(sm) == 0);
    expect_empty_reply(0x540u);
    CHECK(dm_sm_registered_clients(host) == baseline_clients + 1);

    build_get_service("lm", 0x541u);
    uint8_t before[DM_IPC_BUFFER_SIZE];
    memcpy(before, tls, sizeof before);
    test_created_handle = 0;
    test_output_pin_calls = 0;
    test_output_pin_injections = 0;
    test_output_pin_host = host;
    test_output_pin_tls = (uintptr_t)tls;
    test_output_pin_fail = 1;

    CHECK(send_request(sm) == DM_SVC_INVALID_ADDRESS);
    CHECK(test_output_pin_calls == 1);
    CHECK(test_output_pin_injections == 1);
    CHECK(test_created_handle != 0);
    CHECK(!memcmp(before, tls, sizeof before));       // no reply bytes published
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions);
    CHECK(dm_sm_registered_clients(host) == baseline_clients);
    CHECK(close_handle(sm) == 0xe401);                // sm closed by the error rule

    // The allocated lm session handle was cleared, not merely omitted from TLS.
    pthread_mutex_lock(&host->mutex);
    unsigned slot = 0, index = 0;
    int live = dm_host_handle_lookup_locked(host, test_created_handle, 4, &slot, &index) == 0;
    pthread_mutex_unlock(&host->mutex);
    CHECK(!live);

    // Capacity/state recovered: a subsequent genuine discovery succeeds.
    uint32_t root = 0, child = 0;
    uint32_t lm = log_session_from_sm(0x542u, &root, &child);
    CHECK(root == 1u && child == 2u);
    CHECK(close_handle(lm) == 0);
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions);
    CHECK(dm_sm_registered_clients(host) == baseline_clients);
}

// Exercise the real SM GetServiceHandle capacity path. The existing generic
// 32-session case uses a convenience endpoint; this fills 31 lm sessions, adds
// the sm session, and makes GetServiceHandle's own create_session return ENOSPC.
static void sm_getservice_capacity_tests(void) {
    size_t baseline_sessions = dm_host_ipc_open_sessions(host);
    size_t baseline_clients = dm_sm_registered_clients(host);
    uint32_t fixtures[31];
    for (unsigned i = 0; i < 31; i++) fixtures[i] = connect_name("lm");
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions + 31);

    uint32_t sm = connect_name("sm:");
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions + 32);
    build_register(0, 0x550u, 1, 10);
    CHECK(send_request(sm) == 0);
    build_get_service("lm", 0x551u);
    uint8_t before[DM_IPC_BUFFER_SIZE];
    memcpy(before, tls, sizeof before);
    CHECK(send_request(sm) == DM_SVC_OUT_OF_RESOURCE);
    CHECK(!memcmp(before, tls, sizeof before));
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions + 31);
    CHECK(dm_sm_registered_clients(host) == baseline_clients);
    CHECK(close_handle(sm) == 0xe401);

    for (unsigned i = 0; i < 31; i++) CHECK(close_handle(fixtures[i]) == 0);
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions);

    // Capacity is genuinely recovered, not merely reported as such.
    uint32_t root = 0, child = 0;
    uint32_t lm = log_session_from_sm(0x552u, &root, &child);
    CHECK(root == 1u && child == 2u);
    CHECK(close_handle(lm) == 0);
    CHECK(dm_host_ipc_open_sessions(host) == baseline_sessions);
    CHECK(dm_sm_registered_clients(host) == baseline_clients);
}

typedef struct {
    dm_host *host;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int entered, release, closed;
} domain_fixture;
static domain_fixture dom = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
static int domain_block_handler(void *user, uint64_t client, unsigned flags, uint32_t command,
                                const uint8_t *request, size_t size,
                                uint8_t *reply, size_t capacity, size_t *reply_size, uint32_t *out_handle) {
    domain_fixture *f = user;
    (void)flags; (void)request;
    if (out_handle) *out_handle = 0;
    if (command != 0 || size != 8 || capacity < 4) { fprintf(stderr, "dom handler bad cmd=%u size=%zu cap=%zu\n", command, size, capacity); return -1; }
    uint32_t id;
    int arc = dm_host_ipc_object_alloc(f->host, client, &id);
    if (arc) { fprintf(stderr, "dom alloc rc=%d client=%llx\n", arc, (unsigned long long)client); return -2; }
    memcpy(reply, &id, 4); *reply_size = 4;
    pthread_mutex_lock(&f->lock);
    f->entered = 1; pthread_cond_broadcast(&f->cond);
    while (!f->release) pthread_cond_wait(&f->cond, &f->lock);
    pthread_mutex_unlock(&f->lock);
    return 0;
}
static void domain_closer(void *user, uint64_t client) {
    domain_fixture *f = user;
    (void)client;
    pthread_mutex_lock(&f->lock); f->closed++; pthread_mutex_unlock(&f->lock);
}
// 真实域派发期间关闭会话：处理器已分配子对象后再提交失败，必须回滚且只清理一次。
// Close during real domain dispatch: handler allocates a child, commit fails, rollback exactly once.
static void *dom_close_thread(void *opaque) {
    uint32_t handle = *(uint32_t *)opaque;
    pthread_mutex_lock(&dom.lock);
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 2;
    while (!dom.entered) if (pthread_cond_timedwait(&dom.cond, &dom.lock, &ts) == ETIMEDOUT) break;
    pthread_mutex_unlock(&dom.lock);
    CHECK(dom.entered);
    CHECK(close_handle(handle) == 0);
    pthread_mutex_lock(&dom.lock); dom.release = 1; pthread_cond_broadcast(&dom.cond); pthread_mutex_unlock(&dom.lock);
    return NULL;
}
static void domain_close_race(void) {
    size_t baseline = dm_host_ipc_open_sessions(host);
    dom.host = host; dom.entered = 0; dom.release = 0; dom.closed = 0;
    CHECK(dm_host_ipc_register_domain_port_full(host, "dmt:dom", domain_block_handler, domain_closer, &dom) == 0);
    uint32_t session = connect_name("dmt:dom");
    build_control(0, 0x70u);
    CHECK(send_request(session) == 0);
    uint32_t root = get32(tls + 32);
    build_openlogger(root, 0x71u, 0x1234u);
    uint8_t before[DM_IPC_BUFFER_SIZE];
    memcpy(before, tls, sizeof before);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, dom_close_thread, &session) == 0);
    uint64_t result = send_request(session);     // 主线程派发并阻塞在处理器。 / main dispatches and blocks in handler.
    void *ret = NULL;
    CHECK(pthread_join(thread, &ret) == 0);
    CHECK(result != 0);                          // 提交失败。 / commit fails.
    CHECK(!memcmp(before, tls, sizeof before));  // 无半回复。 / no partial reply.
    CHECK(dm_host_ipc_open_sessions(host) == baseline);
    CHECK(dom.closed == 1);                      // 关闭回调恰好一次。 / exactly once.
    uint32_t reuse = connect_name("dmt:dom");    // 槽可复用。 / slot reuse.
    CHECK(reuse != 0);
    CHECK(close_handle(reuse) == 0);
}

typedef struct { uint32_t handle; } closer_arg;
static void *closer(void *opaque) {
    closer_arg *arg = opaque;
    pthread_mutex_lock(&state.lock);
    while (!state.entered) pthread_cond_wait(&state.condition, &state.lock);
    pthread_mutex_unlock(&state.lock);
    // 先关闭再放行，保证完成路径观察到 closing。
    // Close before releasing, so the completion path observes closing.
    CHECK(close_handle(arg->handle) == 0);
    pthread_mutex_lock(&state.lock); state.release = 1; pthread_cond_broadcast(&state.condition); pthread_mutex_unlock(&state.lock);
    return NULL;
}

// SDK 真实首包形式：type6 + send-PID 特殊头 + CMIF version1；PID 槽填垃圾以证明运行时从不信任它。
// The SDK's real first packet: type6 + send-PID special header + CMIF version1; the PID slot is filled with junk to prove it is never trusted.
static void build_register(uint32_t command, uint32_t token, int special, uint32_t words) {
    memset(tls, 0x5a, DM_IPC_BUFFER_SIZE);
    put32(tls, 6u);
    put32(tls + 4, words | (special ? 0x80000000u : 0u));
    if (special) {
        put32(tls + 8, 1u); // send_pid
        uint64_t junk_pid = 0xdeadbeefcafebabeull;
        memcpy(tls + 12, &junk_pid, 8);
        memset(tls + 20, 0xa5, 12);
    } else memset(tls + 8, 0xa5, 8);
    put32(tls + 32, 0x49434653u);
    put32(tls + 36, 1u);
    put32(tls + 40, command);
    put32(tls + 44, token);
    put32(tls + 48, 0);
    put32(tls + 52, 0);
}
static void expect_empty_reply(uint32_t token) {
    CHECK(get32(tls) == 0);
    CHECK((get32(tls + 4) & 0x3ffu) == 6);
    CHECK(get32(tls + 16) == 0x4F434653u);
    CHECK(get32(tls + 28) == token);
}
// GetServiceHandle：type6 无特殊头，命令 1，8 字节打包服务名。
// GetServiceHandle: type6 without special header, command 1, 8-byte packed service name.
static void build_get_service(const char *name, uint32_t token) {
    memset(tls, 0x5a, DM_IPC_BUFFER_SIZE);
    put32(tls, 6u);
    put32(tls + 4, 10u);
    put32(tls + 16, 0x49434653u);
    put32(tls + 20, 1u);
    put32(tls + 24, 1u);
    put32(tls + 28, token);
    memset(tls + 32, 0, 8);
    memcpy(tls + 32, name, strlen(name) < 8 ? strlen(name) : 8);
}

typedef struct { dm_host *host; uint32_t handle; size_t registered; int ready, destroy, done, rc; uint32_t ctrl_id, ctrl_dup; uint64_t ctrl_query; } other_host;
static pthread_mutex_t other_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t other_cond = PTHREAD_COND_INITIALIZER;
// 第二个宿主线程：独立创建、附着、连接 sm: 并登记同一代际句柄值。
// Second host thread: creates, attaches, connects sm: and registers the same generation handle value independently.
static void *other_host_main(void *opaque) {
    other_host *o = opaque;
    o->rc = -1;
    o->host = dm_host_create_accounted_process(64 * UNIT, 16 * UNIT, 32 * UNIT, 128 * UNIT);
    if (o->host) {
        dm_gate_table gates = {0};
        if (dm_guest_attach(o->host, &gates) == 0) {
            dm_guest_context *c = dm_guest_current();
            uint8_t *t = (uint8_t *)c->slots[DM_SLOT_TLS];
            memset(t, 0, 0x200); strcpy((char *)t + 0x100, "sm:");
            uint64_t registers[31] = {0};
            registers[0] = 0x11111111u; registers[1] = (uintptr_t)(t + 0x100);
            if (dm_host_svc(o->host, 0x1f, registers) == 0) {
                o->handle = (uint32_t)registers[1];
                memset(t, 0x5a, DM_IPC_BUFFER_SIZE);
                put32(t, 6u); put32(t + 4, 10u | 0x80000000u);
                put32(t + 8, 1u);
                uint64_t junk = 0xdeadbeefcafebabeull; memcpy(t + 12, &junk, 8);
                put32(t + 32, 0x49434653u); put32(t + 36, 1u); put32(t + 40, 0u); put32(t + 44, 0x90u);
                put32(t + 48, 0u); put32(t + 52, 0u);
                registers[0] = o->handle;
                if (dm_host_svc(o->host, 0x21, registers) == 0) {
                    o->registered = dm_sm_registered_clients(o->host);
                    // 对端宿主的控制消息状态必须独立：查询/转换/重复转换。
                    // The peer host's control state must be independent: query/convert/duplicate.
                    memset(t, 0x5a, DM_IPC_BUFFER_SIZE);
                    put32(t, 7u); put32(t + 4, 8u);
                    memset(t + 8, 0xa5, 8);
                    put32(t + 16, 0x49434653u); put32(t + 20, 1u); put32(t + 24, 3u); put32(t + 28, 0x777u);
                    registers[0] = o->handle;
                    o->ctrl_query = dm_host_svc(o->host, 0x21, registers);
                    memset(t, 0x5a, DM_IPC_BUFFER_SIZE);
                    put32(t, 7u); put32(t + 4, 8u);
                    memset(t + 8, 0xa5, 8);
                    put32(t + 16, 0x49434653u); put32(t + 20, 1u); put32(t + 24, 0u); put32(t + 28, 0x778u);
                    registers[0] = o->handle;
                    if (dm_host_svc(o->host, 0x21, registers) == 0) {
                        memcpy(&o->ctrl_id, t + 32, 4);
                        memset(t, 0x5a, DM_IPC_BUFFER_SIZE);
                        put32(t, 7u); put32(t + 4, 8u);
                        memset(t + 8, 0xa5, 8);
                        put32(t + 16, 0x49434653u); put32(t + 20, 1u); put32(t + 24, 0u); put32(t + 28, 0x779u);
                        registers[0] = o->handle;
                        if (dm_host_svc(o->host, 0x21, registers) == 0) memcpy(&o->ctrl_dup, t + 32, 4);
                    }
                    o->rc = 0;
                }
            }
        }
    }
    pthread_mutex_lock(&other_lock);
    o->ready = 1; pthread_cond_broadcast(&other_cond);
    while (!o->destroy) pthread_cond_wait(&other_cond, &other_lock);
    pthread_mutex_unlock(&other_lock);
    if (o->host) {
        if (o->handle) { uint64_t registers[31] = {0}; registers[0] = o->handle; dm_host_svc(o->host, 0x16, registers); }
        dm_guest_detach();
        dm_host_destroy(o->host);
    }
    pthread_mutex_lock(&other_lock);
    o->done = 1; pthread_cond_broadcast(&other_cond);
    pthread_mutex_unlock(&other_lock);
    return NULL;
}

int main(void) {
    state.host = NULL;
    pthread_mutex_init(&state.lock, NULL);
    pthread_cond_init(&state.condition, NULL);
    host = dm_host_create_accounted_process(64 * UNIT, 16 * UNIT, 32 * UNIT, 128 * UNIT);
    CHECK(host);
    state.host = host;
    dm_gate_table gates = {0};
    CHECK(dm_guest_attach(host, &gates) == 0);
    context = dm_guest_current();
    CHECK(context && context->host == host);
    tls = (uint8_t *)context->slots[DM_SLOT_TLS];
    CHECK(tls && (uintptr_t)tls % 16 == 0);
    CHECK(dm_host_ipc_register_port(host, "dm:fixture", endpoint, &state) == 0);
    CHECK(dm_host_ipc_register_port(host, "dmt:svc", endpoint, &state) == 0);
    CHECK(dm_host_ipc_register_port(host, "dm:fixture", endpoint, &state) == EEXIST);

    // A0. 双宿主隔离：相同代际句柄在两个宿主中独立登记；销毁/关闭互不影响。
    // A0. Two-host isolation: identical generation handles register independently; destroy/close do not affect the other host.
    {
        uint32_t a = connect_name("sm:");
        build_register(0, 0x40u, 1, 10);
        CHECK(send_request(a) == 0);
        CHECK(dm_sm_registered_clients(host) == 1);
        other_host o = {0};
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, other_host_main, &o) == 0);
        pthread_mutex_lock(&other_lock);
        while (!o.ready) pthread_cond_wait(&other_cond, &other_lock);
        pthread_mutex_unlock(&other_lock);
        CHECK(o.rc == 0);
        CHECK(o.registered == 1);
        CHECK(o.ctrl_query == 0);
        CHECK(o.ctrl_id != 0 && o.ctrl_dup == o.ctrl_id);
        CHECK(o.handle == a); // 两个宿主都从同一代际槽分配。 / both hosts allocate from the same generation slot.
        CHECK(dm_sm_registered_clients(host) == 1);
        pthread_mutex_lock(&other_lock);
        o.destroy = 1; pthread_cond_broadcast(&other_cond);
        while (!o.done) pthread_cond_wait(&other_cond, &other_lock);
        pthread_mutex_unlock(&other_lock);
        CHECK(pthread_join(thread, NULL) == 0);
        CHECK(dm_sm_registered_clients(host) == 1); // 对端销毁不影响本宿主。 / peer destruction leaves this host intact.
        // 对端销毁后本宿主仍可独立转换/重复转换。
        // After peer destruction this host still converts and de-duplicates independently.
        {
            uint32_t cs = connect_name("sm:");
            build_control(0, 0x4242u);
            CHECK(send_request(cs) == 0);
            uint32_t local = get32(tls + 32);
            CHECK(local != 0);
            build_control(0, 0x4243u);
            CHECK(send_request(cs) == 0);
            CHECK(get32(tls + 32) == local);
            CHECK(close_handle(cs) == 0);
        }
        // 本宿主重复登记仍被拒绝，并只注销本宿主状态。
        // A duplicate in this host is still rejected and retires only this host's state.
        build_register(0, 0x41u, 1, 10);
        CHECK(send_request(a) == 0x4201);
        CHECK(dm_sm_registered_clients(host) == 0);
    }
    // A. 原始 SVC ABI：x0 不被内核消费（这里故意给非法指针），w1 返回句柄。
    // A. Raw SVC ABI: x0 is not consumed (deliberately invalid here); w1 returns the handle.
    uint32_t session = connect_name("dm:fixture");
    CHECK(session != 0 && dm_host_ipc_open_sessions(host) == 1);
    {
        uint64_t registers[31] = {0};
        registers[0] = 0x11111111u; registers[1] = 0;
        CHECK(dm_host_svc(host, 0x1f, registers) == 0xcc01);
        CHECK(registers[1] == 0);
    }
    // 未登记端口仍显式失败；sm: 由运行时按已证明的 RegisterClient 语义提供。
    // Unregistered ports still fail explicitly; sm: is provided by the runtime with proven RegisterClient semantics.
    memset(tls, 0, 0x200); strcpy((char *)tls + 0x100, "no:such");
    {
        uint32_t handle = 0xfeedfaceu;
        CHECK(connect_raw(0x11111111u, (uintptr_t)(tls + 0x100), &handle) == 0xe401);
        CHECK(handle == 0);
    }
    // A2. 真实首包：sm: RegisterClient（type6 + send-PID + CMIF v1），真实登记与会话清理。
    // A2. Real first packet: sm: RegisterClient (type6 + send-PID + CMIF v1), with real registration and session cleanup.
    {
        uint32_t sm = connect_name("sm:");
        build_register(0, 0x77u, 1, 10);
        CHECK(send_request(sm) == 0);
        expect_empty_reply(0x77u);
        CHECK(dm_sm_registered_clients(host) == 1);
        // 同一会话重复注册明确失败并关闭会话，回调注销真实状态。
        // A duplicate registration on the same session fails explicitly and closes it; the callback retires the real state.
        build_register(0, 0x78u, 1, 10);
        CHECK(send_request(sm) == 0x4201);
        CHECK(dm_sm_registered_clients(host) == 0);
        CHECK(send_request(sm) == 0xe401);
        // 缺 send-PID 特殊头的 type6 包明确拒绝。
        // A type6 packet without the send-PID special header is explicitly rejected.
        sm = connect_name("sm:");
        build_register(0, 0x79u, 0, 8);
        CHECK(send_request(sm) == 0x4201);
        CHECK(dm_sm_registered_clients(host) == 0);
        // GetServiceHandle（命令 1）尚未实现：显式 unsupported，不伪造句柄。
        // GetServiceHandle (command 1) is not implemented: explicit unsupported, no fabricated handle.
        sm = connect_name("sm:");
        build_register(1, 0x7au, 1, 10);
        CHECK(send_request(sm) == 0x4201);
        CHECK(dm_sm_registered_clients(host) == 0);
        // 特殊头存在但 data_words 不足以容纳对齐后的 CMIF 头。
        // Special header present but data_words cannot cover the aligned CMIF header.
        sm = connect_name("sm:");
        build_register(0, 0x7bu, 1, 6);
        CHECK(send_request(sm) == 0x4201);
    }
    // A3. GetServiceHandle：真实注册端口返回 move handle；未知服务显式 unsupported 且不泄漏会话。
    // A3. GetServiceHandle: a registered port returns a move handle; unknown services are explicit unsupported with no session leak.
    {
        uint32_t sm = connect_name("sm:");
        build_register(0, 0x80u, 1, 10);
        CHECK(send_request(sm) == 0);
        build_get_service("dmt:svc", 0x81u);
        CHECK(send_request(sm) == 0);
        CHECK(get32(tls) == 0);
        CHECK((get32(tls + 4) & 0x3ffu) == 4);          // cmif out header only
        CHECK((get32(tls + 4) >> 31) == 1);             // has special header
        CHECK(get32(tls + 8) == (1u << 5));             // one move handle
        uint32_t svc = get32(tls + 12);
        CHECK(svc != 0);
        CHECK(get32(tls + 16) == 0x4F434653u);
        CHECK(get32(tls + 24) == 0);
        CHECK(get32(tls + 28) == 0x81u);
        uint64_t input = 0x8877665544332211ull, expected = input + 1;
        build(1, &input, 8, 0x82u);
        CHECK(send_request(svc) == 0);
        expect_reply(0x82u, &expected, 8);
        CHECK(close_handle(svc) == 0);
        build_get_service("fsp:", 0x83u);
        CHECK(send_request(sm) == 0x4201);
        CHECK(close_handle(sm) == 0xe401);
        CHECK(dm_host_ipc_open_sessions(host) == 1); // 仅剩 A 段的 dm:fixture 会话。 / only the section-A dm:fixture session remains.
    }
    // A4. 未 RegisterClient 的全新 sm 会话不能获取服务；关闭会话后旧登记不得复用。
    // A4. A fresh sm session that never called RegisterClient cannot acquire a service; a closed session's registration cannot be reused.
    {
        uint32_t fresh = connect_name("sm:");
        build_get_service("dmt:svc", 0x84u);
        uint8_t before[DM_IPC_BUFFER_SIZE];
        memcpy(before, tls, sizeof before);
        size_t sessions = dm_host_ipc_open_sessions(host);
        CHECK(send_request(fresh) == 0x4201);
        CHECK(!memcmp(before, tls, sizeof before));
        CHECK(dm_host_ipc_open_sessions(host) == sessions - 1); // sm 会话按错误规则关闭，未创建服务会话。 / sm closes per error rule and no service session is created.
        // 先登记再关闭；新会话不得继承旧登记。
        // Register then close; a new session must not inherit the old registration.
        uint32_t reg = connect_name("sm:");
        build_register(0, 0x85u, 1, 10);
        CHECK(send_request(reg) == 0);
        CHECK(close_handle(reg) == 0);
        uint32_t again = connect_name("sm:");
        build_get_service("dmt:svc", 0x86u);
        CHECK(send_request(again) == 0x4201);
        CHECK(dm_host_ipc_open_sessions(host) == sessions - 1);
    }
    control_tests();
    domain_tests();
    domain_close_race();

    // B. 成功请求/响应与确定性填充。
    // B. Successful request/response with deterministic padding.
    {
        uint64_t input = 0x1122334455667788ull, expected = input + 1;
        int before = state.calls;
        build(1, &input, 8, 0xabcdu);
        CHECK(send_request(session) == 0);
        expect_reply(0xabcdu, &expected, 8);
        CHECK(state.calls == before + 1);
        build(5, NULL, 0, 0x55u);
        CHECK(send_request(session) == 0);
        expect_reply(0x55u, NULL, 0);
    }
    // C. 名称与错误指针。
    // C. Names and invalid pointers.
    {
        const char *stack_name = "dm:fixture";
        CHECK(connect_raw(0x11111111u, (uintptr_t)stack_name, NULL) == 0xcc01);
        memset(tls + 0x100, 'a', 32); tls[0x100 + 32] = 0;
        CHECK(connect_raw(0x11111111u, (uintptr_t)(tls + 0x100), NULL) == 0xca01);
        memset(tls, 0, 0x200); strcpy((char *)tls + 0x100, "dm:bad");
        tls[0x100 + 4] = 1;
        CHECK(connect_raw(0x11111111u, (uintptr_t)(tls + 0x100), NULL) == 0xca01);
    }
    // D. 畸形包：明确 unsupported，TLS 不改变，会话自动关闭。
    // D. Malformed packets: explicit unsupported, TLS unchanged, session auto-closed.
    {
        CHECK(close_handle(session) == 0);
        uint64_t input = 0x0102030405060708ull;
        for (int variant = 0; variant < 9; variant++) {
            uint32_t handle = connect_name("dm:fixture");
            build(1, &input, 8, 0x1234u);
            if (variant == 0) tls[0] = 5;                    // 错误 type
            if (variant == 1) tls[3] |= 0x10;                // send_buffers 描述符
            if (variant == 2) tls[7] |= 0x80;                // special header
            if (variant == 3) tls[5] |= 0x04;                // recv_static_mode
            if (variant == 4) put32(tls + 4, 5);             // data_words 太小
            if (variant == 5) put32(tls + 4, 63);            // data_words 太大
            if (variant == 6) tls[16] ^= 0xff;               // CMIF magic
            if (variant == 7) put32(tls + 20, 1);            // CMIF version
            if (variant == 8) {                              // 错误地省掉 8 字节对齐：CMIF 落在 +8
                memmove(tls + 8, tls + 16, 16);
                put32(tls + 16, 0xa5a5a5a5u);
            }
            uint8_t before[DM_IPC_BUFFER_SIZE];
            memcpy(before, tls, sizeof before);
            CHECK(send_request(handle) == 0x4201);
            CHECK(!memcmp(before, tls, sizeof before));
            CHECK(send_request(handle) == 0xe401);
            CHECK(close_handle(handle) == 0xe401);
    CHECK(dm_host_ipc_open_sessions(host) == 0);
        }
        // 处理器显式失败同样不发行任何回复字节。
        // An explicit handler failure likewise publishes no reply byte.
        uint32_t handle = connect_name("dm:fixture");
        build(2, &input, 8, 0x7777u);
        uint8_t before[DM_IPC_BUFFER_SIZE];
        memcpy(before, tls, sizeof before);
        CHECK(send_request(handle) == 0x4201);
        CHECK(!memcmp(before, tls, sizeof before));
        CHECK(close_handle(handle) == 0xe401);
    }
    // E. 句柄种类隔离与代际。
    // E. Handle-kind isolation and generations.
    {
        uint32_t handle = connect_name("dm:fixture");
        CHECK(session_control(0x12, handle) == 0xe401);  // ClearEvent
        CHECK(session_control(0x17, handle) == 0xe401);  // ResetSignal
        CHECK(session_control(0x0c, handle) == 0xe401);  // GetThreadPriority
        CHECK(session_control(0x25, handle) == 0xe401);  // GetThreadId
        {
            put32(tls + 0x300, handle);
            uint64_t registers[31] = {0};
            registers[0] = 0; registers[1] = (uintptr_t)(tls + 0x300); registers[2] = 1; registers[3] = 0;
            CHECK(dm_host_svc(host, 0x18, registers) == 0xe401);
        }
        uint64_t input = 7, expected = 8;
        build(1, &input, 8, 0x99u);
        CHECK(send_request(handle) == 0);
        expect_reply(0x99u, &expected, 8);
        CHECK(close_handle(handle) == 0);
        CHECK(close_handle(handle) == 0xe401);
        CHECK(send_request(handle) == 0xe401);
        // 事件句柄仍走既有命名空间；会话操作不能影响它。
        // Event handles still use the existing namespace; session operations cannot disturb them.
        uint64_t registers[31] = {0};
        CHECK(dm_host_svc(host, 0x45, registers) == 0);
        uint32_t write = (uint32_t)registers[1], read = (uint32_t)registers[2];
        CHECK(send_request(write) == 0xe401);
        CHECK(close_handle(write) == 0);
        CHECK(close_handle(read) == 0);
    }
    // F. 会话耗尽与回滚。
    // F. Session exhaustion and rollback.
    {
        uint32_t handles[32];
        for (unsigned i = 0; i < 32; i++) handles[i] = connect_name("dm:fixture");
        CHECK(dm_host_ipc_open_sessions(host) == 32);
        memset(tls, 0, 0x200); strcpy((char *)tls + 0x100, "dm:fixture");
        {
            uint32_t rejected = 0xfeedfaceu;
            CHECK(connect_raw(0x11111111u, (uintptr_t)(tls + 0x100), &rejected) == 0xce01);
            CHECK(rejected == 0);
        }
        CHECK(dm_host_ipc_unregister_port(host, "dm:fixture") == EBUSY);
        CHECK(close_handle(handles[0]) == 0);
        CHECK(dm_host_ipc_open_sessions(host) == 31);
        uint32_t replacement = connect_name("dm:fixture");
        CHECK(replacement != 0 && dm_host_ipc_open_sessions(host) == 32);
        for (unsigned i = 1; i < 32; i++) CHECK(close_handle(handles[i]) == 0);
        CHECK(close_handle(replacement) == 0);
    CHECK(dm_host_ipc_open_sessions(host) == 0);
    }
    // G. 同一会话的重入请求显式 BAD_STATE。
    // G. A re-entrant request on the same session is explicitly BAD_STATE.
    {
        uint32_t handle = connect_name("dm:fixture");
        state.handle = handle;
        uint32_t value = 0; build(4, &value, 4, 0x31u);
        CHECK(send_request(handle) == 0);
        CHECK(state.reentrant == 0xfa01);
        uint32_t reply = 0x24681357u;
        expect_reply(0x31u, &reply, 4);
        CHECK(close_handle(handle) == 0);
    }
    // H. 派发期间关闭：完成路径不发布回复并回收会话，旧句柄失效。
    // H. Close during dispatch: completion publishes nothing, reclaims the session and invalidates the handle.
    {
        uint32_t handle = connect_name("dm:fixture");
        state.entered = 0; state.release = 0;
        closer_arg argument = {handle};
        pthread_t thread;
        CHECK(pthread_create(&thread, NULL, closer, &argument) == 0);
        build(3, NULL, 0, 0x42u);
        uint8_t before[DM_IPC_BUFFER_SIZE];
        memcpy(before, tls, sizeof before);
        CHECK(send_request(handle) == 0xfa01);
        CHECK(!memcmp(before, tls, sizeof before));
        CHECK(pthread_join(thread, NULL) == 0);
    CHECK(dm_host_ipc_open_sessions(host) == 0);
        CHECK(send_request(handle) == 0xe401);
    }
    // I. 宿主销毁与注销生命周期。
    // I. Host destroy and unregistration lifecycle.
    {
        uint32_t handle = connect_name("dm:fixture");
        CHECK(dm_host_destroy(host) == EBUSY);
        CHECK(close_handle(handle) == 0);
        CHECK(dm_host_destroy(host) == EBUSY); // 仍附着当前线程。 / the current thread is still attached.
        CHECK(dm_host_ipc_unregister_port(host, "dm:fixture") == 0);
        CHECK(dm_guest_detach() == 0);
        CHECK(dm_host_destroy(host) == 0);
    }
    // J. 独立的宿主：真实 SVC Log、pin/复制、队列与关闭清理。
    // J. Independent host: real SVC Log, pin/copy, queue and close cleanup.
    // ASAN 在 x86_64 保留 shadow gap；使用真实匿名映射而不是固定 VA。
    // ASAN reserves an x86_64 shadow gap; use real anonymous mappings, not fixed VAs.
    {
        host = dm_host_create_process_layout(64 * UNIT, 16 * UNIT, 32 * UNIT);
        CHECK(host);
        uint8_t *log_base = wire_page(0);
        CHECK(log_base != NULL);
        CHECK(dm_host_register_writable(host, log_base, 4096) == 0);
        uint8_t *readonly_base = wire_page(1);
        CHECK(readonly_base != NULL);
        memset(readonly_base, 0x99, 4096);
        CHECK(mprotect(readonly_base, 4096, PROT_READ) == 0);
        CHECK(dm_host_register_executable(host, readonly_base, 4096) == 0);
        dm_gate_table gates = {0};
        CHECK(dm_guest_attach(host, &gates) == 0);
        context = dm_guest_current();
        CHECK(context && context->host == host);
        tls = (uint8_t *)context->slots[DM_SLOT_TLS];
        CHECK(tls && (uintptr_t)tls % 16 == 0);
        log_tests(log_base, readonly_base);
        sm_lm_discovery_tests(log_base);
        sm_getservice_pin_rollback_tests();
        sm_getservice_capacity_tests();
        consumer_tests(log_base);
        CHECK(dm_lm_packet_count(host) == 0);
        dm_host *primary = host;
        CHECK(dm_guest_detach() == 0);
        // 第二宿主独立队列：相同数值 id/不同 session 不污染 primary。
        // Second host has an independent queue; same numeric ids do not leak into primary.
        host = dm_host_create_process_layout(64 * UNIT, 16 * UNIT, 32 * UNIT);
        CHECK(host);
        uint8_t *other_base = wire_page(2);
        CHECK(other_base != NULL);
        CHECK(dm_host_register_writable(host, other_base, 4096) == 0);
        CHECK(dm_guest_attach(host, &gates) == 0);
        tls = (uint8_t *)dm_guest_current()->slots[DM_SLOT_TLS];
        uint32_t other_root = 0, other_child = 0;
        uint32_t other = log_session(0x301u, &other_root, &other_child);
        other_base[0] = 0x77;
        build_log(other_child, 0x301u, (uintptr_t)other_base, 1);
        CHECK(send_request(other) == 0);
        CHECK(dm_lm_packet_count(host) == 1);
        sink_reset();
        CHECK(dm_lm_set_sink(host, consumer_sink, host) == 0);
        CHECK(dm_lm_consume(host, 1) == 1 && sink_seen_n == 1 && sink_seen[0] == 0x77);
        CHECK(dm_lm_packet_count(host) == 0);
        CHECK(close_handle(other) == 0);
        CHECK(dm_lm_packet_count(host) == 0);
        sm_lm_discovery_tests(other_base);
        CHECK(dm_lm_packet_count(host) == 0);
        // 并发：阻塞回调期间关闭会话并尝试 close/destroy，必须确定性 EBUSY；回调结束后再销毁。
        // Concurrency: close the session while a callback blocks; close/destroy must return EBUSY until consume ends.
        uint32_t cr = 0, cc = 0;
        uint32_t ch = log_session(0x302u, &cr, &cc);
        other_base[0] = 0x81; build_log(cc, 0x430u, (uintptr_t)other_base, 1); CHECK(send_request(ch) == 0);
        other_base[0] = 0x82; build_log(cc, 0x431u, (uintptr_t)other_base, 1); CHECK(send_request(ch) == 0);
        CHECK(dm_lm_packet_count(host) == 2);
        pthread_mutex_lock(&block_mutex); block_entered = block_release = 0; pthread_mutex_unlock(&block_mutex);
        CHECK(dm_lm_set_sink(host, blocking_sink, host) == 0);
        pthread_t consumer;
        CHECK(pthread_create(&consumer, NULL, consume_thread, host) == 0);
        pthread_mutex_lock(&block_mutex);
        while (!block_entered) pthread_cond_wait(&block_cond, &block_mutex);
        pthread_mutex_unlock(&block_mutex);
        CHECK(dm_lm_close(host) == EBUSY);              // 活动 consume 阻止直接 close
        CHECK(close_handle(ch) == 0);                   // 会话清理可进行，回调仍持有副本
        CHECK(dm_guest_detach() == 0);
        CHECK(dm_host_destroy(host) == EBUSY);          // 唯一 busy 来源是活动 consume
        pthread_mutex_lock(&block_mutex); block_release = 1; pthread_cond_broadcast(&block_cond); pthread_mutex_unlock(&block_mutex);
        CHECK(pthread_join(consumer, NULL) == 0);
        CHECK(consume_result == 1);
        CHECK(block_take_rc == EBUSY && block_set_rc == EBUSY);
        CHECK(dm_host_destroy(host) == 0);
        host = primary;
        CHECK(dm_lm_packet_count(host) == 0);
        CHECK(dm_host_destroy(host) == 0);
        CHECK(munmap(log_base, 4096) == 0);
        CHECK(munmap(readonly_base, 4096) == 0);
        CHECK(munmap(other_base, 4096) == 0);
    }
    puts("ipc transport fixture: all checks passed");
    return 0;
}

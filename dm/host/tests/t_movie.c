// 过场空播的 WebM 时长解析单测（秒级）：手工按 EBML 拼几段容器头，期望值全部手算，不调被测实现。
// 编译运行：make -C dm/host/tests run
// Unit test for WebM duration parsing of cutscene no-op playback (takes seconds): a few container headers are assembled by hand per EBML, and all expected values are computed by hand without calling the implementation under test.
// Build and run: make -C dm/host/tests run
#include "../hle.h"
#include <stdio.h>

void *guest_sym(const char *n) { (void)n; return NULL; }      // 被测文件里的客体调用桩（解析路径不用） | stub for the guest call in the file under test (not used by the parsing path)
struct dm_flags g_flag; uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }                          // 日志时间戳桩（解析路径不用） | log timestamp stub (not used by the parsing path)
typedef int64_t (*mv_read_fn)(void *ctx, int64_t off, void *buf, size_t n);
int64_t mv_webm_duration_ns(mv_read_fn rd, void *ctx);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ✓ "); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { const uint8_t *p; size_t n; } mem;
static int64_t rd_mem(void *c_, int64_t off, void *buf, size_t n) {
    mem *c = c_; if (off < 0 || (size_t)off >= c->n) return 0;
    size_t k = c->n - off < n ? c->n - off : n; memcpy(buf, c->p + off, k); return (int64_t)k;
}
static int64_t dur(const uint8_t *p, size_t n) { mem m = {p, n}; return mv_webm_duration_ns(rd_mem, &m); }

int main(void) {
    // ① 正常：EBML 头(空体) + Segment(已知长度) { SeekHead(空) , Info { TimecodeScale=1000000, Duration=float64 6633.0 } }
    //   6633.0 的 IEEE754 双精度 = 0x40B9E90000000000；期望 6633.0 × 1000000 ns = 6,633,000,000 ns（= ffprobe 测 logoloop 的 6.633 s）
    // (1) Normal: EBML header (empty body) + Segment (known length) { SeekHead (empty) , Info { TimecodeScale=1000000, Duration=float64 6633.0 } }
    //   IEEE754 double of 6633.0 = 0x40B9E90000000000; expected 6633.0 * 1000000 ns = 6,633,000,000 ns (= the 6.633 s ffprobe measures for logoloop)
    static const uint8_t a[] = {
        0x1A,0x45,0xDF,0xA3, 0x80,                                   // EBML，长 0 | EBML, length 0
        0x18,0x53,0x80,0x67, 0x9C,                                   // Segment，长 28 = 5 + 5 + 18 | Segment, length 28 = 5 + 5 + 18
          0x11,0x4D,0x9B,0x74, 0x80,                                 //   SeekHead，长 0 | SeekHead, length 0
          0x15,0x49,0xA9,0x66, 0x92,                                 //   Info，长 18 = 7 + 11 | Info, length 18 = 7 + 11
            0x2A,0xD7,0xB1, 0x83, 0x0F,0x42,0x40,                    //     TimecodeScale = 0x0F4240 = 1000000
            0x44,0x89, 0x88, 0x40,0xB9,0xE9,0x00,0x00,0x00,0x00,0x00 //     Duration float64 6633.0
    };
    CHECK(dur(a, sizeof a) == 6633000000ll, "float64 时长 6633 × 1ms = 6.633 s（读到 %lld）", (long long)dur(a, sizeof a));
    // ② float32 + 非缺省 TimecodeScale：Duration=float32 2000.0（0x44FA0000），Scale=500000 ⇒ 1,000,000,000 ns；Segment 未知长度（0x01FFFFFFFFFFFFFF）
    // (2) float32 + non-default TimecodeScale: Duration=float32 2000.0 (0x44FA0000), Scale=500000 => 1,000,000,000 ns; Segment of unknown length (0x01FFFFFFFFFFFFFF)
    static const uint8_t b[] = {
        0x1A,0x45,0xDF,0xA3, 0x80,
        0x18,0x53,0x80,0x67, 0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
          0x15,0x49,0xA9,0x66, 0x8E,                                 //   Info，长 14 = 7 + 7 | Info, length 14 = 7 + 7
            0x2A,0xD7,0xB1, 0x83, 0x07,0xA1,0x20,                    //     0x07A120 = 500000
            0x44,0x89, 0x84, 0x44,0xFA,0x00,0x00                     //     float32 2000.0
    };
    CHECK(dur(b, sizeof b) == 1000000000ll, "float32 2000 × 0.5ms + 未知长度 Segment = 1.000 s（读到 %lld）", (long long)dur(b, sizeof b));
    // ③ 缺省 TimecodeScale（Info 里没有）：Duration=float64 10033.0（0x40C3988000000000）⇒ 10,033,000,000 ns
    // (3) Default TimecodeScale (absent from Info): Duration=float64 10033.0 (0x40C3988000000000) => 10,033,000,000 ns
    static const uint8_t c[] = {
        0x1A,0x45,0xDF,0xA3, 0x80,
        0x18,0x53,0x80,0x67, 0x90,
          0x15,0x49,0xA9,0x66, 0x8B,
            0x44,0x89, 0x88, 0x40,0xC3,0x98,0x80,0x00,0x00,0x00,0x00
    };
    CHECK(dur(c, sizeof c) == 10033000000ll, "缺省 TimecodeScale = 1ms，10033 ⇒ 10.033 s（读到 %lld）", (long long)dur(c, sizeof c));
    // ④ 先遇到 Cluster、没有 Info ⇒ -1（调用方据此报红，不给缺省值）
    // (4) Cluster encountered first, no Info => -1 (the caller reports a failure on this and gives no default)
    static const uint8_t d[] = { 0x1A,0x45,0xDF,0xA3, 0x80, 0x18,0x53,0x80,0x67, 0x85, 0x1F,0x43,0xB6,0x75, 0x80 };
    CHECK(dur(d, sizeof d) == -1, "无 Info 先到 Cluster ⇒ -1（读到 %lld）", (long long)dur(d, sizeof d));
    // ⑤ Info 里没有 Duration ⇒ -1
    // (5) No Duration in Info => -1
    static const uint8_t e[] = { 0x1A,0x45,0xDF,0xA3, 0x80, 0x18,0x53,0x80,0x67, 0x8C, 0x15,0x49,0xA9,0x66, 0x87, 0x2A,0xD7,0xB1, 0x83, 0x0F,0x42,0x40 };
    CHECK(dur(e, sizeof e) == -1, "无 Duration ⇒ -1（读到 %lld）", (long long)dur(e, sizeof e));
    // ⑥ 截断（Segment 头之后没数据）⇒ -1
    // (6) Truncated (no data after the Segment header) => -1
    CHECK(dur(a, 10) == -1, "截断 ⇒ -1");
    printf(fails ? "t_movie 失败 %d/%d\n" : "t_movie 全过：%d/%d\n", fails ? fails : checks, checks);
    return fails != 0;
}

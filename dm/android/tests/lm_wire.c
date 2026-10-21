// Owned tests for the real bounded lm Log framing decoder.
// Literal wire vectors drive dm_lm_wire_decode_log_request; no decoder copy.
#include "../runtime/lm_wire.h"
#include <stdio.h>
#include <string.h>

#define REQ_END 76u
#define ADDR_LIMIT (1ull << 39)

// ptr=0x123456780, len=0x400, child=0x12345678, token=0x11223344.
static const uint8_t k_request[REQ_END] = {
    0x06,0x00,0x11,0x00, 0x0c,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x04,0x00,0x00, 0x80,0x67,0x45,0x23,
    0x00,0x00,0x00,0x10, 0xcc,0xcc,0xcc,0xcc,
    0x01,0x00,0x10,0x00, 0x78,0x56,0x34,0x12,
    0x00,0x00,0x00,0x00, 0x44,0x33,0x22,0x11,
    0x53,0x46,0x43,0x49, 0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0xcc,0xcc,0xcc,0xcc, 0xcc,0xcc,0xcc,0xcc,
    0xcc,0xcc,0xcc,0xcc
};

static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static void put32(uint8_t *p, uint32_t v) { memcpy(p,&v,4); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p,&v,2); }

// Test-side encoder for boundary vectors; it never decodes.
static void set_buffer(uint8_t *b, uint64_t ptr, uint32_t len) {
    uint32_t packed = ((uint32_t)((ptr >> 32) & 0xfu) << 28) |
                      ((uint32_t)((ptr >> 34) & 0x1cu)) |
                      (uint32_t)((len >> 8) & 0x0f000000u);
    put32(b + 16, len);
    put32(b + 20, (uint32_t)ptr);
    put32(b + 24, packed);
}

static int fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fail = 1; } } while (0)

static void expect(const uint8_t *b, size_t n, dm_lm_wire_status want, const char *label) {
    dm_lm_wire_log_request out, before;
    memset(&out, 0xa5, sizeof out);
    memcpy(&before, &out, sizeof out);
    dm_lm_wire_status got = dm_lm_wire_decode_log_request(b, n, &out);
    if (got != want) { printf("FAIL %s: got %d want %d\n", label, (int)got, (int)want); fail = 1; }
    if (memcmp(&out, &before, sizeof out) != 0) { printf("FAIL %s: output mutated\n", label); fail = 1; }
}

int main(void) {
    dm_lm_wire_log_request r;
    memset(&r, 0xa5, sizeof r);
    CHECK(dm_lm_wire_decode_log_request(k_request, sizeof k_request, &r) == DM_LM_WIRE_OK);
    CHECK(r.pointer == 0x123456780ull);
    CHECK(r.length == 0x400u);
    CHECK(r.object_id == 0x12345678u);
    CHECK(r.token == 0x11223344u);
    CHECK(REQ_END == 76u);
    CHECK(get32(k_request + 24) == 0x10000000u);

    // Positive len1 (the decoder subset starts at one byte).
    uint8_t one[REQ_END];
    memcpy(one, k_request, REQ_END);
    set_buffer(one, 0x123456780ull, 1);
    CHECK(dm_lm_wire_decode_log_request(one, REQ_END, &r) == DM_LM_WIRE_OK);
    CHECK(r.pointer == 0x123456780ull && r.length == 1u);

    // Proved padding is ignored.
    uint8_t m[REQ_END];
    memcpy(m, k_request, REQ_END); m[28] = 0xa5; m[75] = 0x5a;
    CHECK(dm_lm_wire_decode_log_request(m, REQ_END, &r) == DM_LM_WIRE_OK);

    // Malformed mutations.
    memcpy(m, k_request, REQ_END); put32(m, 0x00110007u); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "header type/count");
    memcpy(m, k_request, REQ_END); put32(m + 4, 11u); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "data words");
    memcpy(m, k_request, REQ_END); put32(m + 4, 0x8000000cu); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "special header");
    memcpy(m, k_request, REQ_END); put32(m, 0x00120006u); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "buffer count");
    memcpy(m, k_request, REQ_END); put32(m, 0x00110016u); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "exch count");
    memcpy(m, k_request, REQ_END); m[8] = 1; expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "static index");
    memcpy(m, k_request, REQ_END); m[15] = 1; expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "static address");
    memcpy(m, k_request, REQ_END); m[32] = 2; expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "domain type");
    memcpy(m, k_request, REQ_END); m[33] = 1; expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "domain objects");
    memcpy(m, k_request, REQ_END); put16(m + 34, 0x20); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "domain size");
    memcpy(m, k_request, REQ_END); m[40] = 1; expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "domain reserved");
    memcpy(m, k_request, REQ_END); put32(m + 36, 0); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "child zero");
    memcpy(m, k_request, REQ_END); put32(m + 48, 0); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "sfci");
    memcpy(m, k_request, REQ_END); put32(m + 52, 0); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "cmif version");
    memcpy(m, k_request, REQ_END); put32(m + 56, 1); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "command");
    memcpy(m, k_request, REQ_END); put32(m + 60, 1); expect(m, REQ_END, DM_LM_WIRE_MALFORMED, "cmif token");
    expect(k_request, REQ_END - 1, DM_LM_WIRE_MALFORMED, "truncated 75");
    expect(k_request, 48, DM_LM_WIRE_MALFORMED, "truncated 48");
    expect(NULL, REQ_END, DM_LM_WIRE_MALFORMED, "null message");

    // Unsupported but structurally shaped values.
    memcpy(m, k_request, REQ_END); m[24] |= 1; expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "mode");
    memcpy(m, k_request, REQ_END); m[24] |= 0x20; expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "addr bit39");
    memcpy(m, k_request, REQ_END); m[25] |= 1; expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "reserved descriptor bits");
    memcpy(m, k_request, REQ_END); m[27] |= 1; expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "size high");
    memcpy(m, k_request, REQ_END); set_buffer(m, 0, 0x400); expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "null pointer");
    memcpy(m, k_request, REQ_END); set_buffer(m, 0x123456780ull, 0); expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "zero length");
    memcpy(m, k_request, REQ_END); set_buffer(m, 0x123456780ull, 1025); expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "length 1025");
    memcpy(m, k_request, REQ_END); set_buffer(m, 0x123456780ull, 0x10000); expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "64K length");
    memcpy(m, k_request, REQ_END); set_buffer(m, ADDR_LIMIT - 0x200, 0x400); expect(m, REQ_END, DM_LM_WIRE_UNSUPPORTED, "addr+len overflow");

    // Boundary and bits36..38 accepted cases.
    memcpy(m, k_request, REQ_END); set_buffer(m, ADDR_LIMIT - 1024, 1024);
    CHECK(dm_lm_wire_decode_log_request(m, REQ_END, &r) == DM_LM_WIRE_OK);
    CHECK(r.pointer == ADDR_LIMIT - 1024 && r.length == 1024u);
    memcpy(m, k_request, REQ_END); set_buffer(m, 0x7000000000ull, 1024);
    CHECK(get32(m + 24) == 0x1cu);
    CHECK(dm_lm_wire_decode_log_request(m, REQ_END, &r) == DM_LM_WIRE_OK);
    CHECK(r.pointer == 0x7000000000ull && r.length == 1024u);

    if (fail) { printf("lm wire test: FAILED\n"); return 1; }
    printf("lm wire test: all checks passed\n");
    return 0;
}

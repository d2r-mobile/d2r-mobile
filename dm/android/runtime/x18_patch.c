// 分配 256 字节真实栈帧保留暂存寄存器；宿主只取客体槽地址，原指令仍由 CPU 原生执行。
// Allocate a real 256-byte stack frame for scratch registers; the host only retrieves a guest-slot address, and the CPU executes the original operation natively.
#include "x18_patch.h"
#include <errno.h>

static int branch(uint32_t from, uint32_t to, uint32_t *word) {
    int64_t distance = (int64_t)to - from;
    if (distance % 4 || distance < -(1ll << 27) || distance >= (1ll << 27)) return ERANGE;
    *word = 0x14000000u | ((uint32_t)(distance / 4) & 0x03ffffffu);
    return 0;
}
int dm_patch_x18(const dm_x18_request *r, dm_x18_patch *out) {
    if (!r || !out || !r->host_gate || r->original_offset % 4 || r->stub_offset % 8 ||
        r->sp_delta < -128 || r->sp_delta > 128 || r->sp_delta % 16 ||
        r->stub_offset > UINT32_MAX - 64 || r->original_offset > UINT32_MAX - 4)
        return EINVAL;
    unsigned s = r->scratch[0], t = r->scratch[1], u = r->scratch[2];
    if (s > 15 || t > 15 || u > 15 || s == t || s == u || t == u ||
        ((uint64_t)r->stub_offset < (uint64_t)r->original_offset + 4 &&
         r->original_offset < (uint64_t)r->stub_offset + 64)) return EINVAL;
    dm_x18_patch patch = {.site = {.return_address = r->stub_offset + 20,
        .kind = DM_GATE_X18_ADDRESS, .argument = t}};
    uint32_t back;
    if (branch(r->original_offset, r->stub_offset, &patch.replacement) ||
        branch(r->stub_offset + 52, r->original_offset + 4, &back)) return ERANGE;
    unsigned delta = (unsigned)(r->sp_delta < 0 ? -r->sp_delta : r->sp_delta);
    const uint32_t words[] = {
        0xa9b00000u | t << 10 | 31u << 5 | s,
        0xa9010000u | 31u << 10 | 31u << 5 | u,
        0xa9027bfdu, 0x5800017eu, 0xd63f03c0u,
        0xa9427bfdu, 0x91040000u | 31u << 5 | u,
        0xf9400000u | t << 5 | s, r->translated,
        0xf9000000u | t << 5 | s,
        0xa9410000u | 31u << 10 | 31u << 5 | u,
        0xa8d00000u | t << 10 | 31u << 5 | s,
        (r->sp_delta < 0 ? 0xd1000000u : 0x91000000u) | delta << 10 | 31u << 5 | 31u,
        back
    };
    for (unsigned i = 0; i < 14; i++) for (unsigned b = 0; b < 4; b++)
        patch.stub[i * 4 + b] = (uint8_t)(words[i] >> (b * 8));
    for (unsigned b = 0; b < 8; b++) patch.stub[56 + b] = (uint8_t)((uint64_t)r->host_gate >> (b * 8));
    *out = patch;
    return 0;
}

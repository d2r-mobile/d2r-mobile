// 系统指令替换为 B→保存 fp/lr→宿主门→恢复→返回；依据 AArch64 编码与上游门 ABI，不使用红区。
// Replace a system instruction with B -> save fp/lr -> host gate -> restore -> return; based on AArch64 encodings / upstream gate ABI, no red zone.
#include "patch.h"
#include <errno.h>
#include <string.h>

static int branch(int64_t from, int64_t to, uint32_t *word) {
    int64_t offset = to - from;
    if (offset % 4 || offset < -(1ll << 27) || offset >= (1ll << 27)) return ERANGE;
    *word = 0x14000000u | ((uint32_t)(offset / 4) & 0x03ffffffu);
    return 0;
}

int dm_patch_system(const dm_patch_request *request, dm_patch_output *out) {
    if (!request || !out || !request->host_gate || request->original_offset % 4 || request->stub_offset % 8)
        return EINVAL;
    uint64_t original = request->original_offset, stub = request->stub_offset;
    if ((stub < original + 4 && original < stub + 32) || stub + 32 > UINT32_MAX) return EINVAL;
    dm_patch_output result = {.site = {.return_address = request->stub_offset + 12}};
    uint32_t word = request->instruction;
    if ((word & 0xffe0001fu) == 0xd4000001u) {
        result.site.kind = DM_GATE_SVC; result.site.argument = (word >> 5) & 0xffffu;
    } else {
        result.site.argument = word & 31u;
        switch (word & 0xffffffe0u) {
        case 0xd53be020u: case 0xd53be040u: result.site.kind = DM_GATE_TICKS; break;
        case 0xd53bd040u: result.site.kind = DM_GATE_TLS_READ; break;
        case 0xd51bd040u: result.site.kind = DM_GATE_TLS_WRITE; break;
        case 0xd53bd060u: result.site.kind = DM_GATE_TLR_READ; break;
        default: return ENOTSUP;
        }
    }
    // SVC 的异常返回会清除独占监视器；显式模拟 ERET，MRS/MSR 路径保持原布局。
    // SVC exception return clears the exclusive monitor; explicitly model ERET, retaining the MRS/MSR layout.
    int svc=result.site.kind==DM_GATE_SVC;
    uint32_t back;
    if (branch(original, stub, &result.replacement) || branch(stub + (svc?20:16), original + 4, &back)) return ERANGE;
    const uint32_t words[] = {0xa9bf7bfdu, 0x580000beu, 0xd63f03c0u, 0xa8c17bfdu,
        svc?0xd5033f5fu:back, svc?back:0xd503201fu};
    for (unsigned i = 0; i < 6; i++) for (unsigned b = 0; b < 4; b++)
        result.stub[i * 4 + b] = (uint8_t)(words[i] >> (b * 8));
    for (unsigned b = 0; b < 8; b++) result.stub[24 + b] = (uint8_t)((uint64_t)request->host_gate >> (b * 8));
    *out = result;
    return 0;
}

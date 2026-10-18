// 依据 Capstone 5 的操作数 / 寄存器信息，穷举最多四个寄存器字段并逐字核对反汇编，避免误改立即数。
// Use Capstone 5 operands / register metadata, enumerate at most four register fields and compare disassembly exactly to avoid changing immediates.
#include "decode.h"
#include <capstone/capstone.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct dm_decoder { csh handle; cs_insn *original, *candidate; };
dm_decoder *dm_decoder_create(void) {
    dm_decoder *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &d->handle) != CS_ERR_OK) { free(d); return NULL; }
    if (cs_option(d->handle, CS_OPT_DETAIL, CS_OPT_ON) != CS_ERR_OK) { dm_decoder_free(d); return NULL; }
    d->original = cs_malloc(d->handle); d->candidate = cs_malloc(d->handle);
    if (!d->original || !d->candidate) { dm_decoder_free(d); return NULL; }
    return d;
}
void dm_decoder_free(dm_decoder *d) {
    if (!d) return;
    if (d->original) cs_free(d->original, 1);
    if (d->candidate) cs_free(d->candidate, 1);
    cs_close(&d->handle); free(d);
}
static int decode(dm_decoder *d, cs_insn *ins, uint32_t word, uint32_t address) {
    uint8_t bytes[4];
    for (unsigned b = 0; b < 4; b++) bytes[b] = (uint8_t)(word >> (b * 8));
    const uint8_t *p = bytes; size_t size = 4; uint64_t pc = address;
    return cs_disasm_iter(d->handle, &p, &size, &pc, ins) && ins->size == 4;
}
static int gpr(const char *name) {
    if (!name) return -1;
    if (!strcmp(name, "sp") || !strcmp(name, "wsp")) return 31;
    if (!strcmp(name, "fp")) return 29;
    if (!strcmp(name, "lr")) return 30;
    if ((name[0] != 'x' && name[0] != 'w') || !isdigit((unsigned char)name[1])) return -1;
    char *end; unsigned long n = strtoul(name + 1, &end, 10);
    return *end || n > 30 ? -1 : (int)n;
}
static int renamed(const char *source, char target[256], unsigned s, unsigned u, int *has18) {
    size_t length = 0;
    *has18 = 0;
    while (*source) {
        char token[64]; size_t size = 0;
        if (!isalnum((unsigned char)*source) && *source != '_') {
            if (length >= 255) return E2BIG;
            target[length++] = *source++; continue;
        }
        while (isalnum((unsigned char)*source) || *source == '_') {
            if (size == sizeof(token) - 1) return E2BIG;
            token[size++] = *source++;
        }
        token[size] = 0;
        int reg = gpr(token);
        if (reg == 18) { *has18 = 1; snprintf(token, sizeof(token), "%c%u", token[0], s); }
        else if (reg == 31) snprintf(token, sizeof(token), "%c%u", token[0] == 'w' ? 'w' : 'x', u);
        size = strlen(token);
        if (size > 255 - length) return E2BIG;
        memcpy(target + length, token, size); length += size;
    }
    target[length] = 0;
    return 0;
}
static int exclusive(unsigned id) {
    switch (id) {
    case ARM64_INS_LDXR: case ARM64_INS_LDAXR: case ARM64_INS_LDXP: case ARM64_INS_LDAXP:
    case ARM64_INS_LDXRB: case ARM64_INS_LDAXRB: case ARM64_INS_LDXRH: case ARM64_INS_LDAXRH:
    case ARM64_INS_STXR: case ARM64_INS_STLXR: case ARM64_INS_STXP: case ARM64_INS_STLXP:
    case ARM64_INS_STXRB: case ARM64_INS_STLXRB: case ARM64_INS_STXRH: case ARM64_INS_STLXRH: return 1;
    default: return 0;
    }
}
int dm_decode_x18(dm_decoder *d, const dm_decode_input *in, dm_x18_request *out) {
    if (!d || !in || !out || in->offset % 4) return EINVAL;
    if (!decode(d, d->original, in->word, in->offset)) return EILSEQ;
    cs_insn *ins = d->original;
    char desired[256]; int has18;
    int result = renamed(ins->op_str, desired, 0, 0, &has18);
    if (result) return result;
    if (!has18) return ENOENT;
    if (exclusive(ins->id) || ins->id == ARM64_INS_ADR || ins->id == ARM64_INS_ADRP ||
        ins->id == ARM64_INS_MRS || ins->id == ARM64_INS_MSR ||
        cs_insn_group(d->handle, ins, CS_GRP_JUMP) || cs_insn_group(d->handle, ins, CS_GRP_CALL) ||
        cs_insn_group(d->handle, ins, CS_GRP_RET) || cs_insn_group(d->handle, ins, CS_GRP_INT) ||
        cs_insn_group(d->handle, ins, CS_GRP_PRIVILEGE)) return ENOTSUP;
    cs_arm64 *arm = &ins->detail->arm64;
    if ((ins->id == ARM64_INS_LDR || ins->id == ARM64_INS_LDRSW) &&
        arm->op_count > 1 && arm->operands[1].type == ARM64_OP_IMM) return ENOTSUP;
    cs_regs reads, writes; uint8_t nr, nw;
    if (cs_regs_access(d->handle, ins, reads, &nr, writes, &nw) != CS_ERR_OK) return ENOTSUP;
    uint32_t used = 0;
    int sp_written = 0, sp_delta = 0;
    for (unsigned i = 0; i < (unsigned)nr + nw; i++) {
        int n = gpr(cs_reg_name(d->handle, i < nr ? reads[i] : writes[i - nr]));
        if (n >= 0) { used |= 1u << n; if (i >= nr && n == 31) sp_written = 1; }
    }
    for (unsigned i = 0; i < arm->op_count; i++) {
        cs_arm64_op *op = &arm->operands[i];
        if (op->type == ARM64_OP_REG) {
            int n = gpr(cs_reg_name(d->handle, op->reg));
            if (n >= 0) used |= 1u << n;
        } else if (op->type == ARM64_OP_MEM) {
            int base = gpr(cs_reg_name(d->handle, op->mem.base));
            int index = gpr(cs_reg_name(d->handle, op->mem.index));
            if (base >= 0) used |= 1u << base;
            if (index >= 0) used |= 1u << index;
            if (base == 31 && (op->mem.index != ARM64_REG_INVALID || op->mem.disp < -128)) return ENOTSUP;
            if (base == 31 && arm->writeback) {
                sp_delta = op->mem.disp;
                if (arm->post_index) {
                    if (i + 1 >= arm->op_count || arm->operands[i + 1].type != ARM64_OP_IMM) return ENOTSUP;
                    int64_t delta = arm->operands[i + 1].imm;
                    if (delta < -128 || delta > 128) return ENOTSUP;
                    sp_delta = (int)delta;
                }
            }
        }
    }
    if ((sp_written && !arm->writeback) || sp_delta < -128 || sp_delta > 128 || sp_delta % 16) return ENOTSUP;
    dm_x18_request request = {.original_offset = in->offset, .sp_delta = sp_delta};
    unsigned picked = 0;
    for (unsigned i = 0; i < 16 && picked < 3; i++) if (!(used & (1u << i))) request.scratch[picked++] = i;
    if (picked != 3) return ENOTSUP;
    result = renamed(ins->op_str, desired, request.scratch[0], request.scratch[2], &has18);
    if (result) return result;
    // ADD 形式的 MOV x18,sp 改为寄存器 MOV，避免 SP 替换后别名变成 ADD #0。
    // Convert ADD-form MOV x18,sp to register MOV, avoiding ADD #0 alias changes after replacing SP.
    if (in->word == 0x910003f2u) {
        uint32_t word=0xaa0003e0u|request.scratch[2]<<16|request.scratch[0];
        if (!decode(d,d->candidate,word,in->offset) || strcmp(d->candidate->mnemonic,"mov") ||
            strcmp(d->candidate->op_str,desired)) return ENOTSUP;
        request.translated=word; *out=request; return 0;
    }
    const unsigned positions[] = {0,5,10,16};
    for (unsigned subset = 1; subset < 16; subset++) {
        uint32_t word = in->word; int valid = 1;
        for (unsigned i = 0; i < 4; i++) if (subset & (1u << i)) {
            unsigned p = positions[i], old = (in->word >> p) & 31;
            if (old != 18 && old != 31) { valid = 0; break; }
            unsigned next = old == 18 ? request.scratch[0] : request.scratch[2];
            word = (word & ~(31u << p)) | next << p;
        }
        if (valid && decode(d, d->candidate, word, in->offset) &&
            !strcmp(d->candidate->mnemonic, ins->mnemonic) && !strcmp(d->candidate->op_str, desired)) {
            request.translated = word; *out = request; return 0;
        }
    }
    return ENOTSUP;
}

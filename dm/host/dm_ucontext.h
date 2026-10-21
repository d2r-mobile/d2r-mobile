// 平台信号现场寄存器与 ESR WnR 访问；Android 的 Bionic 不暴露 Mach __es。
// Platform signal-context registers and ESR WnR access; Bionic does not expose Mach __es.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/ucontext.h>

#define DM_UC_WRITE_UNKNOWN (-1)
#define DM_UC_WRITE_READ 0
#define DM_UC_WRITE_WRITE 1

// 有界遍历 Android arm64 __reserved 记录，寻找 ESR_MAGIC。返回 -1 表示无记录/截断/畸形，
// 绝不能把未知记录当成读或成功处理。Bounded traversal of Android arm64 __reserved records for ESR_MAGIC;
// -1 means absent/truncated/malformed and must never be reported as a read or a handled write.
static inline int dm_uc_esr_write_record(const uint8_t *data, size_t size) {
    if (!data || size < 8) return DM_UC_WRITE_UNKNOWN;
    for (size_t offset = 0; offset + 8 <= size;) {
        uint32_t magic = 0, length = 0;
        memcpy(&magic, data + offset, 4);
        memcpy(&length, data + offset + 4, 4);
        if (magic == 0 || length < 8 || length > size - offset) return DM_UC_WRITE_UNKNOWN;
        if (magic == 0x45535201u) {
            if (length < 16) return DM_UC_WRITE_UNKNOWN;
            uint64_t esr = 0;
            memcpy(&esr, data + offset + 8, 8);
            return (int)((esr >> 6) & 1u);
        }
        offset += length;
    }
    return DM_UC_WRITE_UNKNOWN;
}

#ifdef __ANDROID__
#define DM_UC_PC(uc) ((uc)->uc_mcontext.pc)
#define DM_UC_SP(uc) ((uc)->uc_mcontext.sp)
#define DM_UC_REG(uc, index) ((uc)->uc_mcontext.regs[(index)])
#define DM_UC_FP(uc) DM_UC_REG(uc, 29)
#define DM_UC_LR(uc) DM_UC_REG(uc, 30)
static inline int dm_uc_is_write(const ucontext_t *uc) {
    return dm_uc_esr_write_record((const uint8_t *)uc->uc_mcontext.__reserved,
                                  sizeof uc->uc_mcontext.__reserved);
}
#else
#define DM_UC_PC(uc) ((uc)->uc_mcontext->__ss.__pc)
#define DM_UC_SP(uc) ((uc)->uc_mcontext->__ss.__sp)
#define DM_UC_REG(uc, index) ((uc)->uc_mcontext->__ss.__x[(index)])
#define DM_UC_FP(uc) ((uc)->uc_mcontext->__ss.__fp)
#define DM_UC_LR(uc) ((uc)->uc_mcontext->__ss.__lr)
static inline int dm_uc_is_write(const ucontext_t *uc) {
    return (int)((uc->uc_mcontext->__es.__esr >> 6) & 1);
}
#endif

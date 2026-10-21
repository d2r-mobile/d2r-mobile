// 有界 lm Log 请求帧校验：字段数值有效性与客体内存可访问性是两件事。
// Bounded lm Log request-frame validation: numeric field validity is not
// guest-memory accessibility.
#include "lm_wire.h"
#include <string.h>

#define DM_LM_LOG_END 76u
#define DM_LM_LOG_ADDR_LIMIT (1ull << 39)
#define DM_LM_LOG_MAX_LENGTH 1024u
#define DM_LM_LOG_MAGIC 0x49434653u

static uint16_t get16(const uint8_t *p) {
    uint16_t value;
    memcpy(&value, p, sizeof value);
    return value;
}

static uint32_t get32(const uint8_t *p) {
    uint32_t value;
    memcpy(&value, p, sizeof value);
    return value;
}

dm_lm_wire_status dm_lm_wire_decode_log_request(const uint8_t *message, size_t size,
                                                dm_lm_wire_log_request *out) {
    if (message == NULL || out == NULL || size < DM_LM_LOG_END) return DM_LM_WIRE_MALFORMED;

    // 头部必须精确为 type6、static1、buffer1、12 data words，无 special/handle 位。
    // Header must be exactly type6/static1/buffer1/12 data words, no special/handles.
    if (get32(message) != 0x00110006u || get32(message + 4) != 12u) return DM_LM_WIRE_MALFORMED;

    // 8 字节 static 域必须全零（索引 0、地址 0、长度 0）。
    // The 8-byte static slot must be entirely zero (index 0, address 0, length 0).
    for (size_t i = 8; i < 16; ++i)
        if (message[i] != 0) return DM_LM_WIRE_MALFORMED;

    const uint32_t size_low = get32(message + 16);
    const uint32_t address_low = get32(message + 20);
    const uint32_t packed = get32(message + 24);
    const uint32_t mode = packed & 3u;
    const uint32_t size_high = (packed >> 24) & 0xfu;
    const uint32_t address_mid = (packed >> 28) & 0xfu;
    const uint32_t address_high = (packed >> 2) & 0x7u;

    if (mode != 0) return DM_LM_WIRE_UNSUPPORTED;
    if ((packed & 0x00ffffe0u) != 0) return DM_LM_WIRE_UNSUPPORTED;
    if (size_high != 0) return DM_LM_WIRE_UNSUPPORTED;

    const uint64_t pointer = (uint64_t)address_low |
                             ((uint64_t)address_mid << 32) |
                             ((uint64_t)address_high << 36);
    const uint32_t length = size_low;
    if (length == 0 || length > DM_LM_LOG_MAX_LENGTH) return DM_LM_WIRE_UNSUPPORTED;
    if (pointer == 0 || pointer >= DM_LM_LOG_ADDR_LIMIT) return DM_LM_WIRE_UNSUPPORTED;
    if ((uint64_t)length > DM_LM_LOG_ADDR_LIMIT - pointer) return DM_LM_WIRE_UNSUPPORTED;

    // 域头：type1、num_in_objects=0、data_size=16、保留 padding=0。
    // Domain header: type1, no in-objects, data_size16, reserved padding zero.
    if (message[32] != 1 || message[33] != 0 || get16(message + 34) != 0x10u ||
        get32(message + 40) != 0)
        return DM_LM_WIRE_MALFORMED;

    const uint32_t object_id = get32(message + 36);
    const uint32_t token = get32(message + 44);
    if (object_id == 0) return DM_LM_WIRE_MALFORMED; // 数值域；不检查活动子对象归属。 / numeric only; live-child membership is separate.

    // CMIF：SFCI、version1、command0、token0。无 raw/in-object 载荷。
    // CMIF: SFCI v1 command0 token0, with no raw/in-object payload.
    if (get32(message + 48) != DM_LM_LOG_MAGIC || get32(message + 52) != 1u ||
        get32(message + 56) != 0u || get32(message + 60) != 0u)
        return DM_LM_WIRE_MALFORMED;

    out->pointer = pointer;
    out->length = length;
    out->object_id = object_id;
    out->token = token;
    return DM_LM_WIRE_OK;
}

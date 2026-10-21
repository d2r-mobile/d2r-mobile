#pragma once
#include <stddef.h>
#include <stdint.h>

// 纯 lm Log 请求帧解码器；不访问客体指针、不发布回复、不代表日志服务已实现。
// Pure lm Log request-frame decoder; never dereferences the guest pointer,
// publishes a reply, or claims a logging service exists.
typedef enum {
    DM_LM_WIRE_OK = 0,
    DM_LM_WIRE_MALFORMED = 1,
    DM_LM_WIRE_UNSUPPORTED = 2
} dm_lm_wire_status;

typedef struct {
    uint64_t pointer;
    uint32_t length;
    uint32_t object_id;
    uint32_t token;
} dm_lm_wire_log_request;

// 成功时写入完整结果；失败时不修改 out。object_id 仅为数值，资源归属由调用方另行校验。
// Writes a complete result only on success; leaves out untouched on failure.
// object_id is numeric only; resource membership must be checked by the caller.
dm_lm_wire_status dm_lm_wire_decode_log_request(const uint8_t *message, size_t size,
                                                dm_lm_wire_log_request *out);

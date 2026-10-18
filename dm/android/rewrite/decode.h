// 宿主离线 x18 解码边界；复用上游 Capstone 操作数字段替换法，不链接进 Android 运行时。
// Host-only offline x18 decode boundary; reuse upstream Capstone operand-field substitution, never linked into the Android runtime.
#pragma once
#include "../runtime/x18_patch.h"
typedef struct dm_decoder dm_decoder;
typedef struct { uint32_t word, offset; } dm_decode_input;
dm_decoder *dm_decoder_create(void);
void dm_decoder_free(dm_decoder *decoder);
// 成功返回已核对替换的请求；ENOENT 表示没有通用 x18，ENOTSUP 表示需要特殊改写，EILSEQ 表示无法解码。
// Success returns a verified replacement request; ENOENT means no GPR x18, ENOTSUP needs a special rewrite, EILSEQ is undecodable.
int dm_decode_x18(dm_decoder *decoder, const dm_decode_input *input, dm_x18_request *out);

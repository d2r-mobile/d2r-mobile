// 原项目资源缓存使用的 MD5/SHA1 标识。| MD5/SHA1 identifiers used by upstream asset caches.
#pragma once
#include <stdint.h>
unsigned char *dm_android_md5(const void *data, uint32_t length, unsigned char *output);
unsigned char *dm_android_sha1(const void *data, uint32_t length, unsigned char *output);
typedef struct { uint32_t state[8]; uint64_t length; unsigned used; unsigned char buffer[64]; } dm_android_sha256_context;
int dm_android_sha256_init(dm_android_sha256_context *context);
int dm_android_sha256_update(dm_android_sha256_context *context, const void *data, uint32_t length);
int dm_android_sha256_final(unsigned char output[32], dm_android_sha256_context *context);

// 仅桥接上游实际使用的资源摘要 API。| Bridge only the asset digest APIs upstream actually uses.
#pragma once
#include "../../legacy_digest.h"
typedef uint32_t CC_LONG;
#define CC_MD5 dm_android_md5
#define CC_SHA1 dm_android_sha1
#define CC_MD5_DIGEST_LENGTH 16
#define CC_SHA1_DIGEST_LENGTH 20
typedef dm_android_sha256_context CC_SHA256_CTX;
#define CC_SHA256_Init dm_android_sha256_init
#define CC_SHA256_Update dm_android_sha256_update
#define CC_SHA256_Final dm_android_sha256_final
#define CC_SHA256_DIGEST_LENGTH 32

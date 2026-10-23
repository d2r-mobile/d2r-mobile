// 安卓构建：宿主用到的 CommonCrypto 摘要（MD5 / SHA-1 / SHA-256，只用于诊断记号与校验），接口与苹果同名同参，实现在 dm/android/compat/digest.c。
// Android build: the CommonCrypto digests the host uses (MD5 / SHA-1 / SHA-256, only for diagnostic tokens and checks), with the same names and parameters as on Apple; implemented in dm/android/compat/digest.c.
#pragma once
#include <stdint.h>
typedef uint32_t CC_LONG;
#define CC_MD5_DIGEST_LENGTH 16
#define CC_SHA1_DIGEST_LENGTH 20
#define CC_SHA256_DIGEST_LENGTH 32
typedef struct { uint32_t h[8]; uint64_t n; uint8_t buf[64]; uint32_t used; } CC_SHA256_CTX;
int CC_SHA256_Init(CC_SHA256_CTX *c);
int CC_SHA256_Update(CC_SHA256_CTX *c, const void *data, CC_LONG len);
int CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *c);
unsigned char *CC_SHA256(const void *data, CC_LONG len, unsigned char *md);
unsigned char *CC_SHA1(const void *data, CC_LONG len, unsigned char *md);
unsigned char *CC_MD5(const void *data, CC_LONG len, unsigned char *md);

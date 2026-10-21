// 标准 MD5/SHA1 单次摘要，替换资源工具中的 CommonCrypto 调用。
// Standard one-shot MD5/SHA1 digests replacing CommonCrypto calls in asset helpers.
#include "legacy_digest.h"
#include <string.h>
#include <stddef.h>
static uint32_t rotate(uint32_t value, unsigned count) { return (value << count) | (value >> (32 - count)); }
static uint32_t read32(const unsigned char *p, int big) {
    if (big) return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static void write32(unsigned char *p, uint32_t value, int big) {
    for (unsigned i = 0; i < 4; ++i) p[big ? 3 - i : i] = value >> (8 * i);
}
static void md5_block(uint32_t *state, const unsigned char *data) {
    static const uint32_t constants[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    static const unsigned shifts[16] = {7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21};
    uint32_t words[16]; for (unsigned i = 0; i < 16; ++i) words[i] = read32(data + 4 * i, 0);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t f; unsigned word;
        if (i < 16) { f = (b & c) | (~b & d); word = i; }
        else if (i < 32) { f = (d & b) | (~d & c); word = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; word = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); word = (7 * i) & 15; }
        uint32_t old = d; d = c; c = b;
        b += rotate(a + f + constants[i] + words[word], shifts[(i / 16) * 4 + i % 4]); a = old;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}
static void sha1_block(uint32_t *state, const unsigned char *data) {
    uint32_t words[80]; for (unsigned i = 0; i < 16; ++i) words[i] = read32(data + 4 * i, 1);
    for (unsigned i = 16; i < 80; ++i) words[i] = rotate(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (unsigned i = 0; i < 80; ++i) {
        uint32_t f, constant;
        if (i < 20) { f = (b & c) | (~b & d); constant = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; constant = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); constant = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; constant = 0xca62c1d6; }
        uint32_t next = rotate(a, 5) + f + e + constant + words[i];
        e = d; d = c; c = rotate(b, 30); b = a; a = next;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}
static unsigned char *digest(const void *input, uint32_t length, unsigned char *output, int sha1) {
    uint32_t state[5] = {0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
    const unsigned char *data = input;
    size_t blocks = length / 64;
    for (size_t i = 0; i < blocks; ++i) (sha1 ? sha1_block : md5_block)(state, data + 64 * i);
    unsigned char tail[128] = {0}; size_t remaining = length % 64;
    if (remaining) memcpy(tail, data + blocks * 64, remaining);
    tail[remaining] = 0x80;
    size_t padded = remaining < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)length * 8;
    for (unsigned i = 0; i < 8; ++i) tail[padded - 8 + (sha1 ? 7 - i : i)] = bits >> (8 * i);
    for (size_t i = 0; i < padded; i += 64) (sha1 ? sha1_block : md5_block)(state, tail + i);
    for (unsigned i = 0; i < (sha1 ? 5u : 4u); ++i) write32(output + i * 4, state[i], sha1);
    return output;
}
unsigned char *dm_android_md5(const void *data, uint32_t length, unsigned char *output) { return digest(data, length, output, 0); }
unsigned char *dm_android_sha1(const void *data, uint32_t length, unsigned char *output) { return digest(data, length, output, 1); }

// SHA256 增量接口用于可选装载诊断，不用于校验用户游戏包。
// Incremental SHA256 is for optional load diagnostics, not validating the user's game packages.
static void sha256_block(dm_android_sha256_context *c, const unsigned char *data) {
    static const uint32_t constants[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t w[64]; for (unsigned i = 0; i < 16; ++i) w[i] = read32(data + 4 * i, 1);
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t x = w[i - 15], y = w[i - 2];
        w[i] = w[i - 16] + (rotate(x, 25) ^ rotate(x, 14) ^ (x >> 3)) + w[i - 7] + (rotate(y, 15) ^ rotate(y, 13) ^ (y >> 10));
    }
    uint32_t a=c->state[0], b=c->state[1], d=c->state[3], e=c->state[4], f=c->state[5], g=c->state[6], h=c->state[7], cc=c->state[2];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t t1 = h + (rotate(e,26)^rotate(e,21)^rotate(e,7)) + ((e&f)^(~e&g)) + constants[i] + w[i];
        uint32_t t2 = (rotate(a,30)^rotate(a,19)^rotate(a,10)) + ((a&b)^(a&cc)^(b&cc));
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->state[0]+=a; c->state[1]+=b; c->state[2]+=cc; c->state[3]+=d; c->state[4]+=e; c->state[5]+=f; c->state[6]+=g; c->state[7]+=h;
}
int dm_android_sha256_init(dm_android_sha256_context *c) {
    *c = (dm_android_sha256_context){.state={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    return 1;
}
int dm_android_sha256_update(dm_android_sha256_context *c, const void *input, uint32_t length) {
    const unsigned char *data = input; c->length += length;
    while (length) {
        unsigned n = 64 - c->used; if (n > length) n = length;
        memcpy(c->buffer + c->used, data, n); c->used += n; data += n; length -= n;
        if (c->used == 64) { sha256_block(c, c->buffer); c->used = 0; }
    }
    return 1;
}
int dm_android_sha256_final(unsigned char output[32], dm_android_sha256_context *c) {
    uint64_t bits = c->length * 8;
    c->buffer[c->used++] = 0x80;
    if (c->used > 56) { memset(c->buffer + c->used, 0, 64 - c->used); sha256_block(c, c->buffer); c->used = 0; }
    memset(c->buffer + c->used, 0, 56 - c->used);
    for (unsigned i = 0; i < 8; ++i) c->buffer[63 - i] = bits >> (8 * i);
    sha256_block(c, c->buffer);
    for (unsigned i = 0; i < 8; ++i) write32(output + 4 * i, c->state[i], 1);
    return 1;
}

// NSO 仅需要 SHA-256；依据 Mbed TLS 3.6.4 的最小配置机制。
// NSO only needs SHA-256; based on the Mbed TLS 3.6.4 minimal configuration mechanism.
#pragma once
#define MBEDTLS_SHA256_C

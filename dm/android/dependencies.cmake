# 固定下载摘要，只构建 NSO 解压和校验需要的源文件；许可文本随构建缓存保留。
# Pin archive digests and build only NSO decompression / checksum sources; keep licenses in the build cache.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(dm_lz4
    URL https://codeload.github.com/lz4/lz4/tar.gz/refs/tags/v1.10.0
    URL_HASH SHA256=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b
    SOURCE_SUBDIR unused)
FetchContent_Declare(dm_mbedtls
    URL https://codeload.github.com/Mbed-TLS/mbedtls/tar.gz/refs/tags/v3.6.4
    URL_HASH SHA256=a1e01f6094bb744ead8caaa92d7c7102b7137a813f509b49afaebc0b8e51899c
    SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(dm_lz4 dm_mbedtls)
add_library(dm_nso_dependencies STATIC
    ${dm_lz4_SOURCE_DIR}/lib/lz4.c
    ${dm_mbedtls_SOURCE_DIR}/library/sha256.c
    ${dm_mbedtls_SOURCE_DIR}/library/platform_util.c)
target_include_directories(dm_nso_dependencies PUBLIC
    ${dm_lz4_SOURCE_DIR}/lib ${dm_mbedtls_SOURCE_DIR}/include)
target_compile_definitions(dm_nso_dependencies PUBLIC
    MBEDTLS_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/crypto_config.h")

// ABI used by gpu_driver.c, transcribed from libadrenotools at the fixed
// reference commit. The build does not vendor the driver library; H3 supplies
// the built libadrenotools.so, and this header only lets the NDK compile the
// optional custom-driver path without guessing the API.
//
// libadrenotools: https://github.com/eden-emulator/libadrenotools
// Fixed commit: 8ba23b42d7
// SPDX-License-Identifier: BSD-2-Clause
// Full license: gpu_driver_adrenotools.LICENSE.txt
// Copyright (c) 2021 Billy Laws
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ADRENOTOOLS_DRIVER_CUSTOM = 1 << 0,
    ADRENOTOOLS_DRIVER_FILE_REDIRECT = 1 << 1,
    ADRENOTOOLS_DRIVER_GPU_MAPPING_IMPORT = 1 << 2,
};

void *adrenotools_open_libvulkan(int dlopenMode, int featureFlags,
                                 const char *tmpLibDir, const char *hookLibDir,
                                 const char *customDriverDir, const char *customDriverName,
                                 const char *fileRedirectDir, void **userMappingHandle);

#ifdef __cplusplus
}
#endif

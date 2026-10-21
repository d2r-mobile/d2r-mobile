// 原宿主 API 的旁路镜像；不依赖 pthread_key_t 的数值或 Bionic 私有布局。
// Side mirror of the original host API without depending on pthread_key_t values or Bionic private layout.
#include "host_tls.h"
#include "runtime.h"
#include <string.h>
static pthread_key_t keys[8];
static int bound;
void dm_android_bind_keys(const pthread_key_t values[8]) { memcpy(keys, values, sizeof keys); bound = 1; }
int dm_android_host_setspecific(pthread_key_t key, const void *value) {
    int result = pthread_setspecific(key, value);
    if (!result && bound) for (unsigned i = 0; i < 8; ++i)
        if (keys[i] == key) dm_android_context_get()->slots[i] = (uintptr_t)value;
    return result;
}

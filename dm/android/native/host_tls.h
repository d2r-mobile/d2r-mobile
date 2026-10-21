// 让原宿主 pthread 键同时发布到 Android 客体上下文。
// Publish original host pthread key values into the Android guest context as well.
#pragma once
#include <pthread.h>
void dm_android_bind_keys(const pthread_key_t keys[8]);
int dm_android_host_setspecific(pthread_key_t key, const void *value);

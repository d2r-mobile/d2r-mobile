// Android 客体线程环境与已改写映像装载。| Android guest thread context and rewritten-image loading.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { DM_K18, DM_KTLS, DM_KTLR, DM_KGATE, DM_KRES0, DM_KRES1, DM_KASPECT, DM_KASPECTUI, DM_NSLOTS };
typedef struct {
    uint64_t slots[DM_NSLOTS];
    uint8_t tls[0x10000];
    uint8_t tlr[0x400];
    // 后续 pthread 析构仍可读取 TLS，但不可重新发布即将释放的缓存。
    // Later pthread destructors may still read TLS, but must not republish a retiring cache entry.
    uint64_t cache_terminating;
} dm_android_context;

typedef struct {
    void *handle;
    uint8_t *image;
    size_t size;
    uint64_t text_end, patch_off, key_page_off;
} dm_android_module;

dm_android_context *dm_android_thread_init(void *gate);
dm_android_context *dm_android_context_get(void);
int64_t dm_android_slot_offset(unsigned slot);
int dm_android_load(dm_android_module *module, const char *library, const char *layout, char *error, size_t capacity);
void dm_android_unload(dm_android_module *module);
int dm_android_runtime_selftest(char *report, size_t capacity);

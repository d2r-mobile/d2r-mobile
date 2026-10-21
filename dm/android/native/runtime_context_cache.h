// TP 只是不透明键；不读取 Android 私有 TLS 槽或 Bionic DTV。
// TP is only an opaque key; never read Android private TLS slots or Bionic's DTV.
#ifndef DM_ANDROID_RUNTIME_CONTEXT_CACHE_H
#define DM_ANDROID_RUNTIME_CONTEXT_CACHE_H
// Bionic 线程 TP 低 12 位相同（同页内偏移），间距约 1 MiB：哈希取页号并折叠高位；
// 撞桶时向后线性探测 PROBES 项（数组多留 PROBES-1 项，不回绕），登记不再因碰撞永久落到慢路。
// Bionic thread TPs share their low 12 bits (same in-page offset) and are ~1 MiB apart: hash the page number folded with
// its high bits; on a collision probe PROBES entries forward (the array has PROBES-1 spare entries, no wrap), so registration no longer leaves a thread on the slow path for good.
#define DM_ANDROID_CONTEXT_CACHE_BITS 10
#define DM_ANDROID_CONTEXT_CACHE_SLOTS (1 << DM_ANDROID_CONTEXT_CACHE_BITS)
#define DM_ANDROID_CONTEXT_CACHE_HASH_SHIFT 12
#define DM_ANDROID_CONTEXT_CACHE_HASH_FOLD 10
#define DM_ANDROID_CONTEXT_CACHE_PROBES 4
#define DM_ANDROID_CONTEXT_CACHE_ENTRIES (DM_ANDROID_CONTEXT_CACHE_SLOTS + DM_ANDROID_CONTEXT_CACHE_PROBES - 1)
#define DM_ANDROID_CONTEXT_CACHE_ENTRY_SHIFT 5
#ifndef __ASSEMBLER__
#include "runtime.h"
#include <stdatomic.h>
#include <pthread.h>

typedef struct __attribute__((aligned(32))) {
    _Atomic uint64_t sequence;
    _Atomic uintptr_t tp,context;
    uint64_t padding;
} dm_android_context_cache_entry;
_Static_assert(sizeof(dm_android_context_cache_entry)==32,"context cache assembly stride");
_Static_assert(offsetof(dm_android_context_cache_entry,tp)==8,"context cache assembly TP offset");
_Static_assert(offsetof(dm_android_context_cache_entry,context)==16,"context cache assembly pointer offset");
extern __attribute__((visibility("hidden"))) dm_android_context_cache_entry dm_android_context_cache[DM_ANDROID_CONTEXT_CACHE_ENTRIES];
static inline unsigned dm_android_context_cache_bucket(uintptr_t tp) {
    uintptr_t page=tp>>DM_ANDROID_CONTEXT_CACHE_HASH_SHIFT;
    return (page^(page>>DM_ANDROID_CONTEXT_CACHE_HASH_FOLD))&(DM_ANDROID_CONTEXT_CACHE_SLOTS-1);
}
__attribute__((visibility("hidden"))) dm_android_context *dm_android_context_cache_lookup(uintptr_t tp);
// 只能由当前线程撤销；信号处理器不可调用加锁的登记/撤销接口。
// Withdrawal is current-thread-only; signal handlers must not call locking registration/withdrawal APIs.
// 匹配当前TP后项不被其他线程改写；所有者退出清理完成后TP才能复用。
// Other threads cannot modify a current-TP hit; owner teardown completes before TP reuse.
__attribute__((visibility("hidden"))) void dm_android_context_cache_forget(uintptr_t tp,const dm_android_context *context);
// 默认调用公开 pthread API；测试可覆盖这两个冷路径适配器注入真实失败返回。
// Defaults call public pthread APIs; tests may override these cold-path adapters to inject real failure returns.
__attribute__((visibility("hidden"))) int dm_android_context_cache_key_create(pthread_key_t *key,void (*destructor)(void *));
__attribute__((visibility("hidden"))) int dm_android_context_cache_set_specific(pthread_key_t key,const void *value);
#endif
#endif

// 使用公开编译器 TLS 支持，不依赖 Bionic 私有 pthread 槽布局。
// Use compiler-supported TLS without relying on Bionic's private pthread slot layout.
#include "runtime.h"
#include "runtime_context_cache.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>

// 隐藏的 ELF TLS 符号也供专用汇编入口直接访问，避免经过普通 C 调用约定。
// A hidden ELF TLS symbol lets the dedicated assembly entry bypass the ordinary C ABI.
__attribute__((visibility("hidden"))) __thread dm_android_context dm_android_context_storage;
extern void dm_android_context_entry(void);

__attribute__((visibility("hidden"))) dm_android_context_cache_entry dm_android_context_cache[DM_ANDROID_CONTEXT_CACHE_SLOTS];
static pthread_mutex_t context_cache_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t context_cache_key;
static int context_cache_key_ready;
// 仅登记慢路持锁访问；最多记录64个不同TP的首次结果，不增加TLS对象或入口计数。
// Only registration's locked slow path accesses these; log the first result for at most 64 distinct TPs.
static uintptr_t context_registration_logged[64];
static unsigned context_registration_logged_count;
static void context_registration_log(uintptr_t tp,uintptr_t owner,const char *result,int error) {
    for(unsigned i=0;i<context_registration_logged_count;i++)if(context_registration_logged[i]==tp)return;
    if(context_registration_logged_count==64)return;
    context_registration_logged[context_registration_logged_count++]=tp;
    int saved_errno=errno;
    fprintf(stderr,"[dm] [context-register] tid=%ld bucket=%u tp=%#llx owner=%#llx result=%s error=%d first_tp_result=1\n",
        (long)syscall(SYS_gettid),dm_android_context_cache_bucket(tp),(unsigned long long)tp,(unsigned long long)owner,result,error);
    errno=saved_errno;
}
__attribute__((weak,visibility("hidden"))) int dm_android_context_cache_key_create(pthread_key_t *key,void (*destructor)(void *)) {return pthread_key_create(key,destructor);}
__attribute__((weak,visibility("hidden"))) int dm_android_context_cache_set_specific(pthread_key_t key,const void *value) {return pthread_setspecific(key,value);}

dm_android_context *dm_android_context_cache_lookup(uintptr_t tp) {
    const dm_android_context_cache_entry *entry=&dm_android_context_cache[dm_android_context_cache_bucket(tp)];
    uint64_t before=atomic_load_explicit(&entry->sequence,memory_order_acquire);
    if(before&1||atomic_load_explicit(&entry->tp,memory_order_relaxed)!=tp)return NULL;
    uintptr_t value=atomic_load_explicit(&entry->context,memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire);
    return before==atomic_load_explicit(&entry->sequence,memory_order_relaxed)?(dm_android_context*)value:NULL;
}

static void context_cache_write(dm_android_context_cache_entry *entry,uintptr_t tp,uintptr_t value,uint64_t sequence) {
    atomic_store_explicit(&entry->sequence,sequence+1,memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    // 发布先写内容再发布所有者；撤销先清所有者，信号重入不会看到半初始化指针。
    // Publish contents before the owner; withdraw the owner first for safe signal reentry.
    if(tp){
        atomic_store_explicit(&entry->context,value,memory_order_relaxed);
        atomic_store_explicit(&entry->tp,tp,memory_order_release);
    }else{
        atomic_store_explicit(&entry->tp,0,memory_order_release);
        atomic_store_explicit(&entry->context,0,memory_order_relaxed);
    }
    atomic_store_explicit(&entry->sequence,sequence+2,memory_order_release);
}

void dm_android_context_cache_forget(uintptr_t tp,const dm_android_context *context) {
    // 只有所有者能撤销；禁止另一线程驱逐当前线程正在读取的项。
    // Only the owner may withdraw; another thread cannot evict a current-thread read.
    if(tp!=(uintptr_t)__builtin_thread_pointer())return;
    if(pthread_mutex_lock(&context_cache_lock))return;
    dm_android_context_cache_entry *entry=&dm_android_context_cache[dm_android_context_cache_bucket(tp)];
    uint64_t sequence=atomic_load_explicit(&entry->sequence,memory_order_relaxed);
    if(!(sequence&1)&&atomic_load_explicit(&entry->tp,memory_order_relaxed)==tp&&atomic_load_explicit(&entry->context,memory_order_relaxed)==(uintptr_t)context)
        context_cache_write(entry,0,0,sequence);
    pthread_mutex_unlock(&context_cache_lock);
}

static void context_cache_destroy(void *value) {
    ((dm_android_context*)value)->cache_terminating=1;
    dm_android_context_cache_forget((uintptr_t)__builtin_thread_pointer(),value);
}

static void context_cache_register(uintptr_t tp,dm_android_context *context) {
    if(pthread_mutex_lock(&context_cache_lock))return;
    dm_android_context_cache_entry *entry=&dm_android_context_cache[dm_android_context_cache_bucket(tp)];
    uint64_t sequence=atomic_load_explicit(&entry->sequence,memory_order_relaxed);
    uintptr_t old_tp=atomic_load_explicit(&entry->tp,memory_order_relaxed);
    // 非空项永不替换，包括相同TP；必须由所有者先显式撤销。
    // Never replace an occupied entry, even for the same TP; its owner must withdraw first.
    if((sequence&1)||old_tp){
        context_registration_log(tp,old_tp,(sequence&1)?"busy":old_tp==tp?"already_registered":"collision",0);
        pthread_mutex_unlock(&context_cache_lock);return;
    }
    // 析构登记成功才发布；失败保持真正 TLSDESC，可在之后的 C 初始化访问重试。
    // Publish only after destructor registration succeeds; failures retain real TLSDESC and may retry on later C initialization access.
    if(!context_cache_key_ready){
        int error=dm_android_context_cache_key_create(&context_cache_key,context_cache_destroy);
        if(error){context_registration_log(tp,old_tp,"key_create_failed",error);pthread_mutex_unlock(&context_cache_lock);return;}
        context_cache_key_ready=1;
    }
    int error=dm_android_context_cache_set_specific(context_cache_key,context);
    if(error){context_registration_log(tp,old_tp,"setspecific_failed",error);pthread_mutex_unlock(&context_cache_lock);return;}
    context_cache_write(entry,tp,(uintptr_t)context,sequence);
    context_registration_log(tp,tp,"success",0);pthread_mutex_unlock(&context_cache_lock);
}

dm_android_context *dm_android_context_get(void) {
    uintptr_t tp=(uintptr_t)__builtin_thread_pointer();
    const dm_android_context_cache_entry *entry=&dm_android_context_cache[dm_android_context_cache_bucket(tp)];
    // 仅当前线程可用此快路；任意TP查询仍使用上面的序号协议。
    // This fast path is current-thread-only; arbitrary TP queries retain the sequence protocol above.
    dm_android_context *cached=atomic_load_explicit(&entry->tp,memory_order_acquire)==tp?
        (dm_android_context*)atomic_load_explicit(&entry->context,memory_order_relaxed):NULL;
    if(cached)return cached;
    dm_android_context *context=&dm_android_context_storage;
    if(!context->cache_terminating)context_cache_register(tp,context);
    return context;
}

dm_android_context *dm_android_thread_init(void *gate) {
    dm_android_context *context=dm_android_context_get();uint64_t terminating=context->cache_terminating;
    memset(context,0,sizeof *context);context->cache_terminating=terminating;
    context->slots[DM_KTLS] = (uintptr_t)context->tls;
    context->slots[DM_KTLR] = (uintptr_t)context->tlr;
    context->slots[DM_KGATE] = (uintptr_t)gate;
    context->slots[DM_KRES0] = ((uint64_t)720 << 32) | 1280;
    context->slots[DM_KRES1] = ((uint64_t)900 << 32) | 1600;
    float aspect = 19.0f / 9.0f;
    uint32_t bits;
    memcpy(&bits, &aspect, sizeof bits);
    context->slots[DM_KASPECT] = context->slots[DM_KASPECTUI] = bits;
    return context;
}

int64_t dm_android_slot_offset(unsigned slot) {
    if (slot >= DM_NSLOTS) abort();
    return slot * sizeof(uint64_t);
}

int dm_android_load(dm_android_module *m, const char *library, const char *layout, char *error, size_t cap) {
    memset(m, 0, sizeof *m);
    FILE *f = fopen(layout, "r");
    if (!f) { snprintf(error, cap, "layout: %s", strerror(errno)); return -1; }
    char line[256], key[40]; unsigned long long value;
    unsigned seen = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "# %39s %llx", key, &value) != 2) continue;
        if (!strcmp(key, "TEXT_END")) { m->text_end = value; seen |= 1; }
        else if (!strcmp(key, "PATCH_OFF")) { m->patch_off = value; seen |= 2; }
        else if (!strcmp(key, "KEY_PAGE_OFF")) { m->key_page_off = value; seen |= 4; }
    }
    fclose(f);
    if (seen != 7 || m->text_end > m->key_page_off || m->key_page_off + 0x4000 > m->patch_off) {
        snprintf(error, cap, "Invalid module layout"); return -1;
    }
    m->handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!m->handle) { snprintf(error, cap, "dlopen: %s", dlerror()); return -1; }
    m->image = dlsym(m->handle, "dm_guest_image");
    if (!m->image) { snprintf(error, cap, "Missing dm_guest_image"); dm_android_unload(m); return -1; }
    uint8_t *end = dlsym(m->handle, "dm_guest_image_end");
    if (!end || end <= m->image || m->patch_off >= (size_t)(end - m->image)) {
        snprintf(error, cap, "Invalid image bounds"); dm_android_unload(m); return -1;
    }
    m->size = end - m->image;
    uint64_t *keys = (void *)(m->image + m->key_page_off);
    for (unsigned i = 0; i < DM_NSLOTS; ++i) keys[i] = dm_android_slot_offset(i);
    keys[DM_NSLOTS] = (uintptr_t)dm_android_context_entry;
    if (mprotect(keys, 0x4000, PROT_READ)) {
        snprintf(error, cap, "key page: %s", strerror(errno)); dm_android_unload(m); return -1;
    }
    return 0;
}

void dm_android_unload(dm_android_module *m) {
    if (m->handle) dlclose(m->handle);
    memset(m, 0, sizeof *m);
}

typedef struct { unsigned id; int ok; int64_t offset; } thread_test;
static void *test_thread(void *arg) {
    thread_test *t = arg;
    dm_android_context *c = dm_android_thread_init(NULL);
    c->slots[DM_K18] = t->id;
    t->offset = dm_android_slot_offset(DM_K18);
    uint64_t *slot = (void *)((uintptr_t)dm_android_context_get() + t->offset);
    for (unsigned i = 0; i < 10000; i++) {
        if (*slot != t->id) return NULL;
        sched_yield();
    }
    t->ok = 1;
    return NULL;
}

int dm_android_runtime_selftest(char *report, size_t cap) {
    pthread_t threads[4]; thread_test tests[4] = {0};
    unsigned created = 0;
    for (unsigned i = 0; i < 4; ++i) {
        tests[i].id = i + 1;
        if (pthread_create(&threads[i], NULL, test_thread, &tests[i])) break;
        created++;
    }
    for (unsigned i = 0; i < created; ++i) pthread_join(threads[i], NULL);
    int ok = created == 4;
    for (unsigned i = 0; i < created; ++i) ok &= tests[i].ok && tests[i].offset == tests[0].offset;
    snprintf(report, cap, "Thread context: %s (4 threads, 10000 iterations each; slot offset %lld; page %ld)",
             ok ? "PASS" : "FAIL", (long long)tests[0].offset, sysconf(_SC_PAGESIZE));
    return ok ? 0 : -1;
}

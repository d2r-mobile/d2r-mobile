// 无游戏运行时回归：线程隔离/析构、稳定堆、事件与公开 SVC 寄存器约定。
// Game-free runtime regression: thread isolation / destruction, stable heap, events and public SVC register contracts.
#include "../runtime/host.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); abort(); } } while (0)
#define UNIT (2u * 1024u * 1024u)
static const dm_gate_table empty_table = {0};
typedef struct { dm_host *host; dm_event *ready, *release; uintptr_t tls, tlr; uint64_t marker; } worker_args;

static void *thread_worker(void *argument) {
    worker_args *a = argument;
    CHECK(!dm_guest_attach(a->host, &empty_table));
    dm_guest_context *context = dm_guest_current(); CHECK(context);
    a->tls = context->slots[DM_SLOT_TLS]; a->tlr = context->slots[DM_SLOT_TLR];
    CHECK(*(uintptr_t *)(a->tlr + 0x1f8) == a->tlr + 0x200);
    for (unsigned i = 0; i < 8192; i++) CHECK(((uint8_t *)a->tls)[i] == 0);
    CHECK(!dm_event_signal(a->ready)); CHECK(!dm_event_wait(a->release, -1));
    for (unsigned i = 0; i < 1000; i++) {
        context->slots[DM_SLOT_X18] = a->marker + i;
        uint64_t regs[31] = {[1] = 0x12345678};
        CHECK(dm_host_svc(a->host, 0x1e, regs) > 0);
        CHECK(regs[1] == 0x12345678);
        CHECK(dm_guest_current() == context && context->slots[DM_SLOT_X18] == a->marker + i);
    }
    context->slots[DM_SLOT_TLS] = 0x1234;
    context->slots[DM_SLOT_TLR] = 0x5678;
    return NULL;
}

static void isolates_contexts_when_threads_run(void) {
    dm_host *host = dm_host_create(UNIT * 4); CHECK(host);
    CHECK(!dm_guest_attach(host, &empty_table));
    CHECK(dm_guest_attach(host, &empty_table) == EALREADY);
    dm_guest_context *main_context = dm_guest_current();
    main_context->slots[DM_SLOT_X18] = 0xcafebabe;
    dm_event *release = dm_event_create(0); CHECK(release);
    worker_args args[4]; pthread_t threads[4];
    for (unsigned i = 0; i < 4; i++) {
        args[i] = (worker_args){.host = host, .ready = dm_event_create(1), .release = release, .marker = 10000u * i};
        CHECK(args[i].ready); CHECK(!pthread_create(&threads[i], NULL, thread_worker, &args[i]));
    }
    for (unsigned i = 0; i < 4; i++) {
        CHECK(!dm_event_wait(args[i].ready, -1));
        CHECK(args[i].tls != main_context->slots[DM_SLOT_TLS]);
        for (unsigned j = 0; j < i; j++) CHECK(args[i].tls != args[j].tls && args[i].tlr != args[j].tlr);
    }
    CHECK(!dm_event_signal(release));
    for (unsigned i = 0; i < 4; i++) { CHECK(!pthread_join(threads[i], NULL)); CHECK(!dm_event_destroy(args[i].ready)); }
    CHECK(main_context->slots[DM_SLOT_X18] == 0xcafebabe);
    CHECK(dm_host_destroy(host) == EBUSY);
    CHECK(!dm_guest_detach()); CHECK(!dm_guest_current());
    CHECK(dm_guest_detach() == ENOENT);
    CHECK(!dm_host_destroy(host)); CHECK(!dm_event_destroy(release));
}

static void resizes_heap_when_svc_requested(void) {
    dm_host *host = dm_host_create(UNIT * 4); CHECK(host);
    uint64_t regs[31] = {[1] = UNIT * 2};
    CHECK(dm_host_svc(host, 1, regs) == 0);
    uint8_t *base = (uint8_t *)(uintptr_t)regs[1];
    base[0] = 17; base[UNIT] = 29;
    regs[1] = UNIT; CHECK(!dm_host_svc(host, 1, regs)); CHECK(regs[1] == (uintptr_t)base);
    regs[1] = UNIT * 2; CHECK(!dm_host_svc(host, 1, regs));
    CHECK(regs[1] == (uintptr_t)base && base[0] == 17 && base[UNIT] == 0);
    regs[1] = 1; CHECK(dm_host_svc(host, 1, regs) == DM_SVC_INVALID_SIZE);
    regs[1] = UNIT * 5; CHECK(dm_host_svc(host, 1, regs) == DM_SVC_OUT_OF_MEMORY);
    CHECK(base[0] == 17);
    uint64_t address_wait[31]={(uintptr_t)(base+16),2,0,0};
    CHECK(dm_host_svc(host,0x34,address_wait)==0xea01);
    CHECK(dm_host_svc(host, 0xffff, regs) == DM_SVC_NOT_IMPLEMENTED);
    regs[0] = 1; regs[1] = 8;
    CHECK(dm_host_svc(host, 0x27, regs) == DM_SVC_INVALID_ADDRESS);
    regs[0] = 0; CHECK(!dm_host_svc(host, 0x0b, regs));
    CHECK(!dm_host_destroy(host));
    CHECK(!dm_host_create(UNIT + 1) && errno == EINVAL);
}

typedef struct { dm_event *request, *reply; } pingpong;
static void *event_worker(void *argument) {
    pingpong *p = argument;
    for (unsigned i = 0; i < 1000; i++) { CHECK(!dm_event_wait(p->request, -1)); CHECK(!dm_event_signal(p->reply)); }
    return NULL;
}

static void synchronizes_events_when_signaled(void) {
    dm_event *manual = dm_event_create(0); CHECK(manual);
    CHECK(dm_event_wait(manual, 0) == ETIMEDOUT);
    CHECK(dm_event_wait(manual, 1) == ETIMEDOUT);
    CHECK(!dm_event_signal(manual));
    CHECK(!dm_event_wait(manual, 0) && !dm_event_wait(manual, 0));
    CHECK(!dm_event_clear(manual)); CHECK(dm_event_wait(manual, 0) == ETIMEDOUT);
    CHECK(dm_event_wait(manual, -2) == EINVAL); CHECK(!dm_event_destroy(manual));
    pingpong p = {.request = dm_event_create(1), .reply = dm_event_create(1)};
    CHECK(p.request && p.reply);
    pthread_t thread; CHECK(!pthread_create(&thread, NULL, event_worker, &p));
    for (unsigned i = 0; i < 1000; i++) { CHECK(!dm_event_signal(p.request)); CHECK(!dm_event_wait(p.reply, -1)); }
    CHECK(!pthread_join(thread, NULL));
    CHECK(dm_event_wait(p.reply, 0) == ETIMEDOUT);
    CHECK(!dm_event_destroy(p.request) && !dm_event_destroy(p.reply));
}

int main(void) {
    isolates_contexts_when_threads_run(); resizes_heap_when_svc_requested(); synchronizes_events_when_signaled();
    puts("PASS: per-thread guest TLS / x18, automatic cleanup, heap resize / zeroing, tick ABI, events / timeouts");
    return 0;
}

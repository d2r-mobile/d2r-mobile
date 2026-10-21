// Real guest_thread_init and host_tls mirror: a pthread gate alone left Android SDK calls at pc=0.
#include "../native/runtime.h"
#include "../native/host_tls.h"
#include "../../host/thread.c"
#undef pthread_setspecific
#include <assert.h>

pthread_key_t g_key18, g_keytls, g_keytlr, g_keygate, g_keyres[2], g_keyaspect, g_keyaspectui;
static _Thread_local dm_android_context context;
dm_android_context *dm_android_context_get(void) { return &context; }
void dm_gate(void) {}
uint64_t dm_game_res(int i) { return ((uint64_t)720 << 32) | (1280 + i); }
uint64_t front_game_res(int i) { return dm_game_res(i); }

static void *check_thread(void *unused) {
    (void)unused;
    guest_thread_init();
    assert(context.slots[DM_KGATE] == (uintptr_t)dm_gate);
    assert(context.slots[DM_KTLS] == (uintptr_t)pthread_getspecific(g_keytls));
    assert(context.slots[DM_KTLR] == (uintptr_t)pthread_getspecific(g_keytlr));
    assert(context.slots[DM_KTLS] && context.slots[DM_KTLR]);
    assert(context.slots[DM_KRES0] == dm_game_res(0));
    assert(context.slots[DM_KRES1] == dm_game_res(1));
    return NULL;
}

int main(void) {
    pthread_key_t *dest[] = {&g_key18, &g_keytls, &g_keytlr, &g_keygate,
                            &g_keyres[0], &g_keyres[1], &g_keyaspect, &g_keyaspectui};
    pthread_key_t keys[8];
    for (unsigned i = 0; i < 8; ++i) {
        assert(pthread_key_create(dest[i], NULL) == 0);
        keys[i] = *dest[i];
    }
    dm_android_bind_keys(keys);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, check_thread, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    for (unsigned i = 0; i < 8; ++i) assert(pthread_key_delete(keys[i]) == 0);
    puts("PASS: actual guest thread publishes gate/TLS/TLR/resolution into Android context");
    return 0;
}

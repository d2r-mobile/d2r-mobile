#include "dm.h"

#include <stdint.h>
#include <string.h>
#include <pthread.h>

typedef struct {
    dm_module_identity modules[MAX_MOD];
    int count;
    int published;
    uint64_t generation;
} module_identity_state;

static module_identity_state g_identity;
static pthread_mutex_t g_identity_lock = PTHREAD_MUTEX_INITIALIZER;

static int valid_module(const module_t *module) {
    if (!module || module->name[0] == 0 || module->base == NULL || module->size == 0)
        return 0;
    if (memchr(module->name, 0, sizeof(module->name)) == NULL) return 0;
    uint64_t base = (uint64_t)(uintptr_t)module->base;
    return base <= UINT64_MAX - (uint64_t)module->size;
}

void dm_module_identity_invalidate(void) {
    pthread_mutex_lock(&g_identity_lock);
    g_identity.published = 0;
    g_identity.count = 0;
    pthread_mutex_unlock(&g_identity_lock);
}

int dm_module_identity_publish(const module_t *modules, int count) {
    // A failed publication must leave no usable snapshot. Invalidate before
    // validation; publish only after every module has passed.
    dm_module_identity_invalidate();
    if (!modules || count <= 0 || count > MAX_MOD) return 0;

    dm_module_identity next[MAX_MOD];
    memset(next, 0, sizeof(next));
    for (int i = 0; i < count; ++i) {
        if (!valid_module(&modules[i])) return 0;
        strlcpy(next[i].name, modules[i].name, sizeof(next[i].name));
        next[i].base = (uint64_t)(uintptr_t)modules[i].base;
        next[i].size = (uint64_t)modules[i].size;
        for (int j = 0; j < i; ++j)
            if (!strcmp(next[j].name, next[i].name)) return 0;
    }

    pthread_mutex_lock(&g_identity_lock);
    if (g_identity.generation == UINT64_MAX) g_identity.generation = 0;
    uint64_t generation = ++g_identity.generation;
    if (generation == 0) generation = ++g_identity.generation;
    memcpy(g_identity.modules, next, sizeof(dm_module_identity) * (size_t)count);
    g_identity.count = count;
    g_identity.published = 1;
    for (int i = 0; i < count; ++i) g_identity.modules[i].generation = generation;
    pthread_mutex_unlock(&g_identity_lock);
    return 1;
}

int dm_module_identity_get(const char *name, dm_module_identity *out) {
    if (!name || !name[0] || !out) return 0;
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&g_identity_lock);
    int found = 0;
    if (g_identity.published) {
        for (int i = 0; i < g_identity.count; ++i) {
            if (!strcmp(g_identity.modules[i].name, name)) {
                *out = g_identity.modules[i];
                found = out->generation != 0 && out->base != 0 && out->size != 0 &&
                        out->base <= UINT64_MAX - out->size;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_identity_lock);
    if (!found) memset(out, 0, sizeof(*out));
    return found;
}

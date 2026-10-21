// Android host bridge for the remaining desktop front-end contracts.
// These are real adapters, not empty link stubs: the pacing helper is the
// shared nvn_pace_hz formula, display refresh comes from the current Java
// display capability, and exit unpause clears the existing guest pause bytes.
#include "../../host/dm.h"
#include "../../gfx/nvn/nvn_pace.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int dm_android_display_hz(void) __attribute__((weak));

// hle_nvn.c registers its actual providers in this pair from its constructor.
// Define the storage it declared; do not synthesize timing values.
uint64_t (*g_ub_sub_cpu_ns)(void);
uint64_t (*g_ub_sub_cyc)(void);

double dm_pace_hz(double screen_hz, double floor_hz) {
    return nvn_pace_hz(screen_hz, floor_hz);
}

double front_display_refresh(char *name, size_t n) {
    int hz = dm_android_display_hz ? dm_android_display_hz() : 0;
    if (name && n) snprintf(name, n, "%s", dm_android_display_hz ? "Android display" : "unknown (no display)");
    return hz > 0 ? hz : 0;
}

// Same contract as the Mac front end: release the original game's pause UI
// bytes only for the imminent exit path, returning whether it was paused.
int dm_unpause_for_exit(void) {
    uint8_t *b = NULL;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base;
    if (!b || !dm_ingame_now()) return 0;
    volatile uint8_t *f = b + 0x2d5ff51, *g = b + 0x2d5ff67;
    int was = *f || *g;
    *f = 0; *g = 0;
    return was;
}

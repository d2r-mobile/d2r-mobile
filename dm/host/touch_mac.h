#pragma once

#include <stdint.h>

int dm_touch_host_enabled(void);
void dm_touch_host_game_start(uint64_t now);
void dm_touch_host_step(uint64_t now, uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);

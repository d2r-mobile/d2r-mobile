// Synthetic native-art fixture. It uses the production implementation.c.inc
// (the same mapping code as generated dm_touch_native_art.c) with made-up IDs;
// no game data or private bitmap is present.
#include "../../ios/Sources/dm_touch_native_art.h"
#include "../../host/dm_input.h"
#include <stddef.h>
#include <stdint.h>

const dm_ta_atlas dm_ta_atlases[DM_TA_ATLASES] = {
    {"synthetic0", 408, 65, 65, 68, 6},
    {"synthetic1", 408, 65, 65, 68, 6},
    {"synthetic2", 0, 0, 0, 0, 0},
    {"synthetic3", 0, 0, 0, 0, 0},
    {"synthetic4", 0, 0, 0, 0, 0},
    {"synthetic5", 0, 0, 0, 0, 0},
    {"synthetic6", 0, 0, 0, 0, 0},
    {"synthetic7", 0, 0, 0, 0, 0},
    {"synthetic8", 0, 0, 0, 0, 0},
};
const char dm_ta_manifest_sha[] = "";
const char dm_ta_geometry_sha[] = "";
const char dm_ta_binding_sha[] = "";

static const struct { uint32_t id; int atlas, frame; } rows[] = {
    {100, 0, 2},
    {101, 1, 4},
    {102, 1, 4},
    {103, 0, 99}, // deliberately outside the atlas: must be unknown
};

#include "../../ios/tools/native_art/implementation.c.inc"

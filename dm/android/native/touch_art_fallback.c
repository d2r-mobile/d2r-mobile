// Default build without the player's generated native art: every bound skill
// is explicitly unknown. A strong generated dm_touch_native_art.c (present only
// after the user runs the local generator) replaces these weak definitions.
#include "../../ios/Sources/dm_touch_native_art.h"

__attribute__((weak)) const dm_ta_atlas dm_ta_atlases[DM_TA_ATLASES] = {{0}};

__attribute__((weak)) dm_ta_glyph dm_ta_current_set(const dm_tg_snapshot *s,
                                                    uint64_t button,
                                                    uint64_t now, int set) {
    (void)s; (void)button; (void)now; (void)set;
    return (dm_ta_glyph){DM_TA_UNKNOWN, -1, -1, -1, -1, -1, UINT32_MAX,
                         "native_art_not_generated"};
}

__attribute__((weak)) int dm_ta_frame_at(int atlas, int frame, dm_ta_frame *out) {
    (void)atlas; (void)frame; (void)out;
    return 0;
}

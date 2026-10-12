#pragma once
#include "dm_touch_game_state.h"
enum { DM_TA_UNKNOWN=0, DM_TA_EMPTY=1, DM_TA_GLYPH=2, DM_TA_ATLASES=9 };
typedef struct { const char *asset; int width,height,frame_width,pitch,count; } dm_ta_atlas;
typedef struct { int x,y,width,height; } dm_ta_frame;
typedef struct { int status,atlas,normal_frame,pressed_frame,slot,effective_slot; uint32_t raw_skill_id; const char *reason; } dm_ta_glyph;
extern const dm_ta_atlas dm_ta_atlases[DM_TA_ATLASES];
extern const char dm_ta_manifest_sha[],dm_ta_geometry_sha[],dm_ta_binding_sha[];
dm_ta_glyph dm_ta_skill(uint32_t raw_skill_id);
dm_ta_glyph dm_ta_current(const dm_tg_snapshot *s,uint64_t sdk_button,uint64_t host_now_ns);
/* 同上，skill_set=1 取第二套（alt 槽，原版按住 ZL） | Same as above; skill_set=1 takes the second set (alt slots, hold ZL in the original game) */
dm_ta_glyph dm_ta_current_set(const dm_tg_snapshot *s,uint64_t sdk_button,uint64_t host_now_ns,int skill_set);
int dm_ta_frame_at(int atlas,int frame,dm_ta_frame *out);

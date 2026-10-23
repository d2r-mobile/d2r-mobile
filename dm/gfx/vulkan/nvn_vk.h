#pragma once
#include "engine/xbg_game.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct dm_vk_config {uint32_t size;xbg_game_config game;VkBool32 sampler_mirror_clamp_to_edge;int display_hz;int (*has_window)(void *userdata);} dm_vk_config;
/* Call before dm_main. Device/queue/window owned by frontend; Vulkan use serialized.
 * shader_directory must contain genuine .xgs exported from matching control blobs. */
int dm_vk_initialize(const dm_vk_config *config,char *error,size_t bytes);
#ifdef __cplusplus
}
#endif

#pragma once
#include "xbg_game.h"
#include "xbg_draw_plan.h"
#include "xbg_vertex.h"
#include "xbg_quad.h"
#include "xbg_stages.h"
#ifdef __cplusplus
#define _Static_assert static_assert
extern "C" {
#endif
#include "../../nvn/nvn_obj.h"
void xbg_legacy_register_texture(int id,const tex_t *t,const view_t *v);
void xbg_legacy_register_sampler(int id,const samp_par *s);
void xbg_logf(const char *format,...);
__attribute__((noreturn)) void xbg_fatalf(const char *format,...);
int xbg_backend_failed(void);
int xbg_shader_control_key(const void *control_0x880,char out_hex17[17]);
int xbg_device_integer(int property,int *value);
void xbg_legacy_pool_add(void *memory,size_t bytes);
void xbg_legacy_pool_del(void *memory);
void xbg_register_texture(const dpool_t *pool,int id,const tex_t *texture,const view_t *view);
void xbg_register_sampler(const dpool_t *pool,int id,const samp_par *sampler);
void xbg_legacy_tex_release(tex_t *texture);
void xbg_legacy_prog_release(prog_t *program);
void xbg_texture_invalidate(const tex_t *texture);
void xbg_legacy_read_texels(const tex_t *texture,unsigned level,unsigned layer,
    const NVNcopyRegion *region,uint8_t *dst,size_t row,size_t image);
void *xbg_host_command_memory(cmd_t *command,size_t bytes);
void xbg_release_command_handle(uint64_t handle);
typedef size_t (*xbg_command_available_fn)(void *userdata,const void *address);
int xbg_command_clone(int count,const uint64_t *handles,void *output,size_t capacity,
    xbg_command_available_fn available,void *userdata,size_t *bytes);
void xbg_copy_commands(cmd_t *command,int count,const uint64_t *handles);
void xbg_record_command(cmd_t *command,const void *record,uint32_t bytes);
void xbg_invoke_command_callback(cmd_t *command,size_t bytes);
void xbg_submit(int count,const uint64_t *handles);
void xbg_present_texture(const win_t *window,const tex_t *texture);
uint64_t xbg_legacy_last_fence(void);
int xbg_legacy_wait(uint64_t fence,uint64_t timeout);
void xbg_legacy_finish(void);
/* 长帧日志的读数（xbg_game.cpp 的 hud）：自启动起帧里同步建管线、载入着色器、整份上传贴图的次数与 ns；每个 HLE 帧的 GPU 时间
 * （不知道时 −1）；xbg_game_set_frame 给出此后提交所属的 HLE 帧。
 * Readings for the long-frame log (xbg_game.cpp's hud): counts and ns since start of the pipelines created, shaders loaded and whole
 * textures uploaded synchronously in a frame; the GPU time of each HLE frame (−1 when unknown); xbg_game_set_frame names the HLE frame
 * that later submissions belong to. */
typedef struct xbg_frame_counters { uint64_t pipelines,pipeline_ns,shader_loads,shader_load_ns,uploads,reuploads,upload_bytes,upload_ns; } xbg_frame_counters;
void xbg_game_frame_counters(xbg_frame_counters *out);
void xbg_game_set_frame(uint64_t frame);
double xbg_game_gpu_ms(uint64_t frame);
/* Extension records retain the original command layout header. */
typedef struct xbg_indirect { uint64_t arguments,count_address,index_address; int32_t primitive,index_type,max_count,stride; } xbg_indirect;
typedef struct xbg_wait_event { uint64_t address; uint32_t mode,value; } xbg_wait_event;
typedef struct xbg_typed_clear { p_clear_color color; int32_t scalar; } xbg_typed_clear;
typedef struct xbg_depth_bounds { uint32_t enable; float minimum,maximum; } xbg_depth_bounds;
#ifdef __cplusplus
}
#undef _Static_assert
#endif

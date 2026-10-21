/* Synthetic boundary fixture for the production UI-ghost capture/replay code.
 * It links the real nvn_vulkan_ui_ghost.inc against a minimal fake backend;
 * no game frame, device or artwork is used. */
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP 5
#define VK_IMAGE_ASPECT_COLOR_BIT 1
#define VK_NULL_HANDLE ((void *)0)
typedef struct { void *buffer; void *memory; void *mapped; uint64_t size; struct { int refs; } *owner; } vk_buffer;
typedef struct { vk_buffer source; } post_op;
typedef struct { uint64_t serial; } tex_t;
typedef struct { int aspect; uint64_t stamp; } image_entry;
typedef struct { int valid; tex_t texture; int view; uint64_t revision; } texture_descriptor;
typedef struct { int minf, magf, ws, wt, wr; float border[4]; int cmp_mode; } samp_par;
typedef void *VkSampler;
typedef struct { int valid; samp_par parameters; VkSampler sampler; uint64_t revision; } sampler_descriptor;
typedef struct { void *buffer; uint64_t offset, range; } VkDescriptorBufferInfo;
typedef struct { uint64_t address, size; } NVNbufferRange;
typedef struct { int fmt, stream, offset; } va_st;
typedef struct { int stride, divisor; } vs_st;
typedef struct { int kind, slot, count, flags; uint32_t type; uint32_t handle; } shader_binding;
typedef struct { int storage_slot, uniform_pointer; } shader_binding_plan;
typedef struct { int stage; unsigned count; shader_binding *bindings; shader_binding_plan *binding_plan; uint32_t attributes[32]; } shader_entry;
typedef struct { const shader_entry *program; unsigned nstage; } prog_t;
typedef struct { unsigned n; int *color[8]; int depth; } p_set_rt;
typedef struct { uint64_t addr, size; } p_bind_buf;
typedef struct { int x, y, w, h; } p_scissor;
typedef struct { float v[16][4]; } p_viewports;
typedef struct { int dt, dw, st; } ds_st;
typedef struct { int first, count, prim; } p_draw;
typedef struct { int unused; } p_draw_idx;
typedef struct { int width, height; } ghost_frame_key;
typedef struct { ghost_frame_key key; } graphics_frame_entry;
typedef enum { FORWARD_NONE, FORWARD_FULL, FORWARD_PARTIAL, FORWARD_UNRECORDED, FORWARD_ALIGNMENT, FORWARD_BOUNDS, FORWARD_HOST_EFFECT } forward_result;
typedef struct { forward_result result; void *slice; } forward_slice;

static int fake_gpu_producer;
static int fake_draw_calls;
static uint32_t fake_replayed_texture;
static int fake_post_count;
static post_op fake_posts[8];
static int fake_live_buffers;
static unsigned fake_descriptor_revision, fake_sampler_revision;
static VkSampler fake_sampler = (void *)1;
static pthread_mutex_t descriptor_lock = PTHREAD_MUTEX_INITIALIZER;
static int sampler_capacity = 1;
static texture_descriptor texture_descriptors[1];
static sampler_descriptor sampler_descriptors[1];
static image_entry fake_image = {.aspect = VK_IMAGE_ASPECT_COLOR_BIT, .stamp = 1};
static shader_entry fake_vs = {.stage = 0}, fake_fs = {.stage = 1};
static shader_binding fake_fs_binding = {.kind = 3, .slot = 0, .count = 1, .handle = 0};
static prog_t fake_program = {.program = NULL, .nstage = 2};
static p_set_rt fake_rt;
static graphics_frame_entry fake_frame = {.key = {640, 360}};
static graphics_frame_entry *graphics_active_frame = &fake_frame;
static unsigned render_passes = 1, presents = 1;
static uint64_t fake_ubo_addr = 0x1000, fake_vbuf_addr = 0x2000;

typedef struct {
    const prog_t *program;
    p_set_rt rt;
    p_bind_buf ubo[6][16], ssbo[6][16];
    uint64_t textures[6][128], samplers[6][128];
    va_st attributes[16];
    vs_st streams[16];
    NVNbufferRange vertices[16];
    ds_st depth;
    p_scissor scissor;
    p_viewports viewports;
} fake_state;
static fake_state state;

static vk_buffer buffer_create(uint64_t size) { vk_buffer b = {.buffer = malloc(1), .memory = malloc(1), .mapped = calloc(1, size), .size = size}; b.owner = calloc(1, sizeof *b.owner); b.owner->refs = 1; fake_live_buffers++; return b; }
static vk_buffer buffer_borrow(vk_buffer b) { b.owner->refs++; return b; }
static void buffer_destroy(vk_buffer *b) { if (!b->buffer) return; if (--b->owner->refs) { memset(b, 0, sizeof *b); return; } free(b->buffer); free(b->memory); free(b->mapped); free(b->owner); fake_live_buffers--; memset(b, 0, sizeof *b); }
static post_op *post_new(int kind) { (void)kind; return &fake_posts[fake_post_count++ % 8]; }
static void fake_complete_posts(void) { for (int i = 0; i < fake_post_count; i++) buffer_destroy(&fake_posts[i].source); fake_post_count = 0; }
static int snapshot_read(uintptr_t address, void *out, uint64_t size) {
    memset(out, 0, size);
    if (address == fake_ubo_addr) {
        float *m = out; m[0] = 1; m[5] = 1;
    } else if (address == fake_vbuf_addr) {
        float *p = out;
        const float q[20] = {-1.0f,-1.0f,0,1,1, 0.0f,-1.0f,0,1,1, -1.0f,0.0f,0,1,1, 0.0f,0.0f,0,1,1};
        memcpy(p, q, sizeof q);
    }
    return 0;
}
static forward_result forward_find(uintptr_t a, uint64_t s, uint64_t al, void *m, forward_slice *out) {
    (void)a; (void)s; (void)al; (void)m;
    if (fake_gpu_producer) { out->result = FORWARD_FULL; return FORWARD_FULL; }
    out->result = FORWARD_NONE; return FORWARD_NONE;
}
static shader_entry *program_shader(const prog_t *p, unsigned i) { (void)p; return i ? &fake_fs : &fake_vs; }
static uint32_t shader_handle(const shader_binding *b, unsigned stage, unsigned element) { (void)element; return state.textures[stage][b->slot]; }
static texture_descriptor descriptor_texture(int id) { (void)id; return texture_descriptors[0]; }
static image_entry *image_find(uint64_t serial) { return serial == texture_descriptors[0].texture.serial ? &fake_image : NULL; }
static int graphics_image_is_attachment(const image_entry *i) { (void)i; return 0; }
static VkSampler sampler_get_for_type(int id, uint32_t type) { (void)id; (void)type; return fake_sampler; }
static int graphics_shader_writes(const shader_entry *s) { (void)s; return 0; }
static void draw_graphics(const p_draw *d, const p_draw_idx *i) { (void)d; (void)i; fake_replayed_texture = state.textures[1][0]; fake_draw_calls++; state.viewports.v[0][0] = 99; state.scissor.x = 99; }

#include "../native/nvn_vulkan_ui_ghost.inc"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)

int main(void) {
    VkDescriptorBufferInfo outside = {0};
    CHECK(!ghost_buffer_override(fake_ubo_addr, 64, &outside));
    const float grid[4] = {0, 0, .5f, .5f};
    const float quad[20] = {-0.5f,-0.5f,0,1,1, .5f,-0.5f,0,1,1, -0.5f,.5f,0,1,1, .5f,.5f,0,1,1};
    const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float margin_rect[4];
    CHECK(nvn_vk_ui_ghost_rect(quad, ident, (float[4]){64,36,512,288}, 640, 360, 1, margin_rect));
    CHECK(fabsf(margin_rect[0] - .3f) < .001f && fabsf(margin_rect[1] - .3f) < .001f &&
          fabsf(margin_rect[2] - .7f) < .001f && fabsf(margin_rect[3] - .7f) < .001f);
    (void)quad;
    fake_rt.n = 1; fake_rt.color[0] = (int *)1; fake_rt.depth = 0;
    state.program = &fake_program; state.rt = fake_rt;
    state.ubo[0][0] = (p_bind_buf){fake_ubo_addr, 64};
    state.vertices[0] = (NVNbufferRange){fake_vbuf_addr, 80};
    state.streams[0].stride = 20;
    state.viewports.v[0][0] = 0; state.viewports.v[0][1] = 0;
    state.viewports.v[0][2] = 640; state.viewports.v[0][3] = 360;
    fake_vs.attributes[0] = 1; fake_vs.attributes[1] = 1;
    fake_fs.count = 1; fake_fs.bindings = &fake_fs_binding;
    texture_descriptors[0] = (texture_descriptor){.valid = 1, .texture = {.serial = 7}, .revision = ++fake_descriptor_revision};
    sampler_descriptors[0] = (sampler_descriptor){.valid = 1, .sampler = fake_sampler, .revision = ++fake_sampler_revision};
    p_draw draw = {.first = 0, .count = 4, .prim = NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP};

    // Outside the shelf grid: no candidate and no replay.
    dm_vk_ui_ghost_request(1, .75f, .75f, grid, .5f, .5f, 0, NULL, 0);
    ghost_note(&draw, NULL);
    CHECK(ghost.count == 0);

    // Valid shop drag: capture, then replay at the requested destination.
    dm_vk_ui_ghost_request(1, .25f, .25f, grid, .75f, .75f, 0, NULL, 0);
    ghost_note(&draw, NULL);
    CHECK(ghost.count == 1);
    fake_draw_calls = 0;
    float saved_vx = state.viewports.v[0][0];
    int saved_sx = state.scissor.x;
    ghost_flush();
    CHECK(fake_draw_calls == 1);
    CHECK(state.viewports.v[0][0] == saved_vx && state.scissor.x == saved_sx);
    CHECK(fake_post_count > 0 && fake_live_buffers > 0); // snapshots retained through GPU submission
    fake_complete_posts();
    CHECK(fake_live_buffers == 0); // reclaimed only after the recorded post completes
    CHECK(ghost_found_valid == 1);
    float r[4];
    CHECK(mtl_ui_ghost_rect(r) == 1);

    // The first matching layer is the repeated slot tile, not the item icon.
    // Count even eligible quads outside the dragged slot before selecting.
    dm_vk_ui_ghost_request(1, .25f, .25f, grid, .75f, .75f, 0, NULL, 0);
    state.textures[1][0] = 1;
    ghost_note(&draw, NULL);
    state.viewports.v[0][0] = 640;
    for (unsigned i = 0; i < 8; i++) ghost_note(&draw, NULL);
    state.viewports.v[0][0] = 0;
    state.textures[1][0] = 2;
    ghost_note(&draw, NULL);
    CHECK(ghost.count == 2);
    fake_draw_calls = 0;
    ghost_flush();
    CHECK(fake_draw_calls == 1 && fake_replayed_texture == 2);
    fake_complete_posts();
    CHECK(fake_live_buffers == 0);

    // Guest UBO slots are reflected as slot+3; storage slots come from the
    // backend binding plan, not the reflection ordinal.
    ghost_candidate_t captured = {0};
    shader_binding bindings[] = {{.kind = 1, .slot = 4, .count = 1},
                                 {.kind = 2, .slot = 12, .count = 1}};
    shader_binding_plan plans[] = {{0}, {.storage_slot = 5}};
    shader_entry shader = {.stage = 0, .count = 2, .bindings = bindings, .binding_plan = plans};
    state.ubo[0][1] = (p_bind_buf){0x3000, 64};
    state.ssbo[0][5] = (p_bind_buf){0x4000, 64};
    CHECK(ghost_capture_shader(&captured, &shader));
    CHECK(captured.nb == 2 && captured.buffers[0].addr == 0x3000 && captured.buffers[1].addr == 0x4000);
    for (unsigned i = 0; i < captured.nb; i++) buffer_destroy(&captured.buffers[i].buffer);

    // Physical cancellation must invalidate before any later capture/flush.
    mtl_ui_ghost(0, 0, 0, NULL, 0, 0);
    CHECK(ghost_found_valid == 0);
    fake_draw_calls = 0;
    ghost_flush();
    CHECK(fake_draw_calls == 0);

    // Changed request generation cannot reuse the old candidate.
    _Atomic uint64_t live = 1;
    dm_vk_ui_ghost_request(1, .25f, .25f, grid, .75f, .75f, 0, &live, 1);
    ghost_note(&draw, NULL);
    CHECK(ghost.count == 1);
    atomic_store(&live, 2);
    fake_draw_calls = 0;
    ghost_flush();
    CHECK(fake_draw_calls == 0);

    // Texture identity mutation rejects the immutable candidate.
    atomic_store(&live, 3);
    dm_vk_ui_ghost_request(1, .25f, .25f, grid, .75f, .75f, 0, &live, 3);
    ghost_note(&draw, NULL);
    CHECK(ghost.count == 1);
    texture_descriptors[0].revision = ++fake_descriptor_revision;
    fake_draw_calls = 0;
    ghost_flush();
    CHECK(fake_draw_calls == 0);

    // Pending GPU producer refuses capture instead of replaying stale guest data.
    texture_descriptors[0].revision = ++fake_descriptor_revision;
    atomic_store(&live, 4);
    dm_vk_ui_ghost_request(1, .25f, .25f, grid, .75f, .75f, 0, &live, 4);
    fake_gpu_producer = 1;
    ghost_note(&draw, NULL);
    CHECK(ghost.count == 0);

    if (failures) return 1;
    puts("PASS ui ghost: outside-drop, capture, replay, cancel, generation, texture mutation, GPU producer");
    return 0;
}

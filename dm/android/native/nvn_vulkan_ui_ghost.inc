/* Original-draw replay for the shop drag icon.
 * MIT-licensed Android compatibility implementation.
 * (MIT), reduced to this backend's existing state/descriptor model.
 * The overlay owns immutable CPU snapshots and never screen-captures or
 * substitutes artwork. A request is invalid when its live contact token,
 * pass, epoch, texture identity or sampler identity changes. */
#include "../../gfx/nvn/nvn_vk_ui_ghost.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    int on, dim;
    float origin[2], grid[4], dest[2];
    const _Atomic uint64_t *live;
    uint64_t token, serial;
} ghost_request_t;

typedef struct { uintptr_t addr; uint64_t size; vk_buffer buffer; } ghost_buffer_t;
typedef struct {
    unsigned id, sid;
    uint32_t type;
    uint64_t serial, descriptor_revision, image_stamp, sampler_revision;
    VkSampler sampler;
    samp_par parameters;
} ghost_texture_t;
typedef struct {
    __typeof__(state) state;
    p_draw draw;
    float rect[4], area;
    uint32_t texture;
    ghost_buffer_t buffers[34];
    unsigned nb;
    ghost_texture_t textures[16];
    unsigned nt;
} ghost_candidate_t;

typedef struct {
    int busy, replaying;
    unsigned pass;
    uint64_t epoch, serial;
    ghost_candidate_t *candidate[4];
    unsigned count;
    float area;
    uint32_t texture[128], uses[128];
    unsigned nt;
} ghost_state_t;

static pthread_mutex_t ghost_lock = PTHREAD_MUTEX_INITIALIZER;
static ghost_request_t ghost_request;
static _Atomic int ghost_on;
static uint64_t ghost_serial;
static float ghost_found[4];
static int ghost_found_valid;
static uint64_t ghost_found_serial;
static ghost_state_t ghost;
static ghost_candidate_t *ghost_replaying;

static int ghost_request_read(ghost_request_t *out) {
    if (!atomic_load_explicit(&ghost_on, memory_order_acquire)) return 0;
    pthread_mutex_lock(&ghost_lock);
    *out = ghost_request;
    pthread_mutex_unlock(&ghost_lock);
    return out->on && (!out->live || atomic_load_explicit(out->live, memory_order_acquire) == out->token);
}

void dm_vk_ui_ghost_request(int on, float px, float py, const float grid[4],
                            float cx, float cy, int dim,
                            const _Atomic uint64_t *live, uint64_t token) {
    ghost_request_t r = {.on = on && grid != NULL, .dim = dim,
                         .origin = {px, py}, .dest = {cx, cy}, .live = live, .token = token};
    if (r.on) {
        memcpy(r.grid, grid, sizeof r.grid);
        for (unsigned i = 0; i < 4; i++) if (!isfinite(grid[i])) r.on = 0;
        if (!isfinite(px) || !isfinite(py) || !isfinite(cx) || !isfinite(cy) ||
            grid[2] <= 0 || grid[3] <= 0) r.on = 0;
    }
    pthread_mutex_lock(&ghost_lock);
    if (!r.on || !ghost_request.on || r.live != ghost_request.live ||
        r.token != ghost_request.token ||
        memcmp(r.origin, ghost_request.origin, sizeof r.origin) ||
        memcmp(r.grid, ghost_request.grid, sizeof r.grid))
        ++ghost_serial;
    r.serial = ghost_serial;
    ghost_request = r;
    if (!r.on) ghost_found_valid = 0;
    atomic_store_explicit(&ghost_on, r.on, memory_order_release);
    pthread_mutex_unlock(&ghost_lock);
}

void mtl_ui_ghost(int on, float px, float py, const float grid[4], float cx, float cy) {
    dm_vk_ui_ghost_request(on, px, py, grid, cx, cy, 0, NULL, 0);
}

int mtl_ui_ghost_rect(float r[4]) {
    ghost_request_t q;
    if (!r || !ghost_request_read(&q)) return 0;
    pthread_mutex_lock(&ghost_lock);
    int have = ghost_found_valid && ghost_found_serial == q.serial;
    if (have) memcpy(r, ghost_found, sizeof ghost_found);
    pthread_mutex_unlock(&ghost_lock);
    return have;
}

static void ghost_clear(void) {
    for (unsigned i = 0; i < ghost.count; i++) {
        ghost_candidate_t *c = ghost.candidate[i];
        for (unsigned j = 0; j < c->nb; j++) buffer_destroy(&c->buffers[j].buffer);
        free(c);
    }
    ghost.count = 0;
    ghost.area = 0;
    ghost.nt = 0;
}

static int ghost_gpu_producer(uintptr_t addr, uint64_t size) {
    forward_slice slice;
    forward_result result = forward_find(addr, size, 1, NULL, &slice);
    return result != FORWARD_NONE && result != FORWARD_ALIGNMENT;
}

static int ghost_snapshot(uintptr_t addr, uint64_t size, ghost_buffer_t *out) {
    if (!addr || !size || size > 65536 || ghost_gpu_producer(addr, size)) return 0;
    uint8_t *bytes = malloc(size);
    if (!bytes) return 0;
    if (snapshot_read(addr, bytes, size)) { free(bytes); return 0; }
    out->addr = addr; out->size = size; out->buffer = buffer_create(size);
    memcpy(out->buffer.mapped, bytes, size);
    free(bytes);
    return 1;
}

static int ghost_add_buffer(ghost_candidate_t *c, uintptr_t addr, uint64_t size) {
    if (!addr && !size) return 1;
    if (!addr || !size || size > 65536 || c->nb == 34) return 0;
    for (unsigned i = 0; i < c->nb; i++)
        if (c->buffers[i].addr == addr && c->buffers[i].size == size) return 1;
    ghost_buffer_t *b = &c->buffers[c->nb];
    if (!ghost_snapshot(addr, size, b)) return 0;
    c->nb++;
    return 1;
}

static int ghost_capture_shader(ghost_candidate_t *c, shader_entry *s) {
    for (unsigned i = 0; i < s->count; i++) {
        const shader_binding *b = &s->bindings[i];
        if (b->count != 1) return 0;
        if (b->kind == 1 && b->slot >= 3) {
            if (b->slot >= 19) return 0;
            const p_bind_buf *u = &state.ubo[s->stage][b->slot - 3];
            if (!ghost_add_buffer(c, (uintptr_t)u->addr, u->size)) return 0;
        } else if (b->kind == 2) {
            const shader_binding_plan *plan = &s->binding_plan[i];
            if (b->flags || plan->storage_slot < 0 || plan->storage_slot >= 16) return 0;
            const p_bind_buf *u = plan->uniform_pointer ? &state.ubo[s->stage][plan->storage_slot]
                                                       : &state.ssbo[s->stage][plan->storage_slot];
            if (!ghost_add_buffer(c, (uintptr_t)u->addr, u->size)) return 0;
        } else if (b->kind == 3 || b->kind == 4) {
            if (b->slot >= 128 || c->nt == 16) return 0;
            uint32_t handle = shader_handle(b, s->stage, 0);
            unsigned id = handle & 0xfffff, sid = (handle >> 20) & 0xfff;
            texture_descriptor d = descriptor_texture(id);
            if (!d.valid) return 0;
            image_entry *image = image_find(d.texture.serial);
            if (!image || image->aspect != VK_IMAGE_ASPECT_COLOR_BIT ||
                graphics_image_is_attachment(image)) return 0;
            pthread_mutex_lock(&descriptor_lock);
            sampler_descriptor sd = sid < (unsigned)sampler_capacity ? sampler_descriptors[sid] : (sampler_descriptor){0};
            pthread_mutex_unlock(&descriptor_lock);
            VkSampler sampler = sampler_get_for_type(sid, b->type);
            if (!sampler || !sd.valid) return 0;
            c->textures[c->nt++] = (ghost_texture_t){
                id, sid, b->type, d.texture.serial, d.revision, image->stamp,
                sd.revision, sampler, sd.parameters};
        } else if (b->kind != 0 && b->kind != 1 && b->kind != 7 && b->kind != 8) {
            return 0;
        }
    }
    return 1;
}

static int ghost_textures_valid(const ghost_candidate_t *c) {
    for (unsigned i = 0; i < c->nt; i++) {
        const ghost_texture_t *v = &c->textures[i];
        texture_descriptor d = descriptor_texture(v->id);
        image_entry *image = d.valid ? image_find(d.texture.serial) : NULL;
        if (!d.valid || d.texture.serial != v->serial || d.revision != v->descriptor_revision ||
            !image || image->stamp != v->image_stamp) return 0;
        pthread_mutex_lock(&descriptor_lock);
        sampler_descriptor sd = v->sid < (unsigned)sampler_capacity ? sampler_descriptors[v->sid] : (sampler_descriptor){0};
        pthread_mutex_unlock(&descriptor_lock);
        if (!sd.valid || sd.revision != v->sampler_revision ||
            memcmp(&sd.parameters, &v->parameters, sizeof v->parameters)) return 0;
        if (sampler_get_for_type(v->sid, v->type) != v->sampler) return 0;
    }
    return 1;
}

static void ghost_note(const p_draw *draw, const p_draw_idx *indexed) {
    if (ghost.busy || !atomic_load_explicit(&ghost_on, memory_order_acquire) ||
        !draw || indexed || draw->first || draw->count != 4 ||
        draw->prim != NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP ||
        !state.program || state.rt.n != 1 || !state.rt.color[0] || state.rt.depth ||
        state.depth.dt || state.depth.dw || state.depth.st || state.streams[0].stride != 20 ||
        state.streams[0].divisor || state.vertices[0].size < 80 ||
        !state.vertices[0].address || !graphics_active_frame) return;
    shader_entry *vs = NULL, *fs = NULL;
    for (unsigned i = 0; i < state.program->nstage; i++) {
        shader_entry *s = program_shader(state.program, i);
        if (s->stage == 0) vs = s; else if (s->stage == 1) fs = s;
    }
    if (!vs || !fs || vs->attributes[0] == 0 || vs->attributes[1] == 0 ||
        graphics_shader_writes(vs) || graphics_shader_writes(fs)) return;
    ghost_request_t q;
    if (!ghost_request_read(&q)) return;
    if (ghost.pass != render_passes || ghost.epoch != presents || ghost.serial != q.serial) {
        ghost_clear(); ghost.pass = render_passes; ghost.epoch = presents; ghost.serial = q.serial;
    }
    uint32_t texture = 0;
    int sampled = 0;
    for (unsigned i = 0; i < fs->count; i++) {
        const shader_binding *b = &fs->bindings[i];
        if ((b->kind == 3 || b->kind == 4) && b->count == 1) {
            texture = shader_handle(b, fs->stage, 0); sampled = 1; break;
        }
    }
    if (!sampled) return;
    unsigned k = 0;
    while (k < ghost.nt && ghost.texture[k] != texture) k++;
    if (k == ghost.nt && ghost.nt < 128) {
        ghost.texture[k] = texture; ghost.uses[k] = 0; ghost.nt++;
    }
    if (k < ghost.nt && ghost.uses[k] != UINT32_MAX) ghost.uses[k]++;
    uint8_t vertices[80];
    float matrix[16], rect[4];
    if (state.ubo[0][0].size < 64 || snapshot_read((uintptr_t)state.ubo[0][0].addr, matrix, 64) ||
        snapshot_read((uintptr_t)state.vertices[0].address, vertices, 80)) return;
    float vp[4] = {state.viewports.v[0][0], state.viewports.v[0][1],
                   state.viewports.v[0][2], state.viewports.v[0][3]};
    if (!nvn_vk_ui_ghost_rect(vertices, matrix, vp,
                              graphics_active_frame->key.width, graphics_active_frame->key.height,
                              1, rect) ||
        !nvn_vk_ui_ghost_matches(rect, q.grid, q.origin[0], q.origin[1])) return;
    float area = (rect[2] - rect[0]) * (rect[3] - rect[1]);
    if (ghost.count && area > ghost.area * 1.03f) return;
    ghost_candidate_t *c = calloc(1, sizeof *c);
    if (!c) return;
    c->state = state; c->draw = *draw; c->area = area; c->texture = texture; memcpy(c->rect, rect, sizeof rect);
    if (!ghost_add_buffer(c, (uintptr_t)state.vertices[0].address, 80) ||
        !ghost_add_buffer(c, (uintptr_t)state.ubo[0][0].addr, state.ubo[0][0].size) ||
        !ghost_capture_shader(c, vs) || !ghost_capture_shader(c, fs)) {
        for (unsigned j = 0; j < c->nb; j++) buffer_destroy(&c->buffers[j].buffer);
        free(c); return;
    }
    // A smaller matching quad supersedes larger slot backgrounds. Keep the
    // per-pass texture counts, which distinguish repeated tiles from icons.
    if (ghost.count && area < ghost.area * .97f) {
        for (unsigned i = 0; i < ghost.count; i++) {
            ghost_candidate_t *old = ghost.candidate[i];
            for (unsigned j = 0; j < old->nb; j++) buffer_destroy(&old->buffers[j].buffer);
            free(old);
        }
        ghost.count = 0;
    }
    if (ghost.count == 4) {
        ghost_candidate_t *old = ghost.candidate[0];
        for (unsigned j = 0; j < old->nb; j++) buffer_destroy(&old->buffers[j].buffer);
        free(old); memmove(ghost.candidate, ghost.candidate + 1, 3 * sizeof ghost.candidate[0]); ghost.count = 3;
    }
    ghost.area = area; ghost.candidate[ghost.count++] = c;
}

static int ghost_buffer_override(uintptr_t addr, uint64_t size, VkDescriptorBufferInfo *out) {
    if (!ghost.busy || !ghost_replaying || !addr) return 0;
    for (unsigned i = 0; i < ghost_replaying->nb; i++) {
        ghost_buffer_t *b = &ghost_replaying->buffers[i];
        if (addr < b->addr || addr - b->addr > b->size || size > b->size - (addr - b->addr)) continue;
        *out = (VkDescriptorBufferInfo){b->buffer.buffer, addr - b->addr, size};
        return 1;
    }
    return 0;
}

static void ghost_flush(void) {
    if (ghost.busy || !ghost.count) return;
    ghost_request_t q;
    if (!ghost_request_read(&q) || q.serial != ghost.serial ||
        ghost.pass != render_passes || ghost.epoch != presents) { ghost_clear(); return; }
    int best = -1;
    unsigned fewest = UINT32_MAX;
    for (unsigned i = 0; i < ghost.count; i++) {
        unsigned uses = UINT32_MAX;
        for (unsigned k = 0; k < ghost.nt; k++)
            if (ghost.texture[k] == ghost.candidate[i]->texture) uses = ghost.uses[k];
        if (uses < fewest && ghost_textures_valid(ghost.candidate[i])) {
            best = i; fewest = uses;
        }
    }
    if (best < 0) { ghost_clear(); return; }
    ghost_candidate_t *c = ghost.candidate[best];
    __typeof__(state) saved = state;
    ghost.busy = 1; ghost_replaying = c;
    // Keep every consumed immutable snapshot alive until the recorded replay
    // reaches submission completion. buffer_destroy below only drops our
    // candidate reference; the post owns the final reference (one per buffer,
    // not one per descriptor binding).
    for (unsigned i = 0; i < c->nb; i++) post_new(3)->source = buffer_borrow(c->buffers[i].buffer);
    state = c->state;
    float width = graphics_active_frame->key.width, height = graphics_active_frame->key.height;
    state.scissor = (p_scissor){0, 0, (int)width, (int)height};
    state.viewports.v[0][0] += (q.dest[0] - (c->rect[0] + c->rect[2]) * .5f) * width;
    state.viewports.v[0][1] += (q.dest[1] - (c->rect[1] + c->rect[3]) * .5f) * height;
    draw_graphics(&c->draw, NULL);
    state = saved;
    ghost_replaying = NULL; ghost.busy = 0;
    pthread_mutex_lock(&ghost_lock);
    if (ghost_request.serial == q.serial && atomic_load(&ghost_on)) {
        memcpy(ghost_found, c->rect, sizeof ghost_found);
        ghost_found_serial = q.serial; ghost_found_valid = 1;
    }
    pthread_mutex_unlock(&ghost_lock);
    ghost_clear();
}

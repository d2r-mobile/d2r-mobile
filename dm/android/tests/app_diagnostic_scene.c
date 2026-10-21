// #117 自有诊断场景与绘制就绪的宿主测试：独立核对场景字面值、按文档几何复算 20 个检查点、
// 检查器负例，并用真实 app/lifecycle.c 覆盖暂停/停止/失败/迟到回调的绘制抑制。
// #117 host tests for the owned diagnostic scene and draw readiness: independently verify the scene literals,
// recompute the 20 probes from the documented geometry, cover checker negatives, and drive the real
// app/lifecycle.c for pause/stop/failure/late-callback draw suppression.
#include "../app/diagnostic_scene.h"
#include "../app/lifecycle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

// 测试自己的探针几何：文档规定四边形为 NDC ±0.75、UV 0..1、4×4 纹理按行紧排。
// The test's own probe geometry: the contract says NDC ±0.75 quad, UV 0..1 and a tight row-major 4x4 texture.
static void expected_probe(uint32_t tx, uint32_t ty, uint32_t width, uint32_t height,
                           uint32_t *x, uint32_t *y) {
    const double u = ((double)tx + 0.5) / 4.0;
    const double v = ((double)ty + 0.5) / 4.0;
    const double ndc_x = u * 1.5 - 0.75;   // 2 × 0.75 extent | 2 x 0.75 extent
    const double ndc_y = v * 1.5 - 0.75;
    *x = (uint32_t)((ndc_x + 1.0) * 0.5 * (double)width);
    *y = (uint32_t)((ndc_y + 1.0) * 0.5 * (double)height);
}

static void put_pixel(uint8_t *canvas, uint32_t width, uint32_t x, uint32_t y, const uint8_t rgba[4]) {
    memcpy(canvas + ((size_t)y * width + x) * 4u, rgba, 4);
}

// 用检查点值搭一张最小画布（只写探针位置，几何由文档固定）。| Build a minimal canvas holding only the probe values.
static void paint_probes(const dm_diag_scene *scene, uint8_t *canvas, uint32_t width, uint32_t height) {
    const uint8_t clear[4] = {0, 0, 0, 255};
    for (uint32_t ty = 0; ty < 4u; ty++)
        for (uint32_t tx = 0; tx < 4u; tx++) {
            uint32_t x, y;
            expected_probe(tx, ty, width, height, &x, &y);
            put_pixel(canvas, width, x, y, scene->texture + ((size_t)ty * 4u + tx) * 4u);
        }
    put_pixel(canvas, width, 0, 0, clear);
    put_pixel(canvas, width, width - 1u, 0, clear);
    put_pixel(canvas, width, 0, height - 1u, clear);
    put_pixel(canvas, width, width - 1u, height - 1u, clear);
}

typedef struct {
    int load_result, stop_result, pause_result, resume_result, acquire_result;
    int loads, stops, pauses, resumes, acquires, releases;
    int ready_during_loading, ready_during_starting;
    dm_app_lifecycle *lifecycle;
} fake_host;

static int fake_load(void *ctx) { fake_host *h = ctx; h->loads++; return h->load_result; }
static int fake_stop(void *ctx) { fake_host *h = ctx; h->stops++; return h->stop_result; }
static int fake_pause(void *ctx) { fake_host *h = ctx; h->pauses++; return h->pause_result; }
static int fake_resume(void *ctx) { fake_host *h = ctx; h->resumes++; return h->resume_result; }
static int fake_acquire(void *ctx, void *surface) {
    (void)surface;
    fake_host *h = ctx;
    h->acquires++;
    return h->acquire_result;
}
static void fake_release(void *ctx, void *surface) {
    (void)surface;
    fake_host *h = ctx;
    h->releases++;
}
static void observe(void *ctx, dm_app_state state) {
    fake_host *h = ctx;
    if (state == DM_APP_STATE_STARTING || state == DM_APP_STATE_LOADING) {
        int ready = h->lifecycle ? dm_diag_scene_ready(h->lifecycle, (void *)0x1000) : 0;
        if (state == DM_APP_STATE_STARTING) h->ready_during_starting = ready;
        else h->ready_during_loading = ready;
    }
}

static dm_app_lifecycle *make_lifecycle(fake_host *host) {
    dm_app_host_ops ops = {.ctx = host, .load = fake_load, .stop = fake_stop, .pause = fake_pause,
        .resume = fake_resume, .surface_acquire = fake_acquire, .surface_release = fake_release};
    return dm_app_lifecycle_create(&ops, observe, host);
}

static void scene_literals(void) {
    const dm_diag_scene *scene = dm_diag_scene_owned();
    CHECK(scene != NULL);
    if (!scene) return;
    CHECK(scene->texture_width == 4u && scene->texture_height == 4u);
    CHECK(scene->texture_size == 4u * 4u * 4u);
    CHECK(scene->vertex_count == 4u);
    CHECK(scene->index_count == 6u && scene->index_count % 3u == 0u);
    CHECK(scene->vertices != NULL && scene->indices != NULL && scene->texture != NULL);

    // 字面值：场景必须是非均匀的，任一 texel 改动都应被本文件的字面值发现。
    // Literals: the scene must be nonuniform, so changing any texel is caught by these values.
    static const struct { uint32_t index; uint8_t rgba[4]; } expected[] = {
        {0, {255, 0, 0, 255}}, {1, {0, 255, 0, 255}}, {3, {255, 255, 0, 255}},
        {4, {255, 0, 255, 255}}, {6, {255, 128, 0, 255}}, {11, {255, 255, 255, 255}},
        {12, {64, 32, 16, 255}}, {15, {32, 32, 32, 255}},
    };
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++)
        CHECK(memcmp(scene->texture + expected[i].index * 4u, expected[i].rgba, 4) == 0);
    // 16 个 texel 两两不同（非均匀），且全部不透明。| All 16 texels pairwise distinct and opaque.
    for (uint32_t a = 0; a < 16u; a++) {
        CHECK(scene->texture[a * 4u + 3u] == 255u);
        for (uint32_t b = a + 1u; b < 16u; b++)
            CHECK(memcmp(scene->texture + a * 4u, scene->texture + b * 4u, 4) != 0);
    }
    // 固定几何：±0.75 位置、0/1 UV、索引 0..3。| Fixed geometry: ±0.75 positions, 0/1 UVs, indices 0..3.
    for (uint32_t i = 0; i < 4u; i++) {
        CHECK(scene->vertices[i].position[0] == -0.75f || scene->vertices[i].position[0] == 0.75f);
        CHECK(scene->vertices[i].position[1] == -0.75f || scene->vertices[i].position[1] == 0.75f);
        CHECK(scene->vertices[i].uv[0] == 0.0f || scene->vertices[i].uv[0] == 1.0f);
        CHECK(scene->vertices[i].uv[1] == 0.0f || scene->vertices[i].uv[1] == 1.0f);
    }
    static const uint16_t expected_indices[6] = {0, 1, 2, 0, 2, 3};
    CHECK(memcmp(scene->indices, expected_indices, sizeof expected_indices) == 0);
    for (uint32_t i = 0; i < 6u; i++) CHECK(scene->indices[i] < scene->vertex_count);
}

static void checker_positives(void) {
    const dm_diag_scene *scene = dm_diag_scene_owned();
    const uint8_t clear[4] = {0, 0, 0, 255};
    const uint32_t sizes[3][2] = {{64u, 64u}, {256u, 256u}, {97u, 53u}};
    for (size_t s = 0; s < 3u; s++) {
        const uint32_t width = sizes[s][0], height = sizes[s][1];
        const size_t capacity = (size_t)width * height * 4u;
        uint8_t *canvas = malloc(capacity);
        CHECK(canvas != NULL);
        if (!canvas) continue;
        memset(canvas, 0, capacity);
        // 背景填充清屏色，再写 16 个 texel 检查点。| Fill the clear colour, then paint the 16 texel probes.
        for (size_t i = 0; i < capacity; i += 4u) memcpy(canvas + i, clear, 4);
        paint_probes(scene, canvas, width, height);
        dm_diag_scene_mismatch mismatch;
        CHECK(dm_diag_scene_check_pixels(scene, canvas, width, height, capacity, &mismatch) == DM_DIAG_SCENE_PROBE_COUNT);
        CHECK(mismatch.kind == DM_DIAG_SCENE_MISMATCH_NONE);
        // 大画布必须也接受带余量的容量。| A larger capacity than required must also be accepted.
        CHECK(dm_diag_scene_check_pixels(scene, canvas, width, height, capacity + 64u, &mismatch) == DM_DIAG_SCENE_PROBE_COUNT);
        free(canvas);
    }
}

static void checker_negatives(void) {
    const dm_diag_scene *scene = dm_diag_scene_owned();
    const uint32_t width = 64u, height = 64u;
    const size_t capacity = (size_t)width * height * 4u;
    uint8_t *canvas = calloc(1, capacity);
    CHECK(canvas != NULL);
    if (!canvas) return;
    const uint8_t clear[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < capacity; i += 4u) memcpy(canvas + i, clear, 4);
    paint_probes(scene, canvas, width, height);

    // 单个 texel 检查点被改：必须定位到该点并给出期望/实测值。
    // One texel probe changed: it must be located with expected/observed values.
    uint32_t x, y;
    expected_probe(2u, 3u, width, height, &x, &y);
    size_t offset = ((size_t)y * width + x) * 4u;
    const uint8_t expected_texel[4] = {0, 64, 64, 255};   // texel (2,3) | texel (2,3)
    CHECK(memcmp(canvas + offset, expected_texel, 4) == 0);
    const uint8_t wrong[4] = {1, 2, 3, 4};
    memcpy(canvas + offset, wrong, 4);
    dm_diag_scene_mismatch mismatch;
    CHECK(dm_diag_scene_check_pixels(scene, canvas, width, height, capacity, &mismatch) == 0);
    CHECK(mismatch.kind == DM_DIAG_SCENE_MISMATCH_PIXEL);
    CHECK(mismatch.x == x && mismatch.y == y);
    CHECK(memcmp(mismatch.expected, expected_texel, 4) == 0);
    CHECK(memcmp(mismatch.observed, wrong, 4) == 0);
    memcpy(canvas + offset, expected_texel, 4);

    // 四边形外角点被改：清屏色缺失必须失败。| A corner outside the quad changed: a missing clear must fail.
    const uint8_t dirty[4] = {9, 9, 9, 255};
    memcpy(canvas, dirty, 4);
    CHECK(dm_diag_scene_check_pixels(scene, canvas, width, height, capacity, &mismatch) == 0);
    CHECK(mismatch.kind == DM_DIAG_SCENE_MISMATCH_PIXEL);
    CHECK(mismatch.x == 0 && mismatch.y == 0);
    CHECK(memcmp(mismatch.expected, clear, 4) == 0);
    memcpy(canvas, clear, 4);

    // 无效画布：容量少 1 字节、空指针、零宽/零高都必须在读画布前拒绝。
    // Invalid canvases (one byte short, null pointer, zero width/height) must be rejected before reading the canvas.
    CHECK(dm_diag_scene_check_pixels(scene, canvas, width, height, capacity - 1u, &mismatch) == 0);
    CHECK(mismatch.kind == DM_DIAG_SCENE_MISMATCH_INPUT);
    CHECK(dm_diag_scene_check_pixels(scene, NULL, width, height, capacity, &mismatch) == 0);
    CHECK(mismatch.kind == DM_DIAG_SCENE_MISMATCH_INPUT);
    CHECK(dm_diag_scene_check_pixels(scene, canvas, 0u, height, capacity, &mismatch) == 0);
    CHECK(dm_diag_scene_check_pixels(scene, canvas, width, 0u, capacity, &mismatch) == 0);
    CHECK(dm_diag_scene_check_pixels(NULL, canvas, width, height, capacity, &mismatch) == 0);
    // 畸形场景副本也必须拒绝。| A malformed scene copy must be rejected too.
    dm_diag_scene broken = *scene;
    broken.texture_size -= 1u;
    CHECK(dm_diag_scene_check_pixels(&broken, canvas, width, height, capacity, &mismatch) == 0);
    broken = *scene;
    broken.texture_width = 0u;
    CHECK(dm_diag_scene_check_pixels(&broken, canvas, width, height, capacity, &mismatch) == 0);
    free(canvas);
}

static void lifecycle_suppression(void) {
    fake_host host = {0};
    dm_app_lifecycle *lc = make_lifecycle(&host);
    CHECK(lc != NULL);
    if (!lc) return;
    host.lifecycle = lc;
    void *window_a = (void *)0x1000, *window_b = (void *)0x2000, *late = (void *)0x3000;

    // created：尚未启动，不得绘制。| created: not started, draw suppressed.
    CHECK(!dm_diag_scene_ready(lc, window_a));
    CHECK(dm_app_lifecycle_start(lc));
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_OWNED_DIAGNOSTIC);
    CHECK(host.loads == 1);
    // starting/loading 阶段的观察回调里也不得就绪。| The starting/loading observer callbacks must not be ready either.
    CHECK(host.ready_during_starting == 0 && host.ready_during_loading == 0);
    // 无 Surface 时不得绘制；注册后只有该 Surface 就绪。| No Surface yet; only the registered Surface is ready.
    CHECK(!dm_diag_scene_ready(lc, window_a));
    CHECK(dm_app_lifecycle_surface_created(lc, window_a));
    CHECK(dm_diag_scene_ready(lc, window_a));
    CHECK(!dm_diag_scene_ready(lc, window_b));   // 非所有者窗口 | non-owned window
    CHECK(!dm_diag_scene_ready(lc, NULL));
    CHECK(!dm_diag_scene_ready(NULL, window_a));

    // 暂停后不得绘制，恢复后重新就绪。| Paused suppresses the draw; resume restores readiness.
    CHECK(dm_app_lifecycle_pause(lc));
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_PAUSED);
    CHECK(!dm_diag_scene_ready(lc, window_a));
    CHECK(!dm_diag_scene_ready(lc, window_b));
    CHECK(dm_app_lifecycle_resume(lc));
    CHECK(dm_diag_scene_ready(lc, window_a));

    // Surface 销毁后即使残留旧回调参数也不得绘制；新窗口注册后恢复。
    // After Surface destruction the stale callback argument must not draw; a new window restores readiness.
    CHECK(dm_app_lifecycle_surface_destroyed(lc, window_a));
    CHECK(!dm_diag_scene_ready(lc, window_a));
    CHECK(dm_app_lifecycle_surface_created(lc, window_b));
    CHECK(dm_diag_scene_ready(lc, window_b));
    CHECK(!dm_diag_scene_ready(lc, window_a));

    // 停止中（有未完成工作）不得绘制，排空后保持停止。| Stopping with pending work suppresses drawing; drained stays stopped.
    CHECK(dm_app_lifecycle_work_begin(lc));
    CHECK(dm_app_lifecycle_stop(lc));
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPING);
    CHECK(!dm_diag_scene_ready(lc, window_b));
    CHECK(dm_app_lifecycle_work_end(lc));
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(!dm_diag_scene_ready(lc, window_b));

    // 停止后的迟到回调：Surface 注册被拒绝，任何窗口参数都不得绘制。| Late callbacks after stop: registration rejected, nothing ready.
    CHECK(!dm_app_lifecycle_surface_created(lc, late));
    CHECK(dm_app_lifecycle_surface(lc) == NULL);
    CHECK(!dm_diag_scene_ready(lc, late));
    CHECK(!dm_diag_scene_ready(lc, window_b));
    CHECK(!dm_diag_scene_ready(lc, window_a));
    CHECK(dm_app_lifecycle_destroy(lc));

    // 加载失败：整个控制器保持 failed，永不就绪。| Load failure keeps the controller failed and never ready.
    fake_host failing = {0};
    failing.load_result = 1;
    dm_app_lifecycle *bad = make_lifecycle(&failing);
    CHECK(bad != NULL);
    if (bad) {
        failing.lifecycle = bad;
        CHECK(!dm_app_lifecycle_start(bad));
        CHECK(dm_app_lifecycle_state(bad) == DM_APP_STATE_FAILED);
        CHECK(!dm_diag_scene_ready(bad, window_a));
        CHECK(!dm_app_lifecycle_surface_created(bad, window_a));
        CHECK(!dm_diag_scene_ready(bad, window_a));
        CHECK(dm_app_lifecycle_destroy(bad));
    }
}

int main(void) {
    scene_literals();
    checker_positives();
    checker_negatives();
    lifecycle_suppression();
    printf("app_diagnostic_scene: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

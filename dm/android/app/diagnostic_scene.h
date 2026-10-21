// #117 自有工程诊断场景：非均匀 4×4 RGBA8 纹理 + 固定索引四边形、探针像素检查与绘制就绪判定。
// 依据已验收 #115 固定 indexed+textured 接口（float2 位置 + float2 UV、uint16 索引、单张 RGBA8 纹理）
// 与 app/lifecycle.h 的 owned-diagnostic 状态机；不含 Android、Vulkan 或游戏头文件。
// #117 owned engineering-diagnostic scene: a nonuniform 4x4 RGBA8 texture plus a fixed indexed quad, probe
// pixel checking and a draw-readiness predicate. Based on the accepted #115 fixed indexed+textured interface
// (float2 position + float2 UV, uint16 indices, one RGBA8 texture) and the app/lifecycle.h owned-diagnostic
// state machine; no Android, Vulkan or game headers are involved.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lifecycle.h"

#define DM_DIAG_SCENE_TEXTURE_WIDTH 4u
#define DM_DIAG_SCENE_TEXTURE_HEIGHT 4u
#define DM_DIAG_SCENE_VERTEX_COUNT 4u
#define DM_DIAG_SCENE_INDEX_COUNT 6u
// 四边形占 NDC ±0.75，UV 0..1：四角留出清屏色边界，四个 texel 行/列全部被采样。
// The quad spans NDC ±0.75 with UV 0..1, leaving a clear-colour border while every texel row/column is sampled.
#define DM_DIAG_SCENE_QUAD_EXTENT 0.75f
// 检查点：16 个 texel 中心 + 4 个角点背景。| Probes: 16 texel centres plus 4 corner background points.
#define DM_DIAG_SCENE_PROBE_COUNT 20u

typedef struct {
    float position[2];   // NDC，有限且 [-1,1] | finite NDC within [-1,1]
    float uv[2];         // 有限且 [0,1] | finite within [0,1]
} dm_diag_scene_vertex;

// 场景由本模块静态持有；纹理按行紧排 RGBA8。调用者只读，不得释放其中任何指针。
// The module statically owns the scene; the texture is tightly packed row-major RGBA8. Callers read only and
// never free any pointer inside it.
typedef struct {
    const dm_diag_scene_vertex *vertices;
    uint32_t vertex_count;
    const uint16_t *indices;
    uint32_t index_count;
    const uint8_t *texture;
    uint32_t texture_width;
    uint32_t texture_height;
    size_t texture_size;
} dm_diag_scene;

typedef enum {
    DM_DIAG_SCENE_MISMATCH_NONE = 0,
    DM_DIAG_SCENE_MISMATCH_PIXEL = 1,      // 读回像素与场景期望不符 | readback pixel differs from the scene expectation
    DM_DIAG_SCENE_MISMATCH_INPUT = 2       // 画布参数无效 | invalid canvas arguments
} dm_diag_scene_mismatch_kind;

typedef struct {
    dm_diag_scene_mismatch_kind kind;
    uint32_t x, y;             // 首个不符点或无效画布时的 0,0 | first mismatch position, 0,0 for invalid input
    uint8_t expected[4];
    uint8_t observed[4];
} dm_diag_scene_mismatch;

const dm_diag_scene *dm_diag_scene_owned(void);

// 按场景几何独立推算 20 个检查点并与读回画布比较；全部相符返回检查点数量，否则返回 0 并填写首个不符点。
// Derives the 20 probes from the scene geometry and compares the readback canvas; returns the probe count when
// everything matches, otherwise 0 with the first mismatch filled in.
uint32_t dm_diag_scene_check_pixels(const dm_diag_scene *scene, const uint8_t *pixels,
                                    uint32_t width, uint32_t height, size_t capacity,
                                    dm_diag_scene_mismatch *mismatch);

// 仅当控制器处于 owned-diagnostic、确实持有该 Surface 时才允许绘制；暂停/停止/失败/陈旧或晚到回调返回 0。
// Draw is allowed only in owned-diagnostic while the controller owns exactly this Surface; paused, stopping,
// stopped, failed, stale or late callbacks return 0.
int dm_diag_scene_ready(const dm_app_lifecycle *lifecycle, const void *surface);

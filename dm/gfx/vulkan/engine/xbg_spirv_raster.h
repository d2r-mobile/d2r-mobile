// 全覆盖首绘证明要的着色器事实（nvn_cover.h；Metal 侧同名事实来自 dmsh_build 的 raster_facts）
// Shader facts for the full-coverage first-draw proof (nvn_cover.h; the Metal side gets the same facts from dmsh_build's
// raster_facts)
#pragma once
#include <cstddef>
#include <cstdint>

struct XbgRasterFacts {
    // Vertex: the Input location whose components 0, 1 and 2 are what gl_Position.x, .y and .z hold when the shader ends,
    // or -1 when that cannot be shown (more than one block, a function call, ClipDistance or CullDistance written, a position
    // component computed rather than copied).
    int pos_input=-1;
    // Vertex: gl_Position.w ends as the constant 1.0 (otherwise it is component 3 of the same input).
    bool pos_w1=false;
    // Fragment: bit L set when an Output variable has Location L.
    uint32_t outputs=0;
    // Fragment: an Output variable is BuiltIn SampleMask.
    bool sample_mask=false;
};

// stage: 0 vertex, 4 fragment (the XGS stage numbers); other stages return the defaults.
XbgRasterFacts xbg_spirv_raster_facts(const uint32_t *words, size_t count, uint32_t stage);

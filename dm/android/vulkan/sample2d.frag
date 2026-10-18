// 采样绘制片段着色器：uv 来自片段坐标与 push constant，LOD 显式给出；纹理与采样器都是自有的合成资源。
// Sampled-draw fragment shader: uv comes from fragment coordinates and push constants; LOD is explicit; texture and
// sampler are owned synthetic resources.
#version 450
layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(location = 0) out vec4 color;
layout(push_constant) uniform Params { vec2 inv_size; float scale; float lod; } params;
void main() {
    vec2 uv = gl_FragCoord.xy * params.inv_size * params.scale;
    color = textureLod(source_texture, uv, params.lod);
}

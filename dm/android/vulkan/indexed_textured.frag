#version 450
// #115 固定 textured fragment 接口：单个 sampler2D，nearest/clamp 单级采样。
layout(location = 0) in vec2 vertex_uv;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = textureLod(tex, vertex_uv, 0.0);
}

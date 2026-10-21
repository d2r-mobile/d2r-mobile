#version 450
// #115 固定 textured vertex 接口：float2 position + float2 UV。
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec2 vertex_uv;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vertex_uv = uv;
}

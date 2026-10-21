#version 450
// 负例：UV 输入是 vec3，不是固定的 vec2。
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 uv;
layout(location = 0) out vec2 vertex_uv;
void main() { gl_Position = vec4(position, 0.0, 1.0); vertex_uv = uv.xy; }

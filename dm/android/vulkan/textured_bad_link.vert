#version 450
// 负例：VS varying location 1 与 FS location 0 不匹配。
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 1) out vec2 vertex_uv;
void main() { gl_Position = vec4(position, 0.0, 1.0); vertex_uv = uv; }

#version 450
layout(location = 0) in vec2 vertex_uv;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(vertex_uv, 0.0, 1.0); }

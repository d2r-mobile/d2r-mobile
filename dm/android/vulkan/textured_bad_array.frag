#version 450
layout(location = 0) in vec2 vertex_uv;
layout(set = 0, binding = 0) uniform sampler2D tex[2];
layout(location = 0) out vec4 out_color;
void main() { out_color = textureLod(tex[0], vertex_uv, 0.0); }

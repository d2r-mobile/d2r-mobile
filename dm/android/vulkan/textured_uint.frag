#version 450
layout(location = 0) in vec2 vertex_uv;
layout(set = 0, binding = 0) uniform usampler2D tex;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(textureLod(tex, vertex_uv, 0.0)); }

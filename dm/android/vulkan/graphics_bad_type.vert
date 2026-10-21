#version 450
// 负例：颜色输入是 vec3 而不是固定 vec4。 | Negative: color input is vec3 instead of fixed vec4.
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;
layout(location = 0) out vec4 vertex_color;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vertex_color = vec4(color, 1.0);
}

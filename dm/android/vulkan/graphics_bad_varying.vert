#version 450
// 负例：VS varying location 与 FS 输入不匹配。 | Negative: VS varying location does not match the FS input.
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 1) out vec4 vertex_color;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vertex_color = color;
}

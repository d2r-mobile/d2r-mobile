#version 450
// 负例：图形接口声明 push constant，必须拒绝。 | Negative: graphics interface declares push constants; must reject.
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vertex_color;
layout(push_constant) uniform Push { vec4 tint; } pc;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vertex_color = color + pc.tint;
}

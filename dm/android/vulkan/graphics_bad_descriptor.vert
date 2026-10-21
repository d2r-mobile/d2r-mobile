#version 450
// 负例：图形接口仍声明 descriptor，必须拒绝。 | Negative: graphics interface declares a descriptor; must reject.
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vertex_color;
layout(set = 0, binding = 0) uniform U { vec4 tint; } ub;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vertex_color = color + ub.tint;
}

#version 450
// #112 固定 vertex 接口：location 0 float2 position，location 1 float4 color。
// #112 fixed vertex interface: location 0 float2 position, location 1 float4 color.
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vertex_color;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vertex_color = color;
}

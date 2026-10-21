#version 450
// #112 固定 fragment 接口：插值后的 RGBA 直接写入 RGBA8 目标。
// #112 fixed fragment interface: interpolated RGBA written directly to the RGBA8 target.
layout(location = 0) in vec4 vertex_color;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vertex_color;
}

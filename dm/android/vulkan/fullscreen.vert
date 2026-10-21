// 全屏合成三角形，依据 Vulkan gl_VertexIndex，不包含游戏着色器。
// Synthetic fullscreen triangle using Vulkan gl_VertexIndex, without game shaders.
#version 450
void main(){
    vec2 positions[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(positions[gl_VertexIndex],0,1);
}

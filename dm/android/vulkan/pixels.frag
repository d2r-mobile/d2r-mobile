// 按片段坐标输出可精确读回的图案，依据 Vulkan gl_FragCoord / RGBA8 UNORM。
// Emit an exactly checkable pattern using Vulkan gl_FragCoord / RGBA8 UNORM.
#version 450
layout(location=0) out vec4 color;
void main(){
    uvec2 pixel=uvec2(gl_FragCoord.xy);
    color=vec4(float(pixel.x&15u)/15.0,float(pixel.y&15u)/15.0,float((pixel.x^pixel.y)&1u),1);
}

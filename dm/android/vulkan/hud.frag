#version 450
layout(location=0) in vec4 v_color;
layout(location=0) out vec4 color;
layout(push_constant) uniform Dims { float width; float height; } dims;
// Synthetic nonuniform background; alpha deliberately varies so blending and
// outside-region checks cannot pass on a constant clear.
vec4 background(vec2 pixel){
    vec2 uv=pixel/vec2(dims.width,dims.height);
    vec4 tl=vec4(0.10,0.16,0.24,1.00);
    vec4 tr=vec4(0.24,0.10,0.14,0.90);
    vec4 bl=vec4(0.08,0.22,0.18,0.85);
    vec4 br=vec4(0.20,0.18,0.08,0.95);
    vec4 top=mix(tl,tr,uv.x);
    vec4 bottom=mix(bl,br,uv.x);
    return mix(top,bottom,uv.y);
}
void main(){
    color = v_color.a < 0.0 ? background(gl_FragCoord.xy) : v_color;
}

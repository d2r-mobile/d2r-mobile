#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec4 color;
layout(location=0) out vec4 v_color;
void main(){
    v_color=color;
    gl_Position=vec4(position,0,1);
}

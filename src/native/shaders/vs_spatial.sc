$input a_position, a_texcoord0
$output v_texcoord0
#include <bgfx_shader.sh>
void main(){gl_Position=mul(u_viewProj,vec4(a_position,1.0));gl_Position.z-=0.00002*gl_Position.w;v_texcoord0=a_texcoord0;}

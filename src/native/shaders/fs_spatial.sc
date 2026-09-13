$input v_texcoord0
#include <bgfx_shader.sh>
uniform vec4 u_overlayColor;
uniform vec4 u_overlayParams;
void main(){vec3 color=u_overlayColor.rgb;if(u_overlayParams.x<0.5)color*=mix(0.65,1.0,clamp(v_texcoord0.x,0.0,1.0));gl_FragColor=vec4(color,u_overlayColor.a);}

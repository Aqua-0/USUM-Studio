$input v_texcoord0, v_color0
#include <bgfx_shader.sh>
SAMPLER2D(s_particleTexture,0);
void main(){vec4 color=texture2D(s_particleTexture,v_texcoord0)*v_color0;if(color.a<0.004)discard;gl_FragColor=color;}

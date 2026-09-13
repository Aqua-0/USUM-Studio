$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_postSource,0);
SAMPLER2D(s_postBlur,1);
SAMPLER2D(s_postMask,2);
uniform vec4 u_postMaskMapping;
uniform vec4 u_postOptions;
uniform vec4 u_postWeights;
void main(){
  vec4 source=texture2D(s_postSource,v_texcoord0);
  if(u_postOptions.x>2.5){
    float edge=0.0;
    for(int i=0;i<4;++i){
      vec2 offset=i==0?vec2(u_postOptions.y,0.0):i==1?vec2(-u_postOptions.y,0.0):i==2?vec2(0.0,u_postOptions.z):vec2(0.0,-u_postOptions.z);
      vec4 other=texture2D(s_postSource,v_texcoord0+offset);
      vec3 delta=source.rgb-other.rgb;
      edge=max(edge,smoothstep(0.08,0.18,dot(delta,delta)));
      edge=max(edge,step(0.002,abs(source.a-other.a)));
    }
    gl_FragColor=vec4(vec3(0.025),edge*0.9);return;
  }
  if(u_postOptions.x>1.5){gl_FragColor=vec4(source.rgb+texture2D(s_postBlur,v_texcoord0).rgb*u_postOptions.w,source.a);return;}
  vec2 direction=u_postOptions.x<0.5?vec2(u_postOptions.y,0.0):vec2(0.0,u_postOptions.z);
  vec3 sum=vec3(0.0);float total=0.0;
  for(int i=-1;i<=1;++i){float weight=i==0?2.0:1.0;vec3 color=texture2D(s_postSource,v_texcoord0+direction*float(i)).rgb;
    if(u_postOptions.x<0.5){vec2 uv=v_texcoord0+direction*float(i);if(u_postMaskMapping.z>0.5)uv.y=1.0-uv.y;float mask=texture2D(s_postMask,uv*u_postMaskMapping.xy).a;color=dot(color,u_postWeights.rgb)<=u_postWeights.a?vec3(0.0):color*mask;}sum+=color*weight;total+=weight;}
  gl_FragColor=vec4(sum/total,1.0);
}

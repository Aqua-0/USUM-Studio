$input a_position, a_normal, a_tangent, a_texcoord0, a_texcoord1, a_texcoord2, a_color0, a_texcoord3, a_texcoord4
$output v_edgeColor, v_color0, v_texcoord0, v_texcoord1, v_texcoord2, v_worldPosition, v_normal, v_tangent, v_projectedUv, v_objectNormal, v_objectTangent, v_objectLight
#include <bgfx_shader.sh>
uniform vec4 u_skin;
uniform vec4 u_refresh;
uniform vec4 u_objectBasis[2];
uniform vec4 u_selectionColor;
uniform vec4 u_vertexLighting;
uniform vec4 u_vertexEffect;
uniform vec4 u_projectionRows[6];
uniform vec4 u_projectionOptions;
uniform vec4 u_projectionOffsets;
uniform vec4 u_rimPhong;
uniform vec4 u_vertexLightDirection;
uniform vec4 u_boneRows[93];
uniform vec4 u_normalRows[93];
void main() {
  vec3 position=a_position, normal=a_normal, tangent=a_tangent;
  vec3 objectNormal=u_objectBasis[1].xyz,objectTangent=u_objectBasis[0].xyz;
  if(u_skin.x>0.5){position=vec3(0.0);normal=vec3(0.0);tangent=vec3(0.0);objectNormal=vec3(0.0);objectTangent=vec3(0.0);
    for(int k=0;k<4;++k){int i=int(a_texcoord3[k])*3;float weight=a_texcoord4[k];vec4 p=vec4(a_position,1.0);
      position+=weight*vec3(dot(u_boneRows[i],p),dot(u_boneRows[i+1],p),dot(u_boneRows[i+2],p));
      normal+=weight*vec3(dot(u_normalRows[i].xyz,a_normal),dot(u_normalRows[i+1].xyz,a_normal),dot(u_normalRows[i+2].xyz,a_normal));
      objectNormal+=weight*vec3(dot(u_boneRows[i].xyz,u_objectBasis[1].xyz),dot(u_boneRows[i+1].xyz,u_objectBasis[1].xyz),dot(u_boneRows[i+2].xyz,u_objectBasis[1].xyz));
      objectTangent+=weight*vec3(dot(u_boneRows[i].xyz,u_objectBasis[0].xyz),dot(u_boneRows[i+1].xyz,u_objectBasis[0].xyz),dot(u_boneRows[i+2].xyz,u_objectBasis[0].xyz));
      tangent+=weight*vec3(dot(u_boneRows[i].xyz,a_tangent),dot(u_boneRows[i+1].xyz,a_tangent),dot(u_boneRows[i+2].xyz,a_tangent));
    }
  }
  position=mul(u_model[0],vec4(position,1.0)).xyz;
  normal=mul(u_model[0],vec4(normal,0.0)).xyz;
  tangent=mul(u_model[0],vec4(tangent,0.0)).xyz;
  if(u_skin.y< -0.5){vec3 eye=mul(u_invView,vec4(0.0,0.0,0.0,1.0)).xyz;position+=vec3(eye.x,u_skin.z,eye.z);}
  if(u_vertexEffect.x>3.5){position.xz+=(position.y-u_vertexEffect.w)*u_vertexEffect.yz;position.y=u_vertexEffect.w+0.5;}
  gl_Position = mul(u_viewProj, vec4(position, 1.0));
  if(u_skin.y< -0.5)gl_Position.z=gl_Position.w*0.99999;
  if(u_skin.y>0.5){gl_Position=vec4(position.xy*u_skin.zw,0.0,1.0);}
  if(gl_Position.w>0.00001)gl_Position.z-=u_selectionColor.a*min(max(gl_Position.z,0.0),0.00001*abs(gl_Position.w));
  v_worldPosition = position;
  v_objectNormal=mul(u_model[0],vec4(objectNormal,0.0)).xyz;
  v_objectTangent=mul(u_model[0],vec4(objectTangent,0.0)).xyz;
  vec3 objectLight=u_vertexLightDirection.xyz/max(length(u_vertexLightDirection.xyz),0.000001);
  v_objectLight=vec3(dot(objectLight,v_objectTangent),dot(objectLight,cross(v_objectNormal,v_objectTangent)),dot(objectLight,v_objectNormal));
  v_objectLight/=max(length(v_objectLight),0.000001);
  v_normal = normal;
  v_tangent = tangent;
  v_edgeColor = a_color0;
  v_color0 = a_color0;
  if(u_vertexLighting.x>0.5){
    vec3 n=normal/max(length(normal),0.000001);
    vec3 view=mul(u_invView,vec4(0.0,0.0,1.0,0.0)).xyz;
    view/=max(length(view),0.000001);
    vec3 light=u_vertexLightDirection.xyz/max(length(u_vertexLightDirection.xyz),0.000001);
    vec3 halfDirection=view+light;
    halfDirection/=max(length(halfDirection),0.000001);
    float back=max(dot(n,-light),0.0)*(max(dot(view,-light),0.0)*0.5+0.5);
    float phong=pow(max(dot(n,halfDirection),0.000001),u_rimPhong.z)*u_rimPhong.w;
    float rim=pow(max(1.0-max(dot(n,view),0.0),0.000001),u_rimPhong.x)*u_rimPhong.y;
    float heightColor=0.0;
    if(u_vertexEffect.x>0.5 && u_vertexEffect.x<1.5){
      float height=u_vertexEffect.w*length(mul(u_model[0],vec4(1.0,0.0,0.0,0.0)).xyz);
      if(abs(height)>0.000001)heightColor=(position.y-u_vertexEffect.z)/height;
    }
    v_color0=clamp(vec4(heightColor,back*u_vertexLighting.y,phong*u_vertexLighting.z,rim*u_vertexLighting.w),0.0,1.0);
  }
  if(u_vertexEffect.x>3.5)v_color0=vec4(1.0);
  v_texcoord0 = a_texcoord0;
  if(u_vertexEffect.x>1.5&&u_vertexEffect.x<3.5&&u_refresh.x<0.5){
    vec4 projected=mul(u_viewProj,vec4(position+normal*u_vertexEffect.y,1.0));
    v_texcoord0=projected.xy/max(projected.w,0.000001)*0.5+0.5;
    if(u_vertexEffect.x>2.5)v_texcoord0.y=1.0-v_texcoord0.y;
  }
  v_texcoord1 = a_texcoord1;
  v_texcoord2 = a_texcoord2;
  vec4 world=vec4(position,1.0);
  v_projectedUv=vec3(0.0,0.0,1.0);
  if(u_projectionOptions.x>0.5){
    v_projectedUv=vec3(dot(world,u_projectionRows[0]),dot(world,u_projectionRows[1]),dot(world,u_projectionRows[2]));
    v_projectedUv.xy+=u_projectionOffsets.xy*v_projectedUv.z;
  }
  if(u_projectionOptions.y>0.5){
    vec3 projected=vec3(dot(world,u_projectionRows[3]),dot(world,u_projectionRows[4]),dot(world,u_projectionRows[5]));
    v_texcoord1=projected.xy/(abs(projected.z)>0.000001?projected.z:0.000001)+u_projectionOffsets.zw;
  }
}

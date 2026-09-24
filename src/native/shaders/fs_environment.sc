$input v_edgeColor, v_color0, v_texcoord0, v_texcoord1, v_texcoord2, v_worldPosition, v_normal, v_tangent, v_projectedUv, v_objectNormal, v_objectTangent, v_objectLight
#include <bgfx_shader.sh>
SAMPLER2D(s_tex, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
SAMPLER2D(s_lookup, 3);
SAMPLER2D(s_refreshMask, 4);
SAMPLER2D(s_refreshPalette, 5);
uniform vec4 u_refresh;
uniform vec4 u_pickColor;
uniform vec4 u_selectionColor;
uniform vec4 u_edgeOptions;
uniform vec4 u_fog;
uniform vec4 u_fogColor;
uniform vec4 u_fogDepth;
uniform vec4 u_gameLight;
uniform vec4 u_gameAmbient;
uniform vec4 u_gameDirections[8];
uniform vec4 u_gameColors[8];
uniform vec4 u_lookupInputs[3];
uniform vec4 u_uvRowU[3];
uniform vec4 u_uvRowV[3];
uniform vec4 u_colorSources[6];
uniform vec4 u_alphaSources[6];
uniform vec4 u_colorOperands[6];
uniform vec4 u_alphaOperands[6];
uniform vec4 u_operations[6];
uniform vec4 u_constants[6];
uniform vec4 u_bufferWrites[6];
uniform vec4 u_buffer;
uniform vec4 u_preview;
uniform vec4 u_texturePrecision;
uniform vec4 u_projectionOptions;
uniform vec4 u_cutaway;
uniform vec4 u_lighting;
uniform vec4 u_bump;
uniform vec4 u_lightDirection;
uniform vec4 u_surface[5];
uniform vec4 u_textureOptions[3];
float textureCoordinatePrecision(float value) {
  uint bits = floatBitsToUint(value);
  if ((bits & 0x7f800000u) == 0x7f800000u) return value;
  // Round 23 fraction bits to 16, with nearest-even ties.
  bits = (bits + 63u + ((bits >> 7u) & 1u)) & 0xffffff80u;
  return uintBitsToFloat(bits);
}
vec2 textureCoordinates(vec2 uv) {
  if (u_texturePrecision.x < 0.5) return uv;
  return vec2(textureCoordinatePrecision(uv.x), textureCoordinatePrecision(uv.y));
}
vec3 unitDirection(vec3 value, vec3 fallback) {
  float size = dot(value,value);
  return size > 0.000001 ? value*inversesqrt(size) : fallback;
}
float lightingLookup(int channel, vec3 normal, vec3 light, vec3 view, vec3 halfway, vec3 tangent, float normalView, float fallback) {
  vec4 config = u_lookupInputs[channel];
  if(config.x < 0.0) return fallback;
  float x = dot(normal,halfway);
  if(config.y > 0.5 && config.y < 1.5) x = dot(view,halfway);
  else if(config.y > 1.5 && config.y < 2.5) x = normalView;
  else if(config.y > 2.5 && config.y < 3.5) x = dot(light,normal);
  else if(config.y > 4.5) x = dot(halfway-normal*dot(normal,halfway),tangent);
  float position = config.z > 0.5 ? min(max(x,0.0)*256.0,255.0) : clamp(x*128.0,-128.0,127.0);
  float left = floor(position), blend = position-left;
  float a = mod(left+256.0,256.0), b = config.z > 0.5 ? min(a+1.0,255.0) : mod(a+1.0,256.0);
  float row = (config.x+0.5)/u_gameLight.z;
  float value = mix(texture2DLod(s_lookup,vec2((a+0.5)/256.0,row),0.0).r,texture2DLod(s_lookup,vec2((b+0.5)/256.0,row),0.0).r,blend);
  return clamp(value*config.w,0.0,1.0);
}
vec2 materialUV(int unit, vec2 uv0, vec2 uv1, vec2 uv2, vec3 worldNormal) {
  float source = u_uvRowU[unit].w;
  vec2 uv = source < 0.5 ? uv0 : source < 1.5 ? uv1 : uv2;
  if(source>3.5){vec3 n=normalize(mul(u_view,vec4(normalize(worldNormal),0.0)).xyz);uv=n.xy*0.5+0.5;}
  return vec2(dot(vec3(uv,1.0),u_uvRowU[unit].xyz),dot(vec3(uv,1.0),u_uvRowV[unit].xyz));
}
float materialLod(int unit, vec2 uv) {
  vec4 options = u_textureOptions[unit];
  if(options.x < 0.5) return 0.0;
  vec2 dx = dFdx(uv)*options.zw, dy = dFdy(uv)*options.zw;
  float lod = 0.5*log2(max(max(dot(dx,dx),dot(dy,dy)),0.000001));
  return min(lod,options.y);
}
vec4 sourceColor(float id, vec4 previous, vec4 combBuffer, vec4 primary, vec4 t0, vec4 t1, vec4 t2, vec4 constantColor, vec4 fragmentPrimary, vec4 fragmentSecondary) {
  if(id < 0.5) return primary;
  if(id < 1.5) return fragmentPrimary;
  if(id < 2.5) return fragmentSecondary;
  if(id < 3.5) return t0;
  if(id < 4.5) return t1;
  if(id < 5.5) return t2;
  if(id < 12.5) return vec4(0.0);
  if(id < 13.5) return combBuffer;
  if(id < 14.5) return constantColor;
  return previous;
}
vec4 colorOperand(float op, vec4 v) {
  float base = floor(op * 0.5);
  vec4 value = v;
  if(base > 0.5 && base < 1.5) value = v.aaaa;
  else if(base < 2.5 && base > 1.5) value = v.rrrr;
  else if(base > 3.5 && base < 4.5) value = v.gggg;
  else if(base > 5.5) value = v.bbbb;
  return mod(op,2.0) > 0.5 ? vec4(1.0)-value : value;
}
float alphaOperand(float op, vec4 v) {
  float base = floor(op*0.5);
  float value = base < 0.5 ? v.a : base < 1.5 ? v.r : base < 2.5 ? v.g : v.b;
  return mod(op,2.0)>0.5 ? 1.0-value : value;
}
vec3 combine(float op, vec3 a, vec3 b, vec3 c) {
  vec3 value;
  if(op < 0.5) value = a;
  else if(op < 1.5) value = a*b;
  else if(op < 2.5) value = a+b;
  else if(op < 3.5) value = a+b-vec3(0.5);
  else if(op < 4.5) value = a*c+b*(vec3(1.0)-c);
  else if(op < 5.5) value = a-b;
  else if(op < 7.5) value = vec3(4.0*dot(a-vec3(0.5),b-vec3(0.5)));
  else if(op < 8.5) value = a*b+c;
  else value = min(a+b,vec3(1.0))*c;
  return clamp(value,0.0,1.0);
}
void main() {
  if(u_refresh.x>0.5){
    float id=u_refresh.x<1.5?floor(texture2DLod(s_refreshMask,v_texcoord0,0.0).r*255.0+0.5):255.0;
    if(u_pickColor.a>0.5){gl_FragColor=vec4(u_pickColor.rgb,id/255.0);return;}
    vec3 color=texture2DLod(s_refreshPalette,vec2((id+0.5)/256.0,0.5),0.0).rgb;
    if(u_refresh.y>=0.0&&abs(id-u_refresh.y)>0.5)color*=0.25;
    gl_FragColor=vec4(color,1.0);return;
  }

  if(u_cutaway.x > 0.5 && v_worldPosition.y > u_cutaway.y) discard;
  if(u_cutaway.w > 0.5){gl_FragColor=vec4(mix(vec3(0.35,0.78,0.92),u_selectionColor.rgb,u_selectionColor.a),1.0);return;}
  if(u_cutaway.z > 0.5 && u_edgeOptions.x < 0.5) {
    vec3 normal = normalize(cross(dFdx(v_worldPosition), dFdy(v_worldPosition)));
    float shade = 0.55 + 0.45*abs(dot(normal, normalize(vec3(0.3, 0.8, 0.5))));
    gl_FragColor = u_pickColor.a > 0.5 ? vec4(u_pickColor.rgb,1.0) : vec4(mix(vec3(0.55, 0.40, 0.28)*shade,u_selectionColor.rgb,u_selectionColor.a), 1.0);
    return;
  }
  vec2 uv0 = materialUV(0, v_texcoord0, v_texcoord1, v_texcoord2, v_normal);
  if(u_projectionOptions.x>0.5)uv0=v_projectedUv.xy/(abs(v_projectedUv.z)>0.000001?v_projectedUv.z:0.000001);
  vec4 tex0 = texture2DLod(s_tex, textureCoordinates(uv0), materialLod(0,uv0));
  vec2 uv1 = materialUV(1, v_texcoord0, v_texcoord1, v_texcoord2, v_normal);
  vec4 tex1 = texture2DLod(s_tex1, textureCoordinates(uv1), materialLod(1,uv1));
  vec2 uv2 = materialUV(2, v_texcoord0, v_texcoord1, v_texcoord2, v_normal);
  vec4 tex2 = texture2DLod(s_tex2, textureCoordinates(uv2), materialLod(2,uv2));
  vec4 primary = mix(vec4(1.0), v_color0, u_preview.y);
  vec4 fragmentPrimary = vec4(1.0), fragmentSecondary = vec4(0.0);
  if(u_lighting.w > 0.5 && u_gameLight.w > 0.5) {
    vec3 normal = unitDirection(v_normal,vec3(0.0,1.0,0.0));
    vec3 objectMapped=vec3(0.0,0.0,1.0);
    if(u_bump.x > 0.5) {
      vec3 tangent = v_tangent-normal*dot(normal,v_tangent);
      if(u_bump.w>0.5){
        objectMapped=unitDirection((u_bump.y<0.5?tex0.rgb:u_bump.y<1.5?tex1.rgb:tex2.rgb)*2.0-1.0,objectMapped);
        normal=unitDirection(v_objectTangent*objectMapped.x+cross(v_objectNormal,v_objectTangent)*objectMapped.y+v_objectNormal*objectMapped.z,normal);
      }else if(dot(tangent,tangent) > 0.000001) {
        tangent = normalize(tangent);
        vec3 mapped = (u_bump.y < 0.5 ? tex0.rgb : u_bump.y < 1.5 ? tex1.rgb : tex2.rgb)*2.0-1.0;
        if(u_bump.z > 0.5) mapped.z = sqrt(max(1.0-dot(mapped.xy,mapped.xy),0.0));
        normal = unitDirection(tangent*mapped.x+cross(normal,tangent)*mapped.y+normal*mapped.z,normal);
      }
    }
    vec3 light = unitDirection(u_lightDirection.xyz,vec3(0.0,1.0,0.0));
    vec3 eye = mul(u_invView,vec4(0.0,0.0,0.0,1.0)).xyz;
    vec3 view = unitDirection(eye-v_worldPosition,normal);
    float normalView=u_bump.w>0.5?dot(objectMapped,unitDirection(v_objectLight,vec3(0.0,0.0,1.0))):dot(normal,view);
    vec3 halfway = unitDirection(view+light,normal);
    float diffuse = u_lightDirection.w>0.5?clamp((dot(normal,light)+0.5)/1.5,0.0,1.0):max(dot(normal,light),0.0);
    float highlight = max(dot(normal,halfway),0.0);
    fragmentPrimary = vec4(clamp(u_surface[0].rgb + u_surface[1].rgb*u_lighting.x + u_surface[2].rgb*diffuse*u_lighting.y,0.0,1.0),1.0);
    vec3 specular = (u_surface[3].rgb*pow(highlight,16.0) + u_surface[4].rgb*pow(highlight,64.0))*u_lighting.z;
    fragmentSecondary = vec4(clamp(specular*step(0.000001,diffuse),0.0,1.0),1.0);
    if(u_gameLight.x < 0.5 && (u_lookupInputs[0].x >= 0.0 || u_lookupInputs[1].x >= 0.0 || u_lookupInputs[2].x >= 0.0)) {
      vec3 tangent=unitDirection(v_tangent-normal*dot(normal,v_tangent),vec3(1.0,0.0,0.0));
      float red=lightingLookup(0,normal,light,view,halfway,tangent,normalView,0.0);
      float green=lightingLookup(1,normal,light,view,halfway,tangent,normalView,red);
      float blue=lightingLookup(2,normal,light,view,halfway,tangent,normalView,red);
      fragmentSecondary=vec4(clamp(vec3(red,green,blue),0.0,1.0),1.0);
    }
    if(u_gameLight.x > 0.5) {
      vec3 lit = (u_surface[0].rgb + u_surface[1].rgb*u_gameAmbient.rgb)*u_gameLight.w;
      vec3 reflected = vec3(0.0);
      vec3 tangent = unitDirection(v_tangent-normal*dot(normal,v_tangent),vec3(1.0,0.0,0.0));
      for(int i=0;i<8;++i) {
        if(float(i) >= u_gameLight.y) break;
        vec3 direction = unitDirection(u_gameDirections[i].xyz,vec3(0.0,1.0,0.0));
        vec3 halfDirection = unitDirection(view+direction,normal);
        lit += u_surface[2].rgb*u_gameColors[i].rgb*max(dot(normal,direction),0.0);
        float red = lightingLookup(0,normal,direction,view,halfDirection,tangent,normalView,0.0);
        float green = lightingLookup(1,normal,direction,view,halfDirection,tangent,normalView,red);
        float blue = lightingLookup(2,normal,direction,view,halfDirection,tangent,normalView,red);
        reflected += vec3(red,green,blue);
      }
      fragmentPrimary = vec4(clamp(lit,0.0,1.0),1.0);
      fragmentSecondary = vec4(clamp(reflected,0.0,1.0),1.0);
    }
  }
  if(u_lighting.w > 0.5 && u_gameLight.w < 0.5){fragmentPrimary=vec4(0.0,0.0,0.0,1.0);fragmentSecondary=vec4(0.0,0.0,0.0,1.0);}
  vec4 color = tex0 * primary;
  if(u_preview.x > 0.5) {
    vec4 previous = vec4(0.0), combBuffer = vec4(0.0);
    for(int i=0;i<6;++i) {
      vec4 cs = u_colorSources[i], asrc = u_alphaSources[i];
      vec4 co = u_colorOperands[i], ao = u_alphaOperands[i];
      vec4 ops = u_operations[i];
      vec4 a = colorOperand(co.x, sourceColor(cs.x, previous, combBuffer, primary, tex0, tex1, tex2, u_constants[i], fragmentPrimary, fragmentSecondary));
      vec4 b = colorOperand(co.y, sourceColor(cs.y, previous, combBuffer, primary, tex0, tex1, tex2, u_constants[i], fragmentPrimary, fragmentSecondary));
      vec4 c = colorOperand(co.z, sourceColor(cs.z, previous, combBuffer, primary, tex0, tex1, tex2, u_constants[i], fragmentPrimary, fragmentSecondary));
      float aa = alphaOperand(ao.x, sourceColor(asrc.x, previous, combBuffer, primary, tex0, tex1, tex2, u_constants[i], fragmentPrimary, fragmentSecondary));
      float ab = alphaOperand(ao.y, sourceColor(asrc.y, previous, combBuffer, primary, tex0, tex1, tex2, u_constants[i], fragmentPrimary, fragmentSecondary));
      float ac = alphaOperand(ao.z, sourceColor(asrc.z, previous, combBuffer, primary, tex0, tex1, tex2, u_constants[i], fragmentPrimary, fragmentSecondary));
      color.rgb = combine(ops.x, a.rgb, b.rgb, c.rgb);
      color.a = combine(ops.y, vec3(aa), vec3(ab), vec3(ac)).x;
      if(ops.y > 5.5 && ops.y < 7.5) color.a = min(aa*ab*(ops.y < 6.5 ? 3.0 : 4.0),1.0);
      if(ops.x > 6.5 && ops.x < 7.5) color.a = color.r;
      color = floor(color*255.0+0.5)/255.0;
      color = clamp(color * vec4(ops.zzz, ops.w), 0.0, 1.0);
      if(i == 0) combBuffer = u_buffer;
      combBuffer.rgb = mix(combBuffer.rgb, previous.rgb, u_bufferWrites[i].x);
      combBuffer.a = mix(combBuffer.a, previous.a, u_bufferWrites[i].y);
      previous = color;
    }
  }
  float test = u_preview.z, ref = u_preview.w;
  if(test < 0.5) discard;
  if(test > 1.5 && test < 2.5 && abs(color.a-ref) > 0.001) discard;
  if(test > 2.5 && test < 3.5 && abs(color.a-ref) < 0.001) discard;
  if(test > 3.5 && test < 4.5 && color.a >= ref) discard;
  if(test > 4.5 && test < 5.5 && color.a > ref) discard;
  if(test > 5.5 && test < 6.5 && color.a <= ref) discard;
  if(test > 6.5 && color.a < ref) discard;
  if(u_pickColor.a > 1.5 && color.a <= 0.0) discard;
  if(u_edgeOptions.x>0.5){
    float mask=1.0;
    if(u_edgeOptions.w>=0.0){
      float slot=mod(u_edgeOptions.w,3.0);
      vec4 sampleColor=slot<0.5?tex0:slot<1.5?tex1:tex2;
      mask=u_edgeOptions.w<2.5?sampleColor.a:sampleColor.r;
    }
    if(mask<0.5)discard;
    vec3 edgeNormal=vec3(0.5);
    if(u_edgeOptions.y<0.5 || u_edgeOptions.y>4.5)edgeNormal=unitDirection(v_normal,vec3(0.0,1.0,0.0))*0.5+0.5;
    else if(u_edgeOptions.y<1.5)edgeNormal=v_edgeColor.rgb;
    else if(u_edgeOptions.y>3.5)edgeNormal=color.rgb;
    gl_FragColor=vec4(edgeNormal,u_edgeOptions.z);return;
  }
  if(u_fog.w>0.5){float depth=dot(u_fogDepth,vec4(v_worldPosition,1.0));float amount=clamp((depth-u_fog.x)/max(u_fog.y-u_fog.x,0.001),0.0,1.0);float strength=clamp(u_fog.z,0.0,2.0);amount=strength<=1.0?amount*strength:1.0-(1.0-amount)*(2.0-strength);color.rgb=mix(color.rgb,u_fogColor.rgb,amount);}
  if(u_selectionColor.a>0.5)color=vec4(u_selectionColor.rgb,1.0);
  gl_FragColor = u_pickColor.a > 0.5 ? vec4(u_pickColor.rgb,1.0) : color;
}

cbuffer SceneConstants : register(b0) {
  row_major float4x4 viewProjection;
  row_major float4x4 view;
  float4 material;
  float4 effects;
  float4 diffuseMask;
  float4 fogParameters; // start,end,native homogeneous-Z scale,bias
  float4 fogColor;      // RGB plus per-draw eligibility/enabled
  float4 textureUV;     // native source scale XY, origin XY
};
Texture2D image : register(t0);
Texture2D reflectionImage : register(t1);
SamplerState imageSampler : register(s0);
SamplerState reflectionSampler : register(s1);
struct VertexInput {float3 position : POSITION;float3 normal : NORMAL;float2 uv : TEXCOORD0;float3 reflectionPosition : TEXCOORD1;float4 diffuse : COLOR0;};
struct PixelInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
  float2 reflectionUV : TEXCOORD1;
  float4 diffuse : COLOR0;
  noperspective float3 fogDiffuse : COLOR1;
  noperspective float3 fogAdditive : TEXCOORD2;
};
struct EffectInput {float4 position : POSITION;float4 diffuse : COLOR0;float2 uv : TEXCOORD0;float cameraDepth : TEXCOORD1;};
struct UiInput {float3 position : POSITION;float2 uv : TEXCOORD0;float4 color : COLOR0;float2 reflectionUV : TEXCOORD1;};
//6D84C0 computes both colour operands per vertex, before projection. Keep
// the attenuated lighting and additive fog separate through interpolation.
// Computing fog from per-fragment W, or multiplying independently interpolated
// lighting and fog, changes triangles crossing the native range boundaries.
void vertexFog(float cameraDepth,float4 diffuse,out float3 fogDiffuse,out float3 fogAdditive) {
  fogDiffuse=diffuse.rgb;
  fogAdditive=float3(0,0,0);
  if(fogColor.w>0) {
    float depth=fogParameters.z*cameraDepth+fogParameters.w;
    float amount=0;
    if(depth>fogParameters.x) {
      amount=depth<fogParameters.y
          ? (depth-fogParameters.x)/(fogParameters.y-fogParameters.x)
          : 1;
    }
    fogDiffuse*=1-amount;
    fogAdditive=fogColor.rgb*amount;
  }
}
PixelInput ui_vertex_main(UiInput value) {
  PixelInput result;
  result.position=float4(value.position.xy/material.xy*float2(2,-2)+float2(-1,1),effects.x>0?value.position.z:0.5,1);
  result.uv=value.uv;result.diffuse=value.color;
  result.reflectionUV=value.reflectionUV;
  result.uv=(effects.w>0?result.reflectionUV:result.uv)*textureUV.xy+textureUV.zw;
  if(effects.z>0)result.diffuse.a=128.0/255.0;
  result.fogDiffuse=result.diffuse.rgb;
  result.fogAdditive=float3(0,0,0);
  return result;
}
float4 ui_pixel_main(PixelInput value) : SV_TARGET {
  float4 color=image.Sample(imageSampler,value.uv);
  if(effects.y>0) {
    float4 environment=reflectionImage.Sample(reflectionSampler,value.reflectionUV);
    color.rgb=lerp(environment.rgb,color.rgb,environment.a);
  } else {
    color*=value.diffuse;
    color.rgb=saturate(color.rgb*material.w);
    if(effects.z>0)color.rgb*=reflectionImage.Sample(reflectionSampler,value.reflectionUV).rgb;
  }
  clip(color.a-material.z);
  return color;
}
PixelInput effect_main(EffectInput value) {
  PixelInput result;result.position=value.position;result.diffuse=value.diffuse;
  result.uv=value.uv*textureUV.xy+textureUV.zw;result.reflectionUV=float2(0.5,0.5);
  vertexFog(value.cameraDepth,result.diffuse,result.fogDiffuse,result.fogAdditive);
  return result;
}
float3 normalizedOrZero(float3 value) {float magnitude=length(value);return magnitude>0 && isfinite(magnitude)?value/magnitude:float3(0,0,0);}
float2 reflectionUV(float3 position,float3 normal) {
  position=normalizedOrZero(position);normal=normalizedOrZero(normal);
  float3 reflected=position-2*dot(position,normal)*normal+float3(0,0,1);
  float denominator=2*length(reflected);
  return denominator>0 && isfinite(denominator)?reflected.xy/denominator+0.5:float2(0.5,0.5);
}
PixelInput vertex_main(VertexInput value) {
  PixelInput result;
  result.position=mul(float4(value.position,1),viewProjection);
  result.uv=value.uv;
  result.reflectionUV=float2(0.5,0.5);
  if(material.y>0 || material.z>0)result.reflectionUV=reflectionUV(mul(float4(value.reflectionPosition,1),view).xyz,mul(float4(value.normal,0),view).xyz);
  if(material.y>0)result.uv=result.reflectionUV;
  result.uv=result.uv*textureUV.xy+textureUV.zw;
  result.diffuse=effects.x>0?diffuseMask:value.diffuse;
  if(effects.z>0)result.diffuse.a=128.0/255.0;
  vertexFog(mul(float4(value.position,1),view).z,result.diffuse,
            result.fogDiffuse,result.fogAdditive);
  return result;
}
float4 pixel_main(PixelInput value) : SV_TARGET {
  float4 color=image.Sample(imageSampler,value.uv);
  if(effects.y>0) {
    // Native80000000 combiner:stage0 selects texture, stage1 blends
    // CURRENT and TEXTURE with stage1 texture alpha; alpha stays CURRENT.
    float4 environment=reflectionImage.Sample(reflectionSampler,value.reflectionUV);
    color.rgb=lerp(environment.rgb,color.rgb,environment.a);
  } else {
    // Fog changes RGB interpolation only; the original diffuse alpha stays
    // on its unchanged varying and retains the same alpha-test/blend result.
    color*=float4(fogColor.w>0?value.fogDiffuse:value.diffuse.rgb,value.diffuse.a);
    color.rgb=saturate(color.rgb*material.w);
    if(material.z>0)color.rgb*=reflectionImage.Sample(reflectionSampler,value.reflectionUV).rgb;
  }
  clip(color.a-material.x);
  // Native specular RGB is added after texture stages, before framebuffer
  // blending. Fog neither replaces alpha nor changes the alpha-test result.
  color.rgb=saturate(color.rgb+value.fogAdditive);
  return color;
}

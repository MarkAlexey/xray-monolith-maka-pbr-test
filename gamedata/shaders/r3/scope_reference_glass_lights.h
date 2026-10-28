// The two strongest lights that reach the lens owner (svp_lens_lighting.cpp) and the light they bring.
#ifndef SCOPE_REFERENCE_GLASS_LIGHTS_H
#define SCOPE_REFERENCE_GLASS_LIGHTS_H
float4 scope_glass_light0_position,scope_glass_light0_color,scope_glass_light0_direction;
float4 scope_glass_light1_position,scope_glass_light1_color,scope_glass_light1_direction;
// Light arriving from a range- and cone-limited light at squared distance d2, from direction L
// (towards the light).
float3 scope_reference_light_incident(float4 color,float4 direction,float range,float d2,float3 L)
{
 float attenuation=saturate(1-d2/(range*range));
 attenuation*=attenuation;
 if(direction.w>-1){
  float cone=saturate((dot(-L,direction.xyz)-direction.w)/max(1-direction.w,1.e-6));
  attenuation*=cone*cone;
 }
 return color.rgb*(saturate(color.w)*attenuation);
}
#endif

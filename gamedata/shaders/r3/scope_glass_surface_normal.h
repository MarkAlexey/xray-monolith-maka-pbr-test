#ifndef SCOPE_GLASS_SURFACE_NORMAL_H
#define SCOPE_GLASS_SURFACE_NORMAL_H
#include "scope_glass_source_profile.h"
float4 scope_glass_surface_control;
float4 scope_glass_surface_center0, scope_glass_surface_center1, scope_glass_surface_center2, scope_glass_surface_center3;
float4 scope_glass_surface_axis0, scope_glass_surface_axis1, scope_glass_surface_axis2, scope_glass_surface_axis3;
float4 scope_glass_surface_right0, scope_glass_surface_right1, scope_glass_surface_right2, scope_glass_surface_right3;
float4 scope_glass_surface_up0, scope_glass_surface_up1, scope_glass_surface_up2, scope_glass_surface_up3;
// Normal of the nearest registered lens surface at this point, from its
// radial tilt profile. Returns meshNormal outside every surface.
float3 scope_glass_surface_normal(float3 world, float3 meshNormal)
{
 float4 centers[4]={scope_glass_surface_center0,scope_glass_surface_center1,scope_glass_surface_center2,scope_glass_surface_center3};
 float4 axes[4]={scope_glass_surface_axis0,scope_glass_surface_axis1,scope_glass_surface_axis2,scope_glass_surface_axis3};
 float4 rights[4]={scope_glass_surface_right0,scope_glass_surface_right1,scope_glass_surface_right2,scope_glass_surface_right3};
 float4 ups[4]={scope_glass_surface_up0,scope_glass_surface_up1,scope_glass_surface_up2,scope_glass_surface_up3};
 float best=1e20; float3 result=meshNormal;
 [unroll] for(int i=0;i<4;i++)
 {
  if(i>=scope_glass_surface_control.x || centers[i].w<=1e-7) continue;
  float3 d=world-centers[i].xyz;
  float2 p=float2(dot(d,rights[i].xyz),dot(d,ups[i].xyz))/centers[i].w;
  float radius=length(p);
  float axial=abs(dot(d,axes[i].xyz));
  if(axial>axes[i].w+centers[i].w*.02) continue;
  float distance=axial/centers[i].w;
  if(radius>1.05 || distance>=best) continue;
  best=distance;
  float3 radial=radius>1e-6 ? (rights[i].xyz*p.x+ups[i].xyz*p.y)/radius : rights[i].xyz;
  // The profile ends in a bevel beyond r=0.9807853. On this mesh that
  // 61-degree face would show as a bright pixel ring, so stop before it.
  float tilt=scope_glass_source_rear_tilt(min(saturate(radius),0.9807853));
  result=normalize(axes[i].xyz*cos(tilt)+radial*sin(tilt));
 }
 return result;
}
#endif

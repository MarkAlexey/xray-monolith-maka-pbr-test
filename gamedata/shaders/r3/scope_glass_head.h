// The player in the scope glass. Part of the glass's reflection is the player's own head and shoulders,
// which the VSLR capture (static level only) lacks: the player's body model, drawn from in front of the
// eye into vslr_mirror, shows there. scope_glass_body holds a low-detail figure as spheres (world
// centre, radius; scope_glass_head.cpp), the head first, unused entries with zero radius: it hides the
// sun and lights behind the player, and stands in for the body while no mirror image is current.
// scope_glass_head_params: x the figure's albedo, y 1 while the glass shows the player, z how far the
// player's silhouette dims the optical image (r__svp_lens_shooter_ads).
#ifndef SCOPE_GLASS_HEAD_H
#define SCOPE_GLASS_HEAD_H
#define SCOPE_GLASS_BODY_SPHERES 10
float4 scope_glass_body[SCOPE_GLASS_BODY_SPHERES];
float4 scope_glass_head_params;
Texture2D vslr_mirror;
float4 scope_glass_mirror_origin; // xyz: camera, w: 1 when the mirror image is current
float4 scope_glass_mirror_right, scope_glass_mirror_up;
float4 scope_glass_mirror_forward; // w: tangent of the half field of view
float4 scope_glass_mirror_head; // xyz: the body's head (at the camera), w: depth of the face in front of it
// Half angle of the sun's and lamps' discs, for the figure's shadow on the glints.
static const float scope_glass_light_lobe=.005;
// The body along a reflected ray from position, for a reflection lobe of half angle lobe: premultiplied
// colour and coverage. The mirror image stands as a plane through the face, square to its camera; the
// ray meets that plane and is projected into the image.
float4 scope_glass_mirror_ray(float3 position,float3 direction,float lobe)
{
 // The mirror camera faces the player: forward points from it back towards the face.
 float3 forward=scope_glass_mirror_forward.xyz;
 float toward=dot(direction,forward);
 if(toward<=1.e-3) return 0;
 float3 face=scope_glass_mirror_head.xyz-forward*scope_glass_mirror_head.w;
 float distance=dot(face-position,forward)/toward;
 if(distance<=0) return 0;
 float3 fromCamera=position+direction*distance-scope_glass_mirror_origin.xyz;
 float z=dot(fromCamera,forward);
 if(z<=.01) return 0;
 float2 uv=float2(dot(fromCamera,scope_glass_mirror_right.xyz),dot(fromCamera,scope_glass_mirror_up.xyz))
  /(z*scope_glass_mirror_forward.w);
 float edge=max(abs(uv.x),abs(uv.y));
 if(edge>=1) return 0;
 uint width,height,levels;
 vslr_mirror.GetDimensions(0,width,height,levels);
 // The lobe's footprint where it meets the face against a texel there.
 float texel=2*z*scope_glass_mirror_forward.w/float(width);
 float footprint=2*tan(lobe)*distance;
 float lod=clamp(log2(max(footprint/texel,1)),0,float(levels)-1);
 // The image fades out over its last 5% (or the lobe's half width) instead of ending in a hard line.
 float border=max(.05,.5*footprint/(z*scope_glass_mirror_forward.w));
 return vslr_mirror.SampleLevel(vslr_linear,uv*float2(.5,-.5)+.5,lod)*saturate((1-edge)/border);
}

// Solid angle of a cone of half angle a (1-cos a, without the 2 pi), stable for small angles.
float scope_glass_cap(float a)
{
 float s=sin(.5*a);
 return 2*s*s;
}

// Shared solid angle of two cones of half angles a and b whose axes are apart by angle distance, in
// units of 2 pi: the smooth spherical cap intersection approximation used for specular occlusion.
float scope_glass_cap_overlap(float a,float b,float distance)
{
 float inner=abs(a-b);
 if(distance>=a+b) return 0;
 float overlap=scope_glass_cap(min(a,b));
 if(distance<=inner) return overlap;
 return overlap*smoothstep(0,1,1-(distance-inner)/max(a+b-inner,1.e-5));
}

// Share of a cone of half angle lobe about direction, seen from position, that a sphere blocks.
float scope_glass_sphere_blocked(float4 sphere,float3 position,float3 direction,float lobe)
{
 float3 toSphere=sphere.xyz-position;
 float distance=length(toSphere);
 float size=distance>sphere.w ? asin(sphere.w/distance) : 1.5707963;
 float apart=acos(clamp(dot(toSphere,direction)/max(distance,1.e-5),-1,1));
 lobe=clamp(lobe,1.e-3,1.5707963);
 return saturate(scope_glass_cap_overlap(lobe,size,apart)/scope_glass_cap(lobe));
}

// Share of the lobe the figure blocks, and the figure's mean surface normal there (towards position).
float scope_glass_body_blocked(float3 position,float3 direction,float lobe,out float3 normal)
{
 float open=1;
 normal=0;
 [unroll] for(int i=0;i<SCOPE_GLASS_BODY_SPHERES;++i)
 {
  float4 sphere=scope_glass_body[i];
  if(sphere.w<=0) continue;
  float blocked=scope_glass_sphere_blocked(sphere,position,direction,lobe);
  open*=1-blocked;
  normal+=blocked*normalize(position-sphere.xyz);
 }
 normal=dot(normal,normal)>1.e-12 ? normalize(normal) : -direction;
 return 1-open;
}

// Half angle of a reflection lobe of the given lookup roughness: half the footprint of the capture
// mip it samples (scope_glass_capture_lod: 2^lod = 2 x width x r^2 texels of a 90 degree face), up to
// the whole face.
float scope_glass_head_lobe(float roughness)
{
 float r=saturate(roughness);
 return min(1.5707963*r*r,.7853982);
}

// A light at L from position (distance d) is hidden by each sphere that lies between; lights at the
// head itself (a head torch) are not.
float scope_glass_head_light_visibility(float3 position,float3 L,float d)
{
 float4 head=scope_glass_body[0];
 if(head.w<=0 || length(position+L*d-head.xyz)<=2*head.w) return 1;
 float visible=1;
 [unroll] for(int i=0;i<SCOPE_GLASS_BODY_SPHERES;++i)
 {
  float4 sphere=scope_glass_body[i];
  float3 toSphere=sphere.xyz-position;
  if(sphere.w>0 && d*d>dot(toSphere,toSphere))
   visible*=1-scope_glass_sphere_blocked(sphere,position,L,scope_glass_light_lobe);
 }
 return visible;
}
#endif

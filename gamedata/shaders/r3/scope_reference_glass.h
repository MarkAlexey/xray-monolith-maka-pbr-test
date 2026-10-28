#ifndef SCOPE_REFERENCE_GLASS_H
#define SCOPE_REFERENCE_GLASS_H
#include "scope_reference_optics.h"
float4 scope_reference_glass_cb[19];
static const float4 sourceGlassNormalOverride=float4(0,0,0,1);
static const float4 sourceGlassRoughnessOverride=float4(0,1,0,0);
static const float sourceGlassMipBias=0;
Texture2D scope_reference_glass_spots,scope_reference_glass_dirt;
#ifndef SCOPE_REFERENCE_GLASS_SAMPLER_DECLARED
SamplerState scope_reference_glass_sampler;
#endif
float4 ref_glass_sample_4(float2 uv,float bias){return scope_reference_glass_spots.SampleBias(scope_reference_glass_sampler,uv,bias);}
float4 ref_glass_sample_5(float2 uv,float bias){return scope_reference_glass_dirt.SampleBias(scope_reference_glass_sampler,uv,bias);}
struct ScopeReferenceGlass {float3 baseColor,worldNormal,emission,coatingColor;float metallic,specular,roughness;};
ScopeReferenceGlass scope_reference_glass(float2 uv,float3 tangent,float3 normal,float3 bitangent,float3 view){
float4 v0=float4(tangent,0),v1=float4(normal,1),v2=float4(uv,0,0);
uint4 r0=asuint(float4(bitangent,0)),r1=0,r2=asuint(float4(normalize(view),0)),r3=0,r4=0,r5=0,r6=0,r7=0,r8=0,r9=0,r10=0,r11=0;
// L74: add [precise(xy)] r3.xy, v2.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r3.xy=(asuint(ref_f4(v2.xyxx)+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L75: dp2 [precise(w)] r0.w, r3.xyxx, r3.xyxx
r0.w=(asuint(ref_f4(dot(asfloat(ref_u4(r3.xyxx)).xy,asfloat(ref_u4(r3.xyxx)).xy)))).w;
// L76: sqrt [precise(w)] r0.w, r0.w
r0.w=(asuint(sqrt(asfloat(ref_u4(r0.w))))).w;
// L77: add [precise(w)] r1.w, -cb1[0].y, cb1[0].x
r1.w=(asuint((-ref_f4(scope_reference_glass_cb[0].y))+ref_f4(scope_reference_glass_cb[0].x))).w;
// L78: add [precise(w)] r2.w, r0.w, -cb1[0].y
r2.w=(asuint(asfloat(ref_u4(r0.w))+(-ref_f4(scope_reference_glass_cb[0].y)))).w;
// L79: div [precise(w)] r1.w, l(1.000000, 1.000000, 1.000000, 1.000000), r1.w
r1.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r1.w)))).w;
// L80: mul_sat [precise(w)] r1.w, r1.w, r2.w
r1.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r1.w))*asfloat(ref_u4(r2.w)),ref_f4(0.)),ref_f4(1.)))).w;
// L81: mul [precise(w)] r2.w, r1.w, l(-2.000000)
r2.w=(asuint(asfloat(ref_u4(r1.w))*ref_f4(-2.000000))).w;
// L82: add [precise(w)] r2.w, r2.w, l(3.000000)
r2.w=(asuint(asfloat(ref_u4(r2.w))+ref_f4(3.000000))).w;
// L83: mul [precise(w)] r1.w, r1.w, r1.w
r1.w=(asuint(asfloat(ref_u4(r1.w))*asfloat(ref_u4(r1.w)))).w;
// L84: mul [precise(w)] r1.w, r1.w, r2.w
r1.w=(asuint(asfloat(ref_u4(r1.w))*asfloat(ref_u4(r2.w)))).w;
// L85: ge [precise(w)] r2.w, l(0.000000), r1.w
r2.w=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r1.w))))).w;
// L86: log [precise(w)] r1.w, r1.w
r1.w=(asuint(dx_log(asfloat(ref_u4(r1.w))))).w;
// L87: mul [precise(w)] r1.w, r1.w, cb1[0].z
r1.w=(asuint(asfloat(ref_u4(r1.w))*ref_f4(scope_reference_glass_cb[0].z))).w;
// L88: exp [precise(w)] r1.w, r1.w
r1.w=(asuint(dx_exp(asfloat(ref_u4(r1.w))))).w;
// L89: mul [precise(xy)] r3.xy, r1.wwww, r3.xyxx
r3.xy=(asuint(asfloat(ref_u4(r1.wwww))*asfloat(ref_u4(r3.xyxx)))).xy;
// L90: mul [precise(xy)] r3.xy, r3.xyxx, cb1[0].wwww
r3.xy=(asuint(asfloat(ref_u4(r3.xyxx))*ref_f4(scope_reference_glass_cb[0].wwww))).xy;
// L91: movc [precise(xy)] r3.xy, r2.wwww, l(0,0,0,0), r3.xyxx
{uint4 c=ref_u4(r2.wwww),yes=ref_u4(uint(0),uint(0),uint(0),uint(0)),no=ref_u4(r3.xyxx);r3.xy=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).xy;}
// L92: add [precise(xy)] r4.xy, v2.yxyy, -cb1[1].wzww
r4.xy=(asuint(ref_f4(v2.yxyy)+(-ref_f4(scope_reference_glass_cb[1].wzww)))).xy;
// L93: max [precise(zw)] r4.zw, cb1[1].yyyx, l(0.000000, 0.000000, 0.000010, 0.000010)
r4.zw=(asuint(dx_max(ref_f4(scope_reference_glass_cb[1].yyyx),ref_f4(0.000000,0.000000,0.000010,0.000010)))).zw;
// L94: div [precise(xy)] r4.xy, r4.xyxx, r4.zwzz
r4.xy=(asuint(asfloat(ref_u4(r4.xyxx))/asfloat(ref_u4(r4.zwzz)))).xy;
// L95: add [precise(xy)] r4.xy, r4.xyxx, cb1[1].wzww
r4.xy=(asuint(asfloat(ref_u4(r4.xyxx))+ref_f4(scope_reference_glass_cb[1].wzww))).xy;
// L96: mul [precise(w)] r1.w, cb1[2].z, l(0.017453)
r1.w=(asuint(ref_f4(scope_reference_glass_cb[2].z)*ref_f4(0.017453))).w;
// L97: sincos [precise(x)] r5.x, r6.x, r1.w
{float4 angle=asfloat(ref_u4(r1.w));r5.x=(asuint(sin(angle))).x;r6.x=(asuint(cos(angle))).x;}
// L98: add [precise(xy)] r4.xy, r4.xyxx, -cb1[3].yxyy
r4.xy=(asuint(asfloat(ref_u4(r4.xyxx))+(-ref_f4(scope_reference_glass_cb[3].yxyy)))).xy;
// L99: mul [precise(zw)] r4.zw, r6.xxxx, r4.yyyx
r4.zw=(asuint(asfloat(ref_u4(r6.xxxx))*asfloat(ref_u4(r4.yyyx)))).zw;
// L100: mul [precise(xy)] r4.xy, r5.xxxx, r4.xyxx
r4.xy=(asuint(asfloat(ref_u4(r5.xxxx))*asfloat(ref_u4(r4.xyxx)))).xy;
// L101: add [precise(x)] r5.x, -r4.x, r4.z
r5.x=(asuint((-asfloat(ref_u4(r4.x)))+asfloat(ref_u4(r4.z)))).x;
// L102: add [precise(y)] r5.y, r4.w, r4.y
r5.y=(asuint(asfloat(ref_u4(r4.w))+asfloat(ref_u4(r4.y)))).y;
// L103: add [precise(xy)] r4.xy, r5.xyxx, cb1[2].xyxx
r4.xy=(asuint(asfloat(ref_u4(r5.xyxx))+ref_f4(scope_reference_glass_cb[2].xyxx))).xy;
// L104: round_ni [precise(zw)] r4.zw, r4.xxxy
r4.zw=(asuint(floor(asfloat(ref_u4(r4.xxxy))))).zw;
// L105: mul [precise(zw)] r4.zw, |r4.zzzw|, l(0.000000, 0.000000, 0.500000, 0.500000)
r4.zw=(asuint(abs(asfloat(ref_u4(r4.zzzw)))*ref_f4(0.000000,0.000000,0.500000,0.500000))).zw;
// L106: ge [precise(xy)] r5.xy, r4.zwzz, -r4.zwzz
r5.xy=(ref_mask(ref_ge(asfloat(ref_u4(r4.zwzz)),(-asfloat(ref_u4(r4.zwzz)))))).xy;
// L107: frc [precise] r4.xyzw, r4.xyzw
r4.xyzw=(asuint(frac(asfloat(ref_u4(r4.xyzw))))).xyzw;
// L108: movc [precise(zw)] r4.zw, r5.xxxy, r4.zzzw, -r4.zzzw
{uint4 c=ref_u4(r5.xxxy),yes=ref_u4(r4.zzzw),no=(ref_u4(r4.zzzw)^ref_u4(0x80000000u));r4.zw=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).zw;}
// L109: mul [precise(zw)] r4.zw, r4.zzzw, l(0.000000, 0.000000, 2.000000, 2.000000)
r4.zw=(asuint(asfloat(ref_u4(r4.zzzw))*ref_f4(0.000000,0.000000,2.000000,2.000000))).zw;
// L110: add [precise(xy)] r5.xy, -r4.xyxx, l(1.000000, 1.000000, 0.000000, 0.000000)
r5.xy=(asuint((-asfloat(ref_u4(r4.xyxx)))+ref_f4(1.000000,1.000000,0.000000,0.000000))).xy;
// L111: add [precise(xy)] r5.xy, -r4.xyxx, r5.xyxx
r5.xy=(asuint((-asfloat(ref_u4(r4.xyxx)))+asfloat(ref_u4(r5.xyxx)))).xy;
// L112: mul [precise(zw)] r4.zw, r4.zzzw, r5.xxxy
r4.zw=(asuint(asfloat(ref_u4(r4.zzzw))*asfloat(ref_u4(r5.xxxy)))).zw;
// L113: add [precise(xy)] r4.xy, r4.zwzz, r4.xyxx
r4.xy=(asuint(asfloat(ref_u4(r4.zwzz))+asfloat(ref_u4(r4.xyxx)))).xy;
// L114: sample_b_indexable [precise(xyz)](texture2d)(float,float,float,float) r4.xyz, r4.xyxx, t4.xyzw, s1, cb0[163].y
r4.xyz=asuint(ref_glass_sample_4(asfloat(r4.xyxx).xy,sourceGlassMipBias).xyzw).xyz;
// L115: add [precise(w)] r1.w, -cb1[3].w, cb1[3].z
r1.w=(asuint((-ref_f4(scope_reference_glass_cb[3].w))+ref_f4(scope_reference_glass_cb[3].z))).w;
// L116: add [precise(w)] r2.w, r0.w, -cb1[3].w
r2.w=(asuint(asfloat(ref_u4(r0.w))+(-ref_f4(scope_reference_glass_cb[3].w)))).w;
// L117: div [precise(w)] r1.w, l(1.000000, 1.000000, 1.000000, 1.000000), r1.w
r1.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r1.w)))).w;
// L118: mul_sat [precise(w)] r1.w, r1.w, r2.w
r1.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r1.w))*asfloat(ref_u4(r2.w)),ref_f4(0.)),ref_f4(1.)))).w;
// L119: mul [precise(w)] r2.w, r1.w, l(-2.000000)
r2.w=(asuint(asfloat(ref_u4(r1.w))*ref_f4(-2.000000))).w;
// L120: add [precise(w)] r2.w, r2.w, l(3.000000)
r2.w=(asuint(asfloat(ref_u4(r2.w))+ref_f4(3.000000))).w;
// L121: mul [precise(w)] r1.w, r1.w, r1.w
r1.w=(asuint(asfloat(ref_u4(r1.w))*asfloat(ref_u4(r1.w)))).w;
// L122: mul [precise(w)] r1.w, r1.w, r2.w
r1.w=(asuint(asfloat(ref_u4(r1.w))*asfloat(ref_u4(r2.w)))).w;
// L123: ge [precise(w)] r2.w, l(0.000000), r1.w
r2.w=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r1.w))))).w;
// L124: log [precise(w)] r1.w, r1.w
r1.w=(asuint(dx_log(asfloat(ref_u4(r1.w))))).w;
// L125: mul [precise(w)] r1.w, r1.w, cb1[4].x
r1.w=(asuint(asfloat(ref_u4(r1.w))*ref_f4(scope_reference_glass_cb[4].x))).w;
// L126: exp [precise(w)] r1.w, r1.w
r1.w=(asuint(dx_exp(asfloat(ref_u4(r1.w))))).w;
// L127: add [precise(w)] r1.w, r1.w, l(-1.000000)
r1.w=(asuint(asfloat(ref_u4(r1.w))+ref_f4(-1.000000))).w;
// L128: movc [precise(w)] r1.w, r2.w, l(-1.000000), r1.w
{uint4 c=ref_u4(r2.w),yes=ref_u4(asuint(-1.000000)),no=ref_u4(r1.w);r1.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L129: mul [precise(w)] r1.w, r1.w, cb1[4].y
r1.w=(asuint(asfloat(ref_u4(r1.w))*ref_f4(scope_reference_glass_cb[4].y))).w;
// L130: add [precise(w)] r1.w, r1.w, l(1.000000)
r1.w=(asuint(asfloat(ref_u4(r1.w))+ref_f4(1.000000))).w;
// L131: add [precise(w)] r2.w, -cb1[4].w, cb1[4].z
r2.w=(asuint((-ref_f4(scope_reference_glass_cb[4].w))+ref_f4(scope_reference_glass_cb[4].z))).w;
// L132: add [precise(w)] r3.w, r0.w, -cb1[4].w
r3.w=(asuint(asfloat(ref_u4(r0.w))+(-ref_f4(scope_reference_glass_cb[4].w)))).w;
// L133: div [precise(w)] r2.w, l(1.000000, 1.000000, 1.000000, 1.000000), r2.w
r2.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r2.w)))).w;
// L134: mul_sat [precise(w)] r2.w, r2.w, r3.w
r2.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r3.w)),ref_f4(0.)),ref_f4(1.)))).w;
// L135: mul [precise(w)] r3.w, r2.w, l(-2.000000)
r3.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(-2.000000))).w;
// L136: add [precise(w)] r3.w, r3.w, l(3.000000)
r3.w=(asuint(asfloat(ref_u4(r3.w))+ref_f4(3.000000))).w;
// L137: mul [precise(w)] r2.w, r2.w, r2.w
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r2.w)))).w;
// L138: mul [precise(w)] r2.w, r2.w, r3.w
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r3.w)))).w;
// L139: ge [precise(w)] r3.w, l(0.000000), r2.w
r3.w=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r2.w))))).w;
// L140: log [precise(w)] r2.w, r2.w
r2.w=(asuint(dx_log(asfloat(ref_u4(r2.w))))).w;
// L141: mul [precise(w)] r2.w, r2.w, cb1[5].x
r2.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(scope_reference_glass_cb[5].x))).w;
// L142: exp [precise(w)] r2.w, r2.w
r2.w=(asuint(dx_exp(asfloat(ref_u4(r2.w))))).w;
// L143: add [precise(w)] r2.w, r2.w, l(-1.000000)
r2.w=(asuint(asfloat(ref_u4(r2.w))+ref_f4(-1.000000))).w;
// L144: movc [precise(w)] r2.w, r3.w, l(-1.000000), r2.w
{uint4 c=ref_u4(r3.w),yes=ref_u4(asuint(-1.000000)),no=ref_u4(r2.w);r2.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L145: mul [precise(w)] r2.w, r2.w, cb1[5].y
r2.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(scope_reference_glass_cb[5].y))).w;
// L146: add [precise(w)] r2.w, r2.w, l(1.000000)
r2.w=(asuint(asfloat(ref_u4(r2.w))+ref_f4(1.000000))).w;
// L147: mul [precise(w)] r1.w, r1.w, r2.w
r1.w=(asuint(asfloat(ref_u4(r1.w))*asfloat(ref_u4(r2.w)))).w;
// L148: add [precise(w)] r1.w, -r1.w, l(1.000000)
r1.w=(asuint((-asfloat(ref_u4(r1.w)))+ref_f4(1.000000))).w;
// L149: mul [precise(w)] r2.w, r1.w, cb1[5].z
r2.w=(asuint(asfloat(ref_u4(r1.w))*ref_f4(scope_reference_glass_cb[5].z))).w;
// L150: max [precise(w)] r2.w, r2.w, l(0.000100)
r2.w=(asuint(dx_max(asfloat(ref_u4(r2.w)),ref_f4(0.000100)))).w;
// L151: ge [precise(xyz)] r5.xyz, l(0.000000, 0.000000, 0.000000, 0.000000), r4.xyzx
r5.xyz=(ref_mask(ref_ge(ref_f4(0.000000,0.000000,0.000000,0.000000),asfloat(ref_u4(r4.xyzx))))).xyz;
// L152: log [precise(xyz)] r4.xyz, r4.xyzx
r4.xyz=(asuint(dx_log(asfloat(ref_u4(r4.xyzx))))).xyz;
// L153: mul [precise(xyz)] r4.xyz, r2.wwww, r4.xyzx
r4.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r4.xyzx)))).xyz;
// L154: exp [precise(xyz)] r4.xyz, r4.xyzx
r4.xyz=(asuint(dx_exp(asfloat(ref_u4(r4.xyzx))))).xyz;
// L155: movc [precise(xyz)] r4.xyz, r5.xyzx, l(0,0,0,0), r4.xyzx
{uint4 c=ref_u4(r5.xyzx),yes=ref_u4(uint(0),uint(0),uint(0),uint(0)),no=ref_u4(r4.xyzx);r4.xyz=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).xyz;}
// L156: mul r4.xyz, r4.xyzx, cb1[5].wwww
r4.xyz=(asuint(asfloat(ref_u4(r4.xyzx))*ref_f4(scope_reference_glass_cb[5].wwww))).xyz;
// L157: min r4.xyz, r4.xyzx, cb1[6].xxxx
r4.xyz=(asuint(dx_min(asfloat(ref_u4(r4.xyzx)),ref_f4(scope_reference_glass_cb[6].xxxx)))).xyz;
// L158: mov r3.z, l(1.000000)
r3.z=(ref_u4(asuint(1.000000))).z;
// L159: mad r3.xyz, r4.xyzx, cb1[6].yyyy, r3.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r4.xyzx))*ref_f4(scope_reference_glass_cb[6].yyyy)+asfloat(ref_u4(r3.xyzx)))).xyz;
// L160: mad r3.xyz, r3.xyzx, cb0[158].wwww, cb0[158].xyzx
r3.xyz=(asuint(asfloat(ref_u4(r3.xyzx))*ref_f4(sourceGlassNormalOverride.wwww)+ref_f4(sourceGlassNormalOverride.xyzx))).xyz;
// L161: dp3 r2.w, r3.xyzx, r3.xyzx
r2.w=(asuint(ref_f4(dot(asfloat(ref_u4(r3.xyzx)).xyz,asfloat(ref_u4(r3.xyzx)).xyz)))).w;
// L162: rsq r2.w, r2.w
r2.w=(asuint(rsqrt(asfloat(ref_u4(r2.w))))).w;
// L163: mul r3.xyz, r2.wwww, r3.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r3.xyzx)))).xyz;
// L164: mul r4.xyz, r0.xyzx, r3.yyyy
r4.xyz=(asuint(asfloat(ref_u4(r0.xyzx))*asfloat(ref_u4(r3.yyyy)))).xyz;
// L165: mad r3.xyw, r3.xxxx, v0.xyxz, r4.xyxz
r3.xyw=(asuint(asfloat(ref_u4(r3.xxxx))*ref_f4(v0.xyxz)+asfloat(ref_u4(r4.xyxz)))).xyw;
// L166: mad r3.xyz, r3.zzzz, v1.xyzx, r3.xywx
r3.xyz=(asuint(asfloat(ref_u4(r3.zzzz))*ref_f4(v1.xyzx)+asfloat(ref_u4(r3.xywx)))).xyz;
// L167: dp3 r2.w, r3.xyzx, r3.xyzx
r2.w=(asuint(ref_f4(dot(asfloat(ref_u4(r3.xyzx)).xyz,asfloat(ref_u4(r3.xyzx)).xyz)))).w;
// L168: rsq r2.w, r2.w
r2.w=(asuint(rsqrt(asfloat(ref_u4(r2.w))))).w;
// L169: mul r3.xyz, r2.wwww, r3.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r3.xyzx)))).xyz;
// L170: mul r4.xyz, cb1[6].zzzz, cb1[7].xyzx
r4.xyz=(asuint(ref_f4(scope_reference_glass_cb[6].zzzz)*ref_f4(scope_reference_glass_cb[7].xyzx))).xyz;
// L171: add [precise(w)] r2.w, cb1[7].w, -cb1[8].x
r2.w=(asuint(ref_f4(scope_reference_glass_cb[7].w)+(-ref_f4(scope_reference_glass_cb[8].x)))).w;
// L172: add [precise(w)] r3.w, r0.w, -cb1[8].x
r3.w=(asuint(asfloat(ref_u4(r0.w))+(-ref_f4(scope_reference_glass_cb[8].x)))).w;
// L173: div [precise(w)] r2.w, l(1.000000, 1.000000, 1.000000, 1.000000), r2.w
r2.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r2.w)))).w;
// L174: mul_sat [precise(w)] r2.w, r2.w, r3.w
r2.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r3.w)),ref_f4(0.)),ref_f4(1.)))).w;
// L175: mul [precise(w)] r3.w, r2.w, l(-2.000000)
r3.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(-2.000000))).w;
// L176: add [precise(w)] r3.w, r3.w, l(3.000000)
r3.w=(asuint(asfloat(ref_u4(r3.w))+ref_f4(3.000000))).w;
// L177: mul [precise(w)] r2.w, r2.w, r2.w
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r2.w)))).w;
// L178: mul [precise(w)] r2.w, r2.w, r3.w
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r3.w)))).w;
// L179: add [precise(xyz)] r5.xyz, cb1[8].yzwy, -cb1[9].xyzx
r5.xyz=(asuint(ref_f4(scope_reference_glass_cb[8].yzwy)+(-ref_f4(scope_reference_glass_cb[9].xyzx)))).xyz;
// L180: mul [precise(xyz)] r5.xyz, r2.wwww, r5.xyzx
r5.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r5.xyzx)))).xyz;
// L181: add [precise(xyz)] r5.xyz, r5.xyzx, cb1[9].xyzx
r5.xyz=(asuint(asfloat(ref_u4(r5.xyzx))+ref_f4(scope_reference_glass_cb[9].xyzx))).xyz;
// L182: mul [precise(xyz)] r5.xyz, r5.xyzx, cb1[9].wwww
r5.xyz=(asuint(asfloat(ref_u4(r5.xyzx))*ref_f4(scope_reference_glass_cb[9].wwww))).xyz;
// L183: mul [precise(w)] r2.w, cb1[10].x, l(0.050000)
r2.w=(asuint(ref_f4(scope_reference_glass_cb[10].x)*ref_f4(0.050000))).w;
// L184: add [precise(w)] r2.w, r2.w, l(-0.025000)
r2.w=(asuint(asfloat(ref_u4(r2.w))+ref_f4(-0.025000))).w;
// L185: dp3 [precise(x)] r6.x, v0.xyzx, r2.xyzx
r6.x=(asuint(ref_f4(dot(ref_f4(v0.xyzx).xyz,asfloat(ref_u4(r2.xyzx)).xyz)))).x;
// L186: dp3 [precise(y)] r6.y, r0.xyzx, r2.xyzx
r6.y=(asuint(ref_f4(dot(asfloat(ref_u4(r0.xyzx)).xyz,asfloat(ref_u4(r2.xyzx)).xyz)))).y;
// L187: mul [precise(xy)] r0.xy, r2.wwww, r6.xyxx
r0.xy=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r6.xyxx)))).xy;
// L188: add [precise(xy)] r0.xy, r0.xyxx, v2.xyxx
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))+ref_f4(v2.xyxx))).xy;
// L189: mul [precise(xy)] r2.xy, r6.xyxx, l(0.975000, 0.975000, 0.000000, 0.000000)
r2.xy=(asuint(asfloat(ref_u4(r6.xyxx))*ref_f4(0.975000,0.975000,0.000000,0.000000))).xy;
// L190: add [precise(xy)] r2.xy, r2.xyxx, v2.xyxx
r2.xy=(asuint(asfloat(ref_u4(r2.xyxx))+ref_f4(v2.xyxx))).xy;
// L191: add [precise(z)] r0.z, -cb1[10].z, cb1[10].y
r0.z=(asuint((-ref_f4(scope_reference_glass_cb[10].z))+ref_f4(scope_reference_glass_cb[10].y))).z;
// L192: add [precise(z)] r2.z, r0.w, -cb1[10].z
r2.z=(asuint(asfloat(ref_u4(r0.w))+(-ref_f4(scope_reference_glass_cb[10].z)))).z;
// L193: div [precise(z)] r0.z, l(1.000000, 1.000000, 1.000000, 1.000000), r0.z
r0.z=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r0.z)))).z;
// L194: mul_sat [precise(z)] r0.z, r0.z, r2.z
r0.z=(asuint(dx_min(dx_max(asfloat(ref_u4(r0.z))*asfloat(ref_u4(r2.z)),ref_f4(0.)),ref_f4(1.)))).z;
// L195: mul [precise(z)] r2.z, r0.z, l(-2.000000)
r2.z=(asuint(asfloat(ref_u4(r0.z))*ref_f4(-2.000000))).z;
// L196: add [precise(xyz)] r2.xyz, r2.xyzx, l(-0.500000, -0.500000, 3.000000, 0.000000)
r2.xyz=(asuint(asfloat(ref_u4(r2.xyzx))+ref_f4(-0.500000,-0.500000,3.000000,0.000000))).xyz;
// L197: mul [precise(z)] r0.z, r0.z, r0.z
r0.z=(asuint(asfloat(ref_u4(r0.z))*asfloat(ref_u4(r0.z)))).z;
// L198: mul [precise(z)] r0.z, r0.z, r2.z
r0.z=(asuint(asfloat(ref_u4(r0.z))*asfloat(ref_u4(r2.z)))).z;
// L199: ge [precise(z)] r2.z, l(0.000000), r0.z
r2.z=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r0.z))))).z;
// L200: log [precise(z)] r0.z, r0.z
r0.z=(asuint(dx_log(asfloat(ref_u4(r0.z))))).z;
// L201: mul [precise(z)] r0.z, r0.z, cb1[10].w
r0.z=(asuint(asfloat(ref_u4(r0.z))*ref_f4(scope_reference_glass_cb[10].w))).z;
// L202: exp [precise(z)] r0.z, r0.z
r0.z=(asuint(dx_exp(asfloat(ref_u4(r0.z))))).z;
// L203: movc [precise(z)] r0.z, r2.z, l(0), r0.z
{uint4 c=ref_u4(r2.z),yes=ref_u4(uint(0)),no=ref_u4(r0.z);r0.z=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).z;}
// L204: mul [precise(xy)] r2.xy, r0.zzzz, r2.xyxx
r2.xy=(asuint(asfloat(ref_u4(r0.zzzz))*asfloat(ref_u4(r2.xyxx)))).xy;
// L205: mul [precise(xy)] r2.xy, r2.xyxx, l(0.500000, 0.500000, 0.000000, 0.000000)
r2.xy=(asuint(asfloat(ref_u4(r2.xyxx))*ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L206: add [precise(xy)] r0.xy, r0.xyxx, -r2.xyxx
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))+(-asfloat(ref_u4(r2.xyxx))))).xy;
// L207: add [precise(xy)] r0.xy, r0.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L208: mul [precise(xy)] r0.xy, r0.xyxx, cb1[11].xxxx
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))*ref_f4(scope_reference_glass_cb[11].xxxx))).xy;
// L209: dp2 [precise(x)] r0.x, r0.xyxx, r0.xyxx
r0.x=(asuint(ref_f4(dot(asfloat(ref_u4(r0.xyxx)).xy,asfloat(ref_u4(r0.xyxx)).xy)))).x;
// L210: sqrt [precise(x)] r0.x, r0.x
r0.x=(asuint(sqrt(asfloat(ref_u4(r0.x))))).x;
// L211: add [precise(x)] r0.x, r0.x, cb1[11].y
r0.x=(asuint(asfloat(ref_u4(r0.x))+ref_f4(scope_reference_glass_cb[11].y))).x;
// L212: mul [precise(x)] r0.x, r0.x, l(4.000000)
r0.x=(asuint(asfloat(ref_u4(r0.x))*ref_f4(4.000000))).x;
// L213: add [precise(xyz)] r0.xyz, r0.xxxx, l(0.000000, 0.330000, 0.670000, 0.000000)
r0.xyz=(asuint(asfloat(ref_u4(r0.xxxx))+ref_f4(0.000000,0.330000,0.670000,0.000000))).xyz;
// L214: mul [precise(xyz)] r0.xyz, r0.xyzx, l(6.283180, 6.283180, 6.283180, 0.000000)
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))*ref_f4(6.283180,6.283180,6.283180,0.000000))).xyz;
// L215: sincos [precise(xyz)] null, r0.xyz, r0.xyzx
{float4 angle=asfloat(ref_u4(r0.xyzx));r0.xyz=(asuint(cos(angle))).xyz;}
// L216: mul [precise(xyz)] r0.xyz, r0.xyzx, l(0.500000, 0.500000, 0.500000, 0.000000)
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))*ref_f4(0.500000,0.500000,0.500000,0.000000))).xyz;
// L217: add [precise(xyz)] r0.xyz, r0.xyzx, l(0.500000, 0.500000, 0.500000, 0.000000)
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))+ref_f4(0.500000,0.500000,0.500000,0.000000))).xyz;
// L218: dp3 [precise(x)] r2.x, r0.xyzx, l(0.212639, 0.715169, 0.072192, 0.000000)
r2.x=(asuint(ref_f4(dot(asfloat(ref_u4(r0.xyzx)).xyz,ref_f4(0.212639,0.715169,0.072192,0.000000).xyz)))).x;
// L219: add [precise(xyz)] r2.xyz, -r0.xyzx, r2.xxxx
r2.xyz=(asuint((-asfloat(ref_u4(r0.xyzx)))+asfloat(ref_u4(r2.xxxx)))).xyz;
// L220: mul [precise(xyz)] r2.xyz, r2.xyzx, cb1[11].zzzz
r2.xyz=(asuint(asfloat(ref_u4(r2.xyzx))*ref_f4(scope_reference_glass_cb[11].zzzz))).xyz;
// L221: add [precise(xyz)] r0.xyz, r0.xyzx, r2.xyzx
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))+asfloat(ref_u4(r2.xyzx)))).xyz;
// L222: mul [precise(xyz)] r0.xyz, r0.xyzx, cb1[11].wwww
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))*ref_f4(scope_reference_glass_cb[11].wwww))).xyz;
// L223: mul [precise(xyz)] r2.xyz, r0.xyzx, r5.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r0.xyzx))*asfloat(ref_u4(r5.xyzx)))).xyz;
// L224: add [precise(xyz)] r2.xyz, -r5.xyzx, r2.xyzx
r2.xyz=(asuint((-asfloat(ref_u4(r5.xyzx)))+asfloat(ref_u4(r2.xyzx)))).xyz;
// L225: mul [precise(xyz)] r2.xyz, r2.xyzx, cb1[12].xxxx
r2.xyz=(asuint(asfloat(ref_u4(r2.xyzx))*ref_f4(scope_reference_glass_cb[12].xxxx))).xyz;
// L226: add [precise(xyz)] r2.xyz, r2.xyzx, r5.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r2.xyzx))+asfloat(ref_u4(r5.xyzx)))).xyz;
// L227: add [precise(xyz)] r0.xyz, r0.xyzx, -r2.xyzx
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))+(-asfloat(ref_u4(r2.xyzx))))).xyz;
// L228: mul [precise(xyz)] r0.xyz, r0.xyzx, cb1[12].yyyy
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))*ref_f4(scope_reference_glass_cb[12].yyyy))).xyz;
// L229: add [precise(xyz)] r0.xyz, r0.xyzx, r2.xyzx
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))+asfloat(ref_u4(r2.xyzx)))).xyz;
// L230: dp3 [precise(x)] r2.x, r0.xyzx, l(0.212639, 0.715169, 0.072192, 0.000000)
r2.x=(asuint(ref_f4(dot(asfloat(ref_u4(r0.xyzx)).xyz,ref_f4(0.212639,0.715169,0.072192,0.000000).xyz)))).x;
// L231: add [precise(xyz)] r2.xyz, -r0.xyzx, r2.xxxx
r2.xyz=(asuint((-asfloat(ref_u4(r0.xyzx)))+asfloat(ref_u4(r2.xxxx)))).xyz;
// L232: mul [precise(xyz)] r2.xyz, r2.xyzx, cb1[12].zzzz
r2.xyz=(asuint(asfloat(ref_u4(r2.xyzx))*ref_f4(scope_reference_glass_cb[12].zzzz))).xyz;
// L233: add [precise(xyz)] r0.xyz, r0.xyzx, r2.xyzx
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))+asfloat(ref_u4(r2.xyzx)))).xyz;
// L234: mul [precise(xyz)] r0.xyz, r0.xyzx, cb1[12].wwww
r0.xyz=(asuint(asfloat(ref_u4(r0.xyzx))*ref_f4(scope_reference_glass_cb[12].wwww))).xyz;
float3 sourceCoatingColor=asfloat(r0.xyz);
// L235: add [precise(x)] r2.x, -cb1[13].y, cb1[13].x
r2.x=(asuint((-ref_f4(scope_reference_glass_cb[13].y))+ref_f4(scope_reference_glass_cb[13].x))).x;
// L236: add [precise(w)] r0.w, r0.w, -cb1[13].y
r0.w=(asuint(asfloat(ref_u4(r0.w))+(-ref_f4(scope_reference_glass_cb[13].y)))).w;
// L237: div [precise(x)] r2.x, l(1.000000, 1.000000, 1.000000, 1.000000), r2.x
r2.x=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r2.x)))).x;
// L238: mul_sat [precise(w)] r0.w, r0.w, r2.x
r0.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r0.w))*asfloat(ref_u4(r2.x)),ref_f4(0.)),ref_f4(1.)))).w;
// L239: mul [precise(x)] r2.x, r0.w, l(-2.000000)
r2.x=(asuint(asfloat(ref_u4(r0.w))*ref_f4(-2.000000))).x;
// L240: add [precise(x)] r2.x, r2.x, l(3.000000)
r2.x=(asuint(asfloat(ref_u4(r2.x))+ref_f4(3.000000))).x;
// L241: mul [precise(w)] r0.w, r0.w, r0.w
r0.w=(asuint(asfloat(ref_u4(r0.w))*asfloat(ref_u4(r0.w)))).w;
// L242: mul [precise(w)] r0.w, r0.w, r2.x
r0.w=(asuint(asfloat(ref_u4(r0.w))*asfloat(ref_u4(r2.x)))).w;
// L243: ge [precise(x)] r2.x, l(0.000000), r0.w
r2.x=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r0.w))))).x;
// L244: log [precise(w)] r0.w, r0.w
r0.w=(asuint(dx_log(asfloat(ref_u4(r0.w))))).w;
// L245: mul [precise(w)] r0.w, r0.w, cb1[13].z
r0.w=(asuint(asfloat(ref_u4(r0.w))*ref_f4(scope_reference_glass_cb[13].z))).w;
// L246: exp [precise(w)] r0.w, r0.w
r0.w=(asuint(dx_exp(asfloat(ref_u4(r0.w))))).w;
// L247: add [precise(w)] r0.w, -r0.w, l(1.000000)
r0.w=(asuint((-asfloat(ref_u4(r0.w)))+ref_f4(1.000000))).w;
// L248: movc [precise(w)] r0.w, r2.x, l(1.000000), r0.w
{uint4 c=ref_u4(r2.x),yes=ref_u4(asuint(1.000000)),no=ref_u4(r0.w);r0.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L249: mul_sat [precise(xyz)] r0.xyz, r0.wwww, r0.xyzx
r0.xyz=(asuint(dx_min(dx_max(asfloat(ref_u4(r0.wwww))*asfloat(ref_u4(r0.xyzx)),ref_f4(0.)),ref_f4(1.)))).xyz;
// L250: add [precise(xy)] r2.xy, v2.yxyy, -cb1[14].wzww
r2.xy=(asuint(ref_f4(v2.yxyy)+(-ref_f4(scope_reference_glass_cb[14].wzww)))).xy;
// L251: max [precise(zw)] r2.zw, cb1[14].yyyx, l(0.000000, 0.000000, 0.000010, 0.000010)
r2.zw=(asuint(dx_max(ref_f4(scope_reference_glass_cb[14].yyyx),ref_f4(0.000000,0.000000,0.000010,0.000010)))).zw;
// L252: div [precise(xy)] r2.xy, r2.xyxx, r2.zwzz
r2.xy=(asuint(asfloat(ref_u4(r2.xyxx))/asfloat(ref_u4(r2.zwzz)))).xy;
// L253: add [precise(xy)] r2.xy, r2.xyxx, cb1[14].wzww
r2.xy=(asuint(asfloat(ref_u4(r2.xyxx))+ref_f4(scope_reference_glass_cb[14].wzww))).xy;
// L254: mul [precise(w)] r0.w, cb1[15].z, l(0.017453)
r0.w=(asuint(ref_f4(scope_reference_glass_cb[15].z)*ref_f4(0.017453))).w;
// L255: sincos [precise(x)] r5.x, r6.x, r0.w
{float4 angle=asfloat(ref_u4(r0.w));r5.x=(asuint(sin(angle))).x;r6.x=(asuint(cos(angle))).x;}
// L256: add [precise(xy)] r2.xy, r2.xyxx, -cb1[16].yxyy
r2.xy=(asuint(asfloat(ref_u4(r2.xyxx))+(-ref_f4(scope_reference_glass_cb[16].yxyy)))).xy;
// L257: mul [precise(zw)] r2.zw, r6.xxxx, r2.yyyx
r2.zw=(asuint(asfloat(ref_u4(r6.xxxx))*asfloat(ref_u4(r2.yyyx)))).zw;
// L258: mul [precise(xy)] r2.xy, r5.xxxx, r2.xyxx
r2.xy=(asuint(asfloat(ref_u4(r5.xxxx))*asfloat(ref_u4(r2.xyxx)))).xy;
// L259: add [precise(x)] r5.x, -r2.x, r2.z
r5.x=(asuint((-asfloat(ref_u4(r2.x)))+asfloat(ref_u4(r2.z)))).x;
// L260: add [precise(y)] r5.y, r2.w, r2.y
r5.y=(asuint(asfloat(ref_u4(r2.w))+asfloat(ref_u4(r2.y)))).y;
// L261: add [precise(xy)] r2.xy, r5.xyxx, cb1[15].xyxx
r2.xy=(asuint(asfloat(ref_u4(r5.xyxx))+ref_f4(scope_reference_glass_cb[15].xyxx))).xy;
// L262: round_ni [precise(zw)] r2.zw, r2.xxxy
r2.zw=(asuint(floor(asfloat(ref_u4(r2.xxxy))))).zw;
// L263: mul [precise(zw)] r2.zw, |r2.zzzw|, l(0.000000, 0.000000, 0.500000, 0.500000)
r2.zw=(asuint(abs(asfloat(ref_u4(r2.zzzw)))*ref_f4(0.000000,0.000000,0.500000,0.500000))).zw;
// L264: ge [precise(xy)] r5.xy, r2.zwzz, -r2.zwzz
r5.xy=(ref_mask(ref_ge(asfloat(ref_u4(r2.zwzz)),(-asfloat(ref_u4(r2.zwzz)))))).xy;
// L265: frc [precise] r2.xyzw, r2.xyzw
r2.xyzw=(asuint(frac(asfloat(ref_u4(r2.xyzw))))).xyzw;
// L266: movc [precise(zw)] r2.zw, r5.xxxy, r2.zzzw, -r2.zzzw
{uint4 c=ref_u4(r5.xxxy),yes=ref_u4(r2.zzzw),no=(ref_u4(r2.zzzw)^ref_u4(0x80000000u));r2.zw=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).zw;}
// L267: mul [precise(zw)] r2.zw, r2.zzzw, l(0.000000, 0.000000, 2.000000, 2.000000)
r2.zw=(asuint(asfloat(ref_u4(r2.zzzw))*ref_f4(0.000000,0.000000,2.000000,2.000000))).zw;
// L268: add [precise(xy)] r5.xy, -r2.xyxx, l(1.000000, 1.000000, 0.000000, 0.000000)
r5.xy=(asuint((-asfloat(ref_u4(r2.xyxx)))+ref_f4(1.000000,1.000000,0.000000,0.000000))).xy;
// L269: add [precise(xy)] r5.xy, -r2.xyxx, r5.xyxx
r5.xy=(asuint((-asfloat(ref_u4(r2.xyxx)))+asfloat(ref_u4(r5.xyxx)))).xy;
// L270: mul [precise(zw)] r2.zw, r2.zzzw, r5.xxxy
r2.zw=(asuint(asfloat(ref_u4(r2.zzzw))*asfloat(ref_u4(r5.xxxy)))).zw;
// L271: add [precise(xy)] r2.xy, r2.zwzz, r2.xyxx
r2.xy=(asuint(asfloat(ref_u4(r2.zwzz))+asfloat(ref_u4(r2.xyxx)))).xy;
// L272: sample_b_indexable [precise(w)](texture2d)(float,float,float,float) r0.w, r2.xyxx, t5.yzwx, s2, cb0[163].y
r0.w=asuint(ref_glass_sample_5(asfloat(r2.xyxx).xy,sourceGlassMipBias).yzwx).w;
// L273: mul [precise(w)] r1.w, r1.w, cb1[16].z
r1.w=(asuint(asfloat(ref_u4(r1.w))*ref_f4(scope_reference_glass_cb[16].z))).w;
// L274: max [precise(w)] r1.w, r1.w, l(0.000100)
r1.w=(asuint(dx_max(asfloat(ref_u4(r1.w)),ref_f4(0.000100)))).w;
// L275: ge [precise(x)] r2.x, l(0.000000), r0.w
r2.x=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r0.w))))).x;
// L276: log [precise(w)] r0.w, r0.w
r0.w=(asuint(dx_log(asfloat(ref_u4(r0.w))))).w;
// L277: mul [precise(w)] r0.w, r0.w, r1.w
r0.w=(asuint(asfloat(ref_u4(r0.w))*asfloat(ref_u4(r1.w)))).w;
// L278: exp [precise(w)] r0.w, r0.w
r0.w=(asuint(dx_exp(asfloat(ref_u4(r0.w))))).w;
// L279: mul [precise(w)] r0.w, r0.w, cb1[16].w
r0.w=(asuint(asfloat(ref_u4(r0.w))*ref_f4(scope_reference_glass_cb[16].w))).w;
// L280: movc [precise(w)] r0.w, r2.x, l(0), r0.w
{uint4 c=ref_u4(r2.x),yes=ref_u4(uint(0)),no=ref_u4(r0.w);r0.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L281: min [precise(w)] r0.w, r0.w, cb1[17].x
r0.w=(asuint(dx_min(asfloat(ref_u4(r0.w)),ref_f4(scope_reference_glass_cb[17].x)))).w;
// L282: mul [precise(w)] r1.w, r0.w, cb1[17].y
r1.w=(asuint(asfloat(ref_u4(r0.w))*ref_f4(scope_reference_glass_cb[17].y))).w;
// L283: add_sat [precise(x)] r2.x, -r1.w, cb1[17].z
r2.x=(asuint(dx_min(dx_max((-asfloat(ref_u4(r1.w)))+ref_f4(scope_reference_glass_cb[17].z),ref_f4(0.)),ref_f4(1.)))).x;
// L284: ge [precise(w)] r1.w, l(0.000000), r0.w
r1.w=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r0.w))))).w;
// L285: log [precise(w)] r0.w, r0.w
r0.w=(asuint(dx_log(asfloat(ref_u4(r0.w))))).w;
// L286: mul [precise(w)] r0.w, r0.w, cb1[18].x
r0.w=(asuint(asfloat(ref_u4(r0.w))*ref_f4(scope_reference_glass_cb[18].x))).w;
// L287: exp [precise(w)] r0.w, r0.w
r0.w=(asuint(dx_exp(asfloat(ref_u4(r0.w))))).w;
// L288: mul [precise(w)] r0.w, r0.w, cb1[18].y
r0.w=(asuint(asfloat(ref_u4(r0.w))*ref_f4(scope_reference_glass_cb[18].y))).w;
// L289: movc [precise(w)] r0.w, r1.w, l(0), r0.w
{uint4 c=ref_u4(r1.w),yes=ref_u4(uint(0)),no=ref_u4(r0.w);r0.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L290: add_sat r0.w, r0.w, cb1[18].z
r0.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r0.w))+ref_f4(scope_reference_glass_cb[18].z),ref_f4(0.)),ref_f4(1.)))).w;
// L291: mov_sat [precise(y)] r2.y, cb1[17].w
r2.y=(asuint(dx_min(dx_max(ref_f4(scope_reference_glass_cb[17].w),ref_f4(0.)),ref_f4(1.)))).y;
// L292: mad r2.z, r0.w, cb0[159].y, cb0[159].x
r2.z=(asuint(asfloat(ref_u4(r0.w))*ref_f4(sourceGlassRoughnessOverride.y)+ref_f4(sourceGlassRoughnessOverride.x))).z;
ScopeReferenceGlass o;o.baseColor=asfloat(r0.xyz);o.coatingColor=sourceCoatingColor;o.worldNormal=asfloat(r3.xyz);o.emission=asfloat(r4.xyz);o.metallic=asfloat(r2.x);o.specular=asfloat(r2.y);o.roughness=asfloat(r2.z);return o;}
#endif

#ifndef SCOPE_REFERENCE_OPTICS_H
#define SCOPE_REFERENCE_OPTICS_H
float4 scope_reference_cb[62];
SamplerState scope_reference_capture_sampler;
SamplerState scope_reference_surface_sampler;
Texture2D scope_reference_t2;
Texture2D scope_reference_t3;
Texture2D scope_reference_t4;
Texture2D scope_reference_t5;
Texture2D scope_reference_t6;
Texture2D scope_reference_t7;
float4 ref_f4(float x){return x.xxxx;}
float4 ref_f4(float4 x){return x;}
float4 ref_f4(int4 x){return (float4)x;}
float4 ref_f4(float a,float b,float c,float d){return float4(a,b,c,d);}
uint4 ref_u4(uint x){return x.xxxx;}
uint4 ref_u4(uint4 x){return x;}
uint4 ref_u4(int4 x){return (uint4)x;}
uint4 ref_u4(uint a,uint b,uint c,uint d){return uint4(a,b,c,d);}
int4 ref_i4(uint4 x){return (int4)x;}
int4 ref_i4(float4 x){return (int4)x;}
bool4 ref_ge(float4 a,float4 b){return a>=b;}
bool4 ref_ge(int4 a,int4 b){return a>=b;}
bool4 ref_lt(float4 a,float4 b){return a<b;}
bool4 ref_ne(float4 a,float4 b){return a!=b;}
uint4 ref_mask(bool4 p){return uint4(p.x?0xffffffffu:0u,p.y?0xffffffffu:0u,p.z?0xffffffffu:0u,p.w?0xffffffffu:0u);}
float4 dx_min(float4 a,float4 b){return float4(isnan(a.x)?b.x:isnan(b.x)?a.x:min(a.x,b.x),isnan(a.y)?b.y:isnan(b.y)?a.y:min(a.y,b.y),isnan(a.z)?b.z:isnan(b.z)?a.z:min(a.z,b.z),isnan(a.w)?b.w:isnan(b.w)?a.w:min(a.w,b.w));}
float4 dx_max(float4 a,float4 b){return float4(isnan(a.x)?b.x:isnan(b.x)?a.x:max(a.x,b.x),isnan(a.y)?b.y:isnan(b.y)?a.y:max(a.y,b.y),isnan(a.z)?b.z:isnan(b.z)?a.z:max(a.z,b.z),isnan(a.w)?b.w:isnan(b.w)?a.w:max(a.w,b.w));}
float ref_log(float x){if(x==0)return asfloat(0xff800000u);if(x<0)return asfloat(0x7fc00000u);return log2(x);}
float ref_exp(float x){if(isinf(x))return x<0?0:x;return exp2(x);}
float4 dx_log(float4 x){return float4(ref_log(x.x),ref_log(x.y),ref_log(x.z),ref_log(x.w));}
float4 dx_exp(float4 x){return float4(ref_exp(x.x),ref_exp(x.y),ref_exp(x.z),ref_exp(x.w));}
float2 sampleUv(int slot,float2 uv){
#ifdef SCOPE_REFERENCE_SAMPLE_UV
return slot<=4?SCOPE_REFERENCE_SAMPLE_UV(slot,uv):uv;
#else
return slot<=4?float2(uv.x,1-uv.y):uv;
#endif
}
float4 ref_sample_2(float2 uv,float lod){return scope_reference_t2.SampleLevel(scope_reference_capture_sampler,uv,lod);}
float4 ref_sample_3(float2 uv,float lod){return scope_reference_t3.SampleLevel(scope_reference_capture_sampler,uv,lod);}
float4 ref_sample_4(float2 uv,float lod){return scope_reference_t4.SampleLevel(scope_reference_capture_sampler,uv,lod);}
float4 ref_sample_5(float2 uv,float lod){return scope_reference_t5.SampleLevel(scope_reference_surface_sampler,uv,lod);}
float4 ref_sample_6(float2 uv,float lod){return scope_reference_t6.SampleLevel(scope_reference_surface_sampler,uv,lod);}
float4 ref_sample_7(float2 uv,float lod){return scope_reference_t7.SampleLevel(scope_reference_surface_sampler,uv,lod);}
#ifdef SCOPE_REFERENCE_RETICLE_SAMPLE
#define ref_sample_5(uv,lod) SCOPE_REFERENCE_RETICLE_SAMPLE(uv,lod)
#endif
#ifndef SCOPE_REFERENCE_RETICLE_CB22
#define SCOPE_REFERENCE_RETICLE_CB22 scope_reference_cb[22]
#endif
#ifndef SCOPE_REFERENCE_PUPIL_VIEW
#define SCOPE_REFERENCE_PUPIL_VIEW(uv,view) (view)
#endif
#ifndef SCOPE_REFERENCE_RETICLE_VIEW
#define SCOPE_REFERENCE_RETICLE_VIEW(uv,view) (view)
#endif

static const float4 icb[9]={float4(-1,1,.0625,0),float4(0,1,.125,0),float4(1,1,.0625,0),float4(-1,0,.125,0),float4(0,0,.25,0),float4(1,0,.125,0),float4(-1,-1,.0625,0),float4(0,-1,.125,0),float4(1,-1,.0625,0)};
float3 scope_reference_optics(float2 sourceUv,float3 sourceTangent,float3 sourceBitangent,float3 sourceView,float sourceEyeDistanceCm,float opticsActive)
{
 uint4 r0=asuint(float4(normalize(sourceBitangent),0)),r1=0,r2=asuint(float4(normalize(sourceView),0)),r3=0,r4=0,r5=0,r6=0,r7=0,r8=0,r9=0,r10=0,r11=0,r12=0,r13=0,r14=0,r15=0,r16=0;
 float4 v1=float4(normalize(sourceTangent),0),v3=float4(sourceUv,0,0);
// L94: mad r8.xy, cb2[0].xyxx, l(-0.050000, -0.050000, 0.000000, 0.000000), l(0.025000, 0.025000, 0.000000, 0.000000)
r8.xy=(asuint(ref_f4(scope_reference_cb[0].xyxx)*ref_f4(-0.050000,-0.050000,0.000000,0.000000)+ref_f4(0.025000,0.025000,0.000000,0.000000))).xy;
// L95: dp3 [precise(x)] r9.x, v1.xyzx, r2.xyzx
r9.x=(asuint(ref_f4(dot(ref_f4(v1.xyzx).xyz,asfloat(ref_u4(r2.xyzx)).xyz)))).x;
// L96: dp3 [precise(y)] r9.y, r0.xyzx, r2.xyzx
r9.y=(asuint(ref_f4(dot(asfloat(ref_u4(r0.xyzx)).xyz,asfloat(ref_u4(r2.xyzx)).xyz)))).y;
// L97: mad r2.xyzw, r9.xyxy, r8.xxxx, v3.xyxy
r2.xyzw=(asuint(asfloat(ref_u4(r9.xyxy))*asfloat(ref_u4(r8.xxxx))+ref_f4(v3.xyxy))).xyzw;
// L98: mad r0.xy, r9.xyxx, r8.yyyy, v3.xyxx
r0.xy=(asuint(asfloat(ref_u4(r9.xyxx))*asfloat(ref_u4(r8.yyyy))+ref_f4(v3.xyxx))).xy;
// L99: add r0.xy, r0.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L100: add [precise(xy)] r8.xy, v3.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r8.xy=(asuint(ref_f4(v3.xyxx)+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L101: dp2 [precise(z)] r0.z, r8.xyxx, r8.xyxx
r0.z=(asuint(ref_f4(dot(asfloat(ref_u4(r8.xyxx)).xy,asfloat(ref_u4(r8.xyxx)).xy)))).z;
// L102: sqrt [precise(w)] r3.w, r0.z
r3.w=(asuint(sqrt(asfloat(ref_u4(r0.z))))).w;
// L103: add [precise(w)] r4.w, -cb2[0].w, cb2[0].z
r4.w=(asuint((-ref_f4(scope_reference_cb[0].w))+ref_f4(scope_reference_cb[0].z))).w;
// L104: add [precise(w)] r5.w, r3.w, -cb2[0].w
r5.w=(asuint(asfloat(ref_u4(r3.w))+(-ref_f4(scope_reference_cb[0].w)))).w;
// L105: div [precise(w)] r4.w, l(1.000000, 1.000000, 1.000000, 1.000000), r4.w
r4.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.w)))).w;
// L106: mul_sat [precise(w)] r4.w, r4.w, r5.w
r4.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r5.w)),ref_f4(0.)),ref_f4(1.)))).w;
// L107: mul [precise(w)] r5.w, r4.w, l(-2.000000)
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-2.000000))).w;
// L108: add [precise(w)] r5.w, r5.w, l(3.000000)
r5.w=(asuint(asfloat(ref_u4(r5.w))+ref_f4(3.000000))).w;
// L109: mul [precise(w)] r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L110: mul [precise(w)] r4.w, r4.w, r5.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r5.w)))).w;
// L111: ge [precise(w)] r5.w, l(0.000000), r4.w
r5.w=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r4.w))))).w;
// L112: log [precise(w)] r4.w, r4.w
r4.w=(asuint(dx_log(asfloat(ref_u4(r4.w))))).w;
// L113: mul [precise(w)] r4.w, r4.w, cb2[1].x
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(scope_reference_cb[1].x))).w;
// L114: exp [precise(w)] r4.w, r4.w
r4.w=(asuint(dx_exp(asfloat(ref_u4(r4.w))))).w;
// L115: movc [precise(w)] r4.w, r5.w, l(0), r4.w
{uint4 c=ref_u4(r5.w),yes=ref_u4(uint(0)),no=ref_u4(r4.w);r4.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L116: mul r8.zw, r0.xxxy, r4.wwww
r8.zw=(asuint(asfloat(ref_u4(r0.xxxy))*asfloat(ref_u4(r4.wwww)))).zw;
// L117: mul r9.zw, r8.zzzw, cb2[1].yyyy
r9.zw=(asuint(asfloat(ref_u4(r8.zzzw))*ref_f4(scope_reference_cb[1].yyyy))).zw;
// L118: mad r2.xyzw, r2.xyzw, l(0.033333, 0.033333, 0.033333, 0.033333), l(0.483333, 0.483333, -0.016667, -0.016667)
r2.xyzw=(asuint(asfloat(ref_u4(r2.xyzw))*ref_f4(0.033333,0.033333,0.033333,0.033333)+ref_f4(0.483333,0.483333,-0.016667,-0.016667))).xyzw;
#ifdef SCOPE_REFERENCE_CAPTURE_BASE
SCOPE_REFERENCE_CAPTURE_BASE(asfloat(r2.xy));
#endif
// L119: dp2 r4.w, r2.zwzz, r2.zwzz
r4.w=(asuint(ref_f4(dot(asfloat(ref_u4(r2.zwzz)).xy,asfloat(ref_u4(r2.zwzz)).xy)))).w;
// L120: sqrt r5.w, r4.w
r5.w=(asuint(sqrt(asfloat(ref_u4(r4.w))))).w;
// L121: mad r6.w, -r5.w, cb2[1].z, l(1.000000)
r6.w=(asuint((-asfloat(ref_u4(r5.w)))*ref_f4(scope_reference_cb[1].z)+ref_f4(1.000000))).w;
// L122: mul r7.w, r6.w, cb2[1].w
r7.w=(asuint(asfloat(ref_u4(r6.w))*ref_f4(scope_reference_cb[1].w))).w;
// L123: mul r7.w, r7.w, r7.w
r7.w=(asuint(asfloat(ref_u4(r7.w))*asfloat(ref_u4(r7.w)))).w;
// L124: mul r7.w, r7.w, l(1.442695)
r7.w=(asuint(asfloat(ref_u4(r7.w))*ref_f4(1.442695))).w;
// L125: exp r7.w, r7.w
r7.w=(asuint(dx_exp(asfloat(ref_u4(r7.w))))).w;
// L126: div r7.w, l(1.000000, 1.000000, 1.000000, 1.000000), r7.w
r7.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r7.w)))).w;
// L127: lt r10.x, l(0.000010), |r6.w|
r10.x=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r6.w)))))).x;
// L128: ge r6.w, r6.w, l(0.000000)
r6.w=(ref_mask(ref_ge(asfloat(ref_u4(r6.w)),ref_f4(0.000000)))).w;
// L129: add r7.w, -r7.w, l(1.000000)
r7.w=(asuint((-asfloat(ref_u4(r7.w)))+ref_f4(1.000000))).w;
// L130: add r7.w, -r7.w, l(1.000000)
r7.w=(asuint((-asfloat(ref_u4(r7.w)))+ref_f4(1.000000))).w;
// L131: movc r6.w, r6.w, r7.w, l(1.000000)
{uint4 c=ref_u4(r6.w),yes=ref_u4(r7.w),no=ref_u4(asuint(1.000000));r6.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L132: movc r6.w, r10.x, r6.w, l(1.000000)
{uint4 c=ref_u4(r10.x),yes=ref_u4(r6.w),no=ref_u4(asuint(1.000000));r6.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L133: mul r10.xy, r2.zwzz, r6.wwww
r10.xy=(asuint(asfloat(ref_u4(r2.zwzz))*asfloat(ref_u4(r6.wwww)))).xy;
// L134: mad r10.zw, -r5.wwww, cb2[2].yyyw, l(0.000000, 0.000000, 1.000000, 1.000000)
r10.zw=(asuint((-asfloat(ref_u4(r5.wwww)))*ref_f4(scope_reference_cb[2].yyyw)+ref_f4(0.000000,0.000000,1.000000,1.000000))).zw;
// L135: mul r6.w, r10.z, cb2[2].z
r6.w=(asuint(asfloat(ref_u4(r10.z))*ref_f4(scope_reference_cb[2].z))).w;
// L136: mul r6.w, r6.w, r6.w
r6.w=(asuint(asfloat(ref_u4(r6.w))*asfloat(ref_u4(r6.w)))).w;
// L137: mul r6.w, r6.w, l(1.442695)
r6.w=(asuint(asfloat(ref_u4(r6.w))*ref_f4(1.442695))).w;
// L138: exp r6.w, r6.w
r6.w=(asuint(dx_exp(asfloat(ref_u4(r6.w))))).w;
// L139: div r6.w, l(1.000000, 1.000000, 1.000000, 1.000000), r6.w
r6.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r6.w)))).w;
// L140: lt r11.xy, l(0.000010, 0.000010, 0.000000, 0.000000), |r10.zwzz|
r11.xy=(ref_mask(ref_lt(ref_f4(0.000010,0.000010,0.000000,0.000000),abs(asfloat(ref_u4(r10.zwzz)))))).xy;
// L141: ge r11.zw, r10.zzzw, l(0.000000, 0.000000, 0.000000, 0.000000)
r11.zw=(ref_mask(ref_ge(asfloat(ref_u4(r10.zzzw)),ref_f4(0.000000,0.000000,0.000000,0.000000)))).zw;
// L142: add r6.w, -r6.w, l(1.000000)
r6.w=(asuint((-asfloat(ref_u4(r6.w)))+ref_f4(1.000000))).w;
// L143: and r6.w, r6.w, r11.z
r6.w=(ref_u4(r6.w)&ref_u4(r11.z)).w;
// L144: and r12.x, r6.w, r11.x
r12.x=(ref_u4(r6.w)&ref_u4(r11.x)).x;
// L145: mul r6.w, r10.w, cb2[3].x
r6.w=(asuint(asfloat(ref_u4(r10.w))*ref_f4(scope_reference_cb[3].x))).w;
// L146: mul r6.w, r6.w, r6.w
r6.w=(asuint(asfloat(ref_u4(r6.w))*asfloat(ref_u4(r6.w)))).w;
// L147: mul r6.w, r6.w, l(1.442695)
r6.w=(asuint(asfloat(ref_u4(r6.w))*ref_f4(1.442695))).w;
// L148: exp r6.w, r6.w
r6.w=(asuint(dx_exp(asfloat(ref_u4(r6.w))))).w;
// L149: div r6.w, l(1.000000, 1.000000, 1.000000, 1.000000), r6.w
r6.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r6.w)))).w;
// L150: add r6.w, -r6.w, l(1.000000)
r6.w=(asuint((-asfloat(ref_u4(r6.w)))+ref_f4(1.000000))).w;
// L151: and r6.w, r6.w, r11.w
r6.w=(ref_u4(r6.w)&ref_u4(r11.w)).w;
// L152: and r12.y, r6.w, r11.y
r12.y=(ref_u4(r6.w)&ref_u4(r11.y)).y;
// L153: mad r10.zw, -r5.wwww, cb2[3].yyyw, l(0.000000, 0.000000, 1.000000, 1.000000)
r10.zw=(asuint((-asfloat(ref_u4(r5.wwww)))*ref_f4(scope_reference_cb[3].yyyw)+ref_f4(0.000000,0.000000,1.000000,1.000000))).zw;
// L154: mul r6.w, r10.z, cb2[3].z
r6.w=(asuint(asfloat(ref_u4(r10.z))*ref_f4(scope_reference_cb[3].z))).w;
// L155: mul r6.w, r6.w, r6.w
r6.w=(asuint(asfloat(ref_u4(r6.w))*asfloat(ref_u4(r6.w)))).w;
// L156: mul r6.w, r6.w, l(1.442695)
r6.w=(asuint(asfloat(ref_u4(r6.w))*ref_f4(1.442695))).w;
// L157: exp r6.w, r6.w
r6.w=(asuint(dx_exp(asfloat(ref_u4(r6.w))))).w;
// L158: div r6.w, l(1.000000, 1.000000, 1.000000, 1.000000), r6.w
r6.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r6.w)))).w;
// L159: lt r11.xy, l(0.000010, 0.000010, 0.000000, 0.000000), |r10.zwzz|
r11.xy=(ref_mask(ref_lt(ref_f4(0.000010,0.000010,0.000000,0.000000),abs(asfloat(ref_u4(r10.zwzz)))))).xy;
// L160: ge r11.zw, r10.zzzw, l(0.000000, 0.000000, 0.000000, 0.000000)
r11.zw=(ref_mask(ref_ge(asfloat(ref_u4(r10.zzzw)),ref_f4(0.000000,0.000000,0.000000,0.000000)))).zw;
// L161: add r6.w, -r6.w, l(1.000000)
r6.w=(asuint((-asfloat(ref_u4(r6.w)))+ref_f4(1.000000))).w;
// L162: and r6.w, r6.w, r11.z
r6.w=(ref_u4(r6.w)&ref_u4(r11.z)).w;
// L163: and r12.z, r6.w, r11.x
r12.z=(ref_u4(r6.w)&ref_u4(r11.x)).z;
// L164: mul r6.w, r10.w, cb2[4].x
r6.w=(asuint(asfloat(ref_u4(r10.w))*ref_f4(scope_reference_cb[4].x))).w;
// L165: mul r6.w, r6.w, r6.w
r6.w=(asuint(asfloat(ref_u4(r6.w))*asfloat(ref_u4(r6.w)))).w;
// L166: mul r6.w, r6.w, l(1.442695)
r6.w=(asuint(asfloat(ref_u4(r6.w))*ref_f4(1.442695))).w;
// L167: exp r6.w, r6.w
r6.w=(asuint(dx_exp(asfloat(ref_u4(r6.w))))).w;
// L168: div r6.w, l(1.000000, 1.000000, 1.000000, 1.000000), r6.w
r6.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r6.w)))).w;
// L169: movc r6.w, r11.w, -r6.w, l(-1.000000)
{uint4 c=ref_u4(r11.w),yes=(ref_u4(r6.w)^ref_u4(0x80000000u)),no=ref_u4(asuint(-1.000000));r6.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L170: movc r6.w, r11.y, r6.w, l(-1.000000)
{uint4 c=ref_u4(r11.y),yes=ref_u4(r6.w),no=ref_u4(asuint(-1.000000));r6.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L171: mad r6.w, cb2[4].y, r6.w, l(1.000000)
r6.w=(asuint(ref_f4(scope_reference_cb[4].y)*asfloat(ref_u4(r6.w))+ref_f4(1.000000))).w;
// L172: mul r11.xyz, r6.wwww, r12.xyzx
r11.xyz=(asuint(asfloat(ref_u4(r6.wwww))*asfloat(ref_u4(r12.xyzx)))).xyz;
// L173: mad r5.w, -r5.w, cb2[4].z, l(1.000000)
r5.w=(asuint((-asfloat(ref_u4(r5.w)))*ref_f4(scope_reference_cb[4].z)+ref_f4(1.000000))).w;
// L174: mul r7.w, r5.w, cb2[4].w
r7.w=(asuint(asfloat(ref_u4(r5.w))*ref_f4(scope_reference_cb[4].w))).w;
// L175: mul r7.w, r7.w, r7.w
r7.w=(asuint(asfloat(ref_u4(r7.w))*asfloat(ref_u4(r7.w)))).w;
// L176: mul r7.w, r7.w, l(1.442695)
r7.w=(asuint(asfloat(ref_u4(r7.w))*ref_f4(1.442695))).w;
// L177: exp r7.w, r7.w
r7.w=(asuint(dx_exp(asfloat(ref_u4(r7.w))))).w;
// L178: div r7.w, l(1.000000, 1.000000, 1.000000, 1.000000), r7.w
r7.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r7.w)))).w;
// L179: lt r10.z, l(0.000010), |r5.w|
r10.z=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r5.w)))))).z;
// L180: ge r5.w, r5.w, l(0.000000)
r5.w=(ref_mask(ref_ge(asfloat(ref_u4(r5.w)),ref_f4(0.000000)))).w;
// L181: add r7.w, -r7.w, l(1.000000)
r7.w=(asuint((-asfloat(ref_u4(r7.w)))+ref_f4(1.000000))).w;
// L182: add r7.w, -r7.w, l(1.000000)
r7.w=(asuint((-asfloat(ref_u4(r7.w)))+ref_f4(1.000000))).w;
// L183: movc r5.w, r5.w, r7.w, l(1.000000)
{uint4 c=ref_u4(r5.w),yes=ref_u4(r7.w),no=ref_u4(asuint(1.000000));r5.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L184: movc r5.w, r10.z, r5.w, l(1.000000)
{uint4 c=ref_u4(r10.z),yes=ref_u4(r5.w),no=ref_u4(asuint(1.000000));r5.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L185: mad r6.w, -r12.x, r6.w, l(1.000000)
r6.w=(asuint((-asfloat(ref_u4(r12.x)))*asfloat(ref_u4(r6.w))+ref_f4(1.000000))).w;
// L186: add r5.w, r5.w, -r6.w
r5.w=(asuint(asfloat(ref_u4(r5.w))+(-asfloat(ref_u4(r6.w))))).w;
// L187: mad r10.zw, -r3.wwww, cb2[5].xxxz, l(0.000000, 0.000000, 1.000000, 1.000000)
r10.zw=(asuint((-asfloat(ref_u4(r3.wwww)))*ref_f4(scope_reference_cb[5].xxxz)+ref_f4(0.000000,0.000000,1.000000,1.000000))).zw;
// L188: mul r12.xy, r10.zwzz, cb2[5].ywyy
r12.xy=(asuint(asfloat(ref_u4(r10.zwzz))*ref_f4(scope_reference_cb[5].ywyy))).xy;
// L189: mul r12.xy, r12.xyxx, r12.xyxx
r12.xy=(asuint(asfloat(ref_u4(r12.xyxx))*asfloat(ref_u4(r12.xyxx)))).xy;
// L190: mul r12.xy, r12.xyxx, l(1.442695, 1.442695, 0.000000, 0.000000)
r12.xy=(asuint(asfloat(ref_u4(r12.xyxx))*ref_f4(1.442695,1.442695,0.000000,0.000000))).xy;
// L191: exp r12.xy, r12.xyxx
r12.xy=(asuint(dx_exp(asfloat(ref_u4(r12.xyxx))))).xy;
// L192: div r12.xy, l(1.000000, 1.000000, 1.000000, 1.000000), r12.xyxx
r12.xy=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r12.xyxx)))).xy;
// L193: lt r12.zw, l(0.000000, 0.000000, 0.000010, 0.000010), |r10.zzzw|
r12.zw=(ref_mask(ref_lt(ref_f4(0.000000,0.000000,0.000010,0.000010),abs(asfloat(ref_u4(r10.zzzw)))))).zw;
// L194: ge r10.zw, r10.zzzw, l(0.000000, 0.000000, 0.000000, 0.000000)
r10.zw=(ref_mask(ref_ge(asfloat(ref_u4(r10.zzzw)),ref_f4(0.000000,0.000000,0.000000,0.000000)))).zw;
// L195: add r12.xy, -r12.xyxx, l(1.000000, 1.000000, 0.000000, 0.000000)
r12.xy=(asuint((-asfloat(ref_u4(r12.xyxx)))+ref_f4(1.000000,1.000000,0.000000,0.000000))).xy;
// L196: add r6.w, -r12.x, l(1.000000)
r6.w=(asuint((-asfloat(ref_u4(r12.x)))+ref_f4(1.000000))).w;
// L197: movc r6.w, r10.z, r6.w, l(1.000000)
{uint4 c=ref_u4(r10.z),yes=ref_u4(r6.w),no=ref_u4(asuint(1.000000));r6.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L198: movc r6.w, r12.z, r6.w, l(1.000000)
{uint4 c=ref_u4(r12.z),yes=ref_u4(r6.w),no=ref_u4(asuint(1.000000));r6.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L199: and r7.w, r10.w, r12.y
r7.w=(ref_u4(r10.w)&ref_u4(r12.y)).w;
// L200: and r7.w, r7.w, r12.w
r7.w=(ref_u4(r7.w)&ref_u4(r12.w)).w;
// L201: add r10.z, -r7.w, l(1.000000)
r10.z=(asuint((-asfloat(ref_u4(r7.w)))+ref_f4(1.000000))).z;
// L202: add r6.w, r6.w, -r10.z
r6.w=(asuint(asfloat(ref_u4(r6.w))+(-asfloat(ref_u4(r10.z))))).w;
// L203-222: world/primitive position reconstruction adapter
r3.x=asuint(sourceEyeDistanceCm);
// L223: add [precise(y)] r3.y, r3.x, -cb2[6].w
r3.y=(asuint(asfloat(ref_u4(r3.x))+(-ref_f4(scope_reference_cb[6].w)))).y;
// L224: mul_sat [precise(y)] r3.y, r3.y, cb2[7].x
r3.y=(asuint(dx_min(dx_max(asfloat(ref_u4(r3.y))*ref_f4(scope_reference_cb[7].x),ref_f4(0.)),ref_f4(1.)))).y;
// L225: exp [precise(z)] r3.z, cb2[7].y
r3.z=(asuint(dx_exp(ref_f4(scope_reference_cb[7].y)))).z;
// L226: add [precise(z)] r3.z, r3.z, l(-1.000000)
r3.z=(asuint(asfloat(ref_u4(r3.z))+ref_f4(-1.000000))).z;
// L227: mul [precise(y)] r3.y, r3.z, r3.y
r3.y=(asuint(asfloat(ref_u4(r3.z))*asfloat(ref_u4(r3.y)))).y;
// L228: add [precise(y)] r3.y, r3.y, l(1.000000)
r3.y=(asuint(asfloat(ref_u4(r3.y))+ref_f4(1.000000))).y;
// L229: mul [precise(z)] r3.z, r3.y, cb2[7].z
r3.z=(asuint(asfloat(ref_u4(r3.y))*ref_f4(scope_reference_cb[7].z))).z;
// L230: add [precise(x)] r3.x, r3.x, -cb2[7].w
r3.x=(asuint(asfloat(ref_u4(r3.x))+(-ref_f4(scope_reference_cb[7].w)))).x;
// L231: mul_sat [precise(x)] r3.x, r3.x, cb2[8].x
r3.x=(asuint(dx_min(dx_max(asfloat(ref_u4(r3.x))*ref_f4(scope_reference_cb[8].x),ref_f4(0.)),ref_f4(1.)))).x;
// L232: exp [precise(x)] r4.x, cb2[8].y
r4.x=(asuint(dx_exp(ref_f4(scope_reference_cb[8].y)))).x;
// L233: add [precise(y)] r4.y, -r4.x, l(1.000000)
r4.y=(asuint((-asfloat(ref_u4(r4.x)))+ref_f4(1.000000))).y;
// L234: mul [precise(x)] r3.x, r3.x, r4.y
r3.x=(asuint(asfloat(ref_u4(r3.x))*asfloat(ref_u4(r4.y)))).x;
// L235: add [precise(x)] r3.x, r4.x, r3.x
r3.x=(asuint(asfloat(ref_u4(r4.x))+asfloat(ref_u4(r3.x)))).x;
// L236: mul [precise(z)] r3.z, r3.x, r3.z
r3.z=(asuint(asfloat(ref_u4(r3.x))*asfloat(ref_u4(r3.z)))).z;
// L237: mul [precise(z)] r3.z, r3.z, l(0.050000)
r3.z=(asuint(asfloat(ref_u4(r3.z))*ref_f4(0.050000))).z;
// L238: add [precise(z)] r3.z, r3.z, l(-0.025000)
r3.z=(asuint(asfloat(ref_u4(r3.z))+ref_f4(-0.025000))).z;
// L239: mul [precise(xy)] r4.xy, r3.zzzz, r9.xyxx
r4.xy=(asuint(asfloat(ref_u4(r3.zzzz))*ref_f4(SCOPE_REFERENCE_PUPIL_VIEW(v3.xy,asfloat(r9.xy)).xyxy))).xy;
// L240: add [precise(xy)] r4.xy, r4.xyxx, v3.xyxx
r4.xy=(asuint(asfloat(ref_u4(r4.xyxx))+ref_f4(v3.xyxx))).xy;
// L241: mul [precise(xy)] r4.xy, r4.xyxx, l(3.000300, 3.000300, 0.000000, 0.000000)
r4.xy=(asuint(asfloat(ref_u4(r4.xyxx))*ref_f4(3.000300,3.000300,0.000000,0.000000))).xy;
// L242: add [precise(xy)] r4.xy, r4.xyxx, l(0.500000, 0.500000, 0.000000, 0.000000)
r4.xy=(asuint(asfloat(ref_u4(r4.xyxx))+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L243: add [precise(xy)] r4.xy, r4.xyxx, l(-1.500150, -1.500150, 0.000000, 0.000000)
r4.xy=(asuint(asfloat(ref_u4(r4.xyxx))+ref_f4(-1.500150,-1.500150,0.000000,0.000000))).xy;
// L244: add [precise(xy)] r4.xy, r4.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r4.xy=(asuint(asfloat(ref_u4(r4.xyxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L245: dp2 [precise(z)] r3.z, r4.xyxx, r4.xyxx
r3.z=(asuint(ref_f4(dot(asfloat(ref_u4(r4.xyxx)).xy,asfloat(ref_u4(r4.xyxx)).xy)))).z;
// L246: sqrt [precise(z)] r3.z, r3.z
r3.z=(asuint(sqrt(asfloat(ref_u4(r3.z))))).z;
// L247: mad r4.x, -r3.z, cb2[8].z, l(1.000000)
r4.x=(asuint((-asfloat(ref_u4(r3.z)))*ref_f4(scope_reference_cb[8].z)+ref_f4(1.000000))).x;
// L248: mul r4.y, r4.x, cb2[8].w
r4.y=(asuint(asfloat(ref_u4(r4.x))*ref_f4(scope_reference_cb[8].w))).y;
// L249: mul r4.y, r4.y, r4.y
r4.y=(asuint(asfloat(ref_u4(r4.y))*asfloat(ref_u4(r4.y)))).y;
// L250: mul r4.y, r4.y, l(1.442695)
r4.y=(asuint(asfloat(ref_u4(r4.y))*ref_f4(1.442695))).y;
// L251: exp r4.y, r4.y
r4.y=(asuint(dx_exp(asfloat(ref_u4(r4.y))))).y;
// L252: div r4.y, l(1.000000, 1.000000, 1.000000, 1.000000), r4.y
r4.y=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.y)))).y;
// L253: lt r4.z, l(0.000010), |r4.x|
r4.z=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r4.x)))))).z;
// L254: ge r4.x, r4.x, l(0.000000)
r4.x=(ref_mask(ref_ge(asfloat(ref_u4(r4.x)),ref_f4(0.000000)))).x;
// L255: add r4.y, -r4.y, l(1.000000)
r4.y=(asuint((-asfloat(ref_u4(r4.y)))+ref_f4(1.000000))).y;
// L256: add r4.y, -r4.y, l(1.000000)
r4.y=(asuint((-asfloat(ref_u4(r4.y)))+ref_f4(1.000000))).y;
// L257: movc r4.x, r4.x, r4.y, l(1.000000)
{uint4 c=ref_u4(r4.x),yes=ref_u4(r4.y),no=ref_u4(asuint(1.000000));r4.x=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).x;}
// L258: movc r4.x, r4.z, r4.x, l(1.000000)
{uint4 c=ref_u4(r4.z),yes=ref_u4(r4.x),no=ref_u4(asuint(1.000000));r4.x=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).x;}
// L259: add r4.y, -cb2[10].z, cb2[10].y
r4.y=(asuint((-ref_f4(scope_reference_cb[10].z))+ref_f4(scope_reference_cb[10].y))).y;
// L260: add r4.z, r3.w, -cb2[10].z
r4.z=(asuint(asfloat(ref_u4(r3.w))+(-ref_f4(scope_reference_cb[10].z)))).z;
// L261: div r4.y, l(1.000000, 1.000000, 1.000000, 1.000000), r4.y
r4.y=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.y)))).y;
// L262: mul_sat r4.y, r4.y, r4.z
r4.y=(asuint(dx_min(dx_max(asfloat(ref_u4(r4.y))*asfloat(ref_u4(r4.z)),ref_f4(0.)),ref_f4(1.)))).y;
// L263: mad r4.z, r4.y, l(-2.000000), l(3.000000)
r4.z=(asuint(asfloat(ref_u4(r4.y))*ref_f4(-2.000000)+ref_f4(3.000000))).z;
// L264: mul r4.y, r4.y, r4.y
r4.y=(asuint(asfloat(ref_u4(r4.y))*asfloat(ref_u4(r4.y)))).y;
// L265: mul r4.y, r4.y, r4.z
r4.y=(asuint(asfloat(ref_u4(r4.y))*asfloat(ref_u4(r4.z)))).y;
// L266: mad r4.y, r4.y, cb2[10].w, cb2[11].x
r4.y=(asuint(asfloat(ref_u4(r4.y))*ref_f4(scope_reference_cb[10].w)+ref_f4(scope_reference_cb[11].x))).y;
// L267: add [precise(z)] r4.z, -cb2[11].z, cb2[11].y
r4.z=(asuint((-ref_f4(scope_reference_cb[11].z))+ref_f4(scope_reference_cb[11].y))).z;
// L268: add [precise(x)] r5.x, r3.w, -cb2[11].z
r5.x=(asuint(asfloat(ref_u4(r3.w))+(-ref_f4(scope_reference_cb[11].z)))).x;
// L269: div [precise(z)] r4.z, l(1.000000, 1.000000, 1.000000, 1.000000), r4.z
r4.z=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.z)))).z;
// L270: mul_sat [precise(z)] r4.z, r4.z, r5.x
r4.z=(asuint(dx_min(dx_max(asfloat(ref_u4(r4.z))*asfloat(ref_u4(r5.x)),ref_f4(0.)),ref_f4(1.)))).z;
// L271: mul [precise(x)] r5.x, r4.z, l(-2.000000)
r5.x=(asuint(asfloat(ref_u4(r4.z))*ref_f4(-2.000000))).x;
// L272: add [precise(x)] r5.x, r5.x, l(3.000000)
r5.x=(asuint(asfloat(ref_u4(r5.x))+ref_f4(3.000000))).x;
// L273: mul [precise(z)] r4.z, r4.z, r4.z
r4.z=(asuint(asfloat(ref_u4(r4.z))*asfloat(ref_u4(r4.z)))).z;
// L274: mul [precise(z)] r4.z, r4.z, r5.x
r4.z=(asuint(asfloat(ref_u4(r4.z))*asfloat(ref_u4(r5.x)))).z;
// L275: ge [precise(x)] r5.x, l(0.000000), r4.z
r5.x=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r4.z))))).x;
// L276: log [precise(z)] r4.z, r4.z
r4.z=(asuint(dx_log(asfloat(ref_u4(r4.z))))).z;
// L277: mul [precise(z)] r4.z, r4.z, cb2[11].w
r4.z=(asuint(asfloat(ref_u4(r4.z))*ref_f4(scope_reference_cb[11].w))).z;
// L278: exp [precise(z)] r4.z, r4.z
r4.z=(asuint(dx_exp(asfloat(ref_u4(r4.z))))).z;
// L279: add [precise(xyz)] r6.xyz, cb2[12].xyzx, -cb2[13].xyzx
r6.xyz=(asuint(ref_f4(scope_reference_cb[12].xyzx)+(-ref_f4(scope_reference_cb[13].xyzx)))).xyz;
// L280: mul [precise(xyz)] r6.xyz, r4.zzzz, r6.xyzx
r6.xyz=(asuint(asfloat(ref_u4(r4.zzzz))*asfloat(ref_u4(r6.xyzx)))).xyz;
// L281: movc [precise(xyz)] r5.xyz, r5.xxxx, l(0,0,0,0), r6.xyzx
{uint4 c=ref_u4(r5.xxxx),yes=ref_u4(uint(0),uint(0),uint(0),uint(0)),no=ref_u4(r6.xyzx);r5.xyz=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).xyz;}
// L282: add r5.xyz, r5.xyzx, cb2[13].xyzx
r5.xyz=(asuint(asfloat(ref_u4(r5.xyzx))+ref_f4(scope_reference_cb[13].xyzx))).xyz;
// L283: mad r6.xy, -r8.zwzz, cb2[1].yyyy, r2.xyxx
r6.xy=(asuint((-asfloat(ref_u4(r8.zwzz)))*ref_f4(scope_reference_cb[1].yyyy)+asfloat(ref_u4(r2.xyxx)))).xy;
// L284: mad r6.xy, -r10.xyxx, cb2[2].xxxx, r6.xyxx
r6.xy=(asuint((-asfloat(ref_u4(r10.xyxx)))*ref_f4(scope_reference_cb[2].xxxx)+asfloat(ref_u4(r6.xyxx)))).xy;
// L285: mad r2.xy, -r9.zwzz, l(1.200000, 1.200000, 0.000000, 0.000000), r2.xyxx
r2.xy=(asuint((-asfloat(ref_u4(r9.zwzz)))*ref_f4(1.200000,1.200000,0.000000,0.000000)+asfloat(ref_u4(r2.xyxx)))).xy;
// L286: mad r2.xy, -r10.xyxx, cb2[2].xxxx, r2.xyxx
r2.xy=(asuint((-asfloat(ref_u4(r10.xyxx)))*ref_f4(scope_reference_cb[2].xxxx)+asfloat(ref_u4(r2.xyxx)))).xy;
// L287: mad r7.xy, r2.zwzz, l(0.800000, 0.800000, 0.000000, 0.000000), l(0.500000, 0.500000, 0.000000, 0.000000)
r7.xy=(asuint(asfloat(ref_u4(r2.zwzz))*ref_f4(0.800000,0.800000,0.000000,0.000000)+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L288: mad r7.xy, -r8.zwzz, cb2[1].yyyy, r7.xyxx
r7.xy=(asuint((-asfloat(ref_u4(r8.zwzz)))*ref_f4(scope_reference_cb[1].yyyy)+asfloat(ref_u4(r7.xyxx)))).xy;
// L289: mad r7.xy, -r10.xyxx, cb2[2].xxxx, r7.xyxx
r7.xy=(asuint((-asfloat(ref_u4(r10.xyxx)))*ref_f4(scope_reference_cb[2].xxxx)+asfloat(ref_u4(r7.xyxx)))).xy;
#ifdef SCOPE_REFERENCE_IMAGE_SAMPLE
SCOPE_REFERENCE_IMAGE_SAMPLE(sampleUv(2,asfloat(ref_u4(r6.xyxx)).xy));
#endif
// L290: sample_l_indexable(texture2d)(float,float,float,float) r10.xyz, r6.xyxx, t2.xyzw, s1, l(0.000000)
r10.xyz=(asuint(ref_sample_2(sampleUv(2,asfloat(ref_u4(r6.xyxx)).xy),ref_f4(0.000000).x).xyzw)).xyz;
// L291: sample_l_indexable(texture2d)(float,float,float,float) r12.xyz, r2.xyxx, t3.xyzw, s1, l(0.000000)
r12.xyz=(asuint(ref_sample_3(sampleUv(3,asfloat(ref_u4(r2.xyxx)).xy),ref_f4(0.000000).x).xyzw)).xyz;
// L292: sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r7.xyxx, t4.xyzw, s1, l(0.000000)
r7.xyz=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r7.xyxx)).xy),ref_f4(0.000000).x).xyzw)).xyz;
// L293: mul r13.xyz, r12.xyzx, cb1[155].zzzz
r13.xyz=(asuint(asfloat(ref_u4(r12.xyzx))*ref_f4(ref_f4(0.,0.,1.,1.).zzzz))).xyz;
// L294: dp3 r4.z, r13.xyzx, l(0.212639, 0.715169, 0.072192, 0.000000)
r4.z=(asuint(ref_f4(dot(asfloat(ref_u4(r13.xyzx)).xyz,ref_f4(0.212639,0.715169,0.072192,0.000000).xyz)))).z;
// L295: lt r6.z, l(0.000000), r5.w
r6.z=(ref_mask(ref_lt(ref_f4(0.000000),asfloat(ref_u4(r5.w))))).z;
// L296: if_nz r6.z
if(ref_u4(r6.z).x!=0u){
// L297: rsq r4.w, r4.w
r4.w=(asuint(rsqrt(asfloat(ref_u4(r4.w))))).w;
// L298: mul r2.zw, r2.zzzw, r4.wwww
r2.zw=(asuint(asfloat(ref_u4(r2.zzzw))*asfloat(ref_u4(r4.wwww)))).zw;
// L299: mov r13.xyz, l(0,0,0,0)
r13.xyz=(ref_u4(uint(0),uint(0),uint(0),uint(0))).xyz;
// L300: mov r14.xyz, l(0,0,0,0)
r14.xyz=(ref_u4(uint(0),uint(0),uint(0),uint(0))).xyz;
// L301: mov r4.w, l(-15)
r4.w=(ref_u4(uint(-15))).w;
// L302: loop
for(int guard=0;guard<512;guard++){
// L303: ige r6.z, r4.w, l(15)
r6.z=(ref_mask(ref_ge(ref_i4(ref_u4(r4.w)),ref_i4(ref_u4(uint(15)))))).z;
// L304: breakc_nz r6.z
if(ref_u4(r6.z).x!=0u)break;
// L305: itof r6.z, r4.w
r6.z=(asuint(ref_f4(ref_i4(ref_u4(r4.w))))).z;
// L306: mul r6.z, r6.z, l(0.000977)
r6.z=(asuint(asfloat(ref_u4(r6.z))*ref_f4(0.000977))).z;
// L307: mad r8.zw, r6.zzzz, r2.zzzw, r6.xxxy
r8.zw=(asuint(asfloat(ref_u4(r6.zzzz))*asfloat(ref_u4(r2.zzzw))+asfloat(ref_u4(r6.xxxy)))).zw;
// L308: sample_l_indexable(texture2d)(float,float,float,float) r15.xyz, r8.zwzz, t2.xyzw, s1, l(0.000000)
r15.xyz=(asuint(ref_sample_2(sampleUv(2,asfloat(ref_u4(r8.zwzz)).xy),ref_f4(0.000000).x).xyzw)).xyz;
// L309: mad r8.zw, r6.zzzz, r2.zzzw, r2.xxxy
r8.zw=(asuint(asfloat(ref_u4(r6.zzzz))*asfloat(ref_u4(r2.zzzw))+asfloat(ref_u4(r2.xxxy)))).zw;
// L310: sample_l_indexable(texture2d)(float,float,float,float) r16.xyz, r8.zwzz, t3.xyzw, s1, l(0.000000)
r16.xyz=(asuint(ref_sample_3(sampleUv(3,asfloat(ref_u4(r8.zwzz)).xy),ref_f4(0.000000).x).xyzw)).xyz;
// L311: add r13.xyz, r13.xyzx, r15.xyzx
r13.xyz=(asuint(asfloat(ref_u4(r13.xyzx))+asfloat(ref_u4(r15.xyzx)))).xyz;
// L312: add r14.xyz, r14.xyzx, r16.xyzx
r14.xyz=(asuint(asfloat(ref_u4(r14.xyzx))+asfloat(ref_u4(r16.xyzx)))).xyz;
// L313: iadd r4.w, r4.w, l(1)
r4.w=(ref_u4(ref_i4(ref_u4(r4.w))+ref_i4(ref_u4(uint(1))))).w;
// L314: endloop
}
// L315: mad r2.xyz, r13.xyzx, l(0.033333, 0.033333, 0.033333, 0.000000), -r10.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r13.xyzx))*ref_f4(0.033333,0.033333,0.033333,0.000000)+(-asfloat(ref_u4(r10.xyzx))))).xyz;
// L316: mad r10.xyz, r5.wwww, r2.xyzx, r10.xyzx
r10.xyz=(asuint(asfloat(ref_u4(r5.wwww))*asfloat(ref_u4(r2.xyzx))+asfloat(ref_u4(r10.xyzx)))).xyz;
// L317: mad r2.xyz, r14.xyzx, l(0.033333, 0.033333, 0.033333, 0.000000), -r12.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r14.xyzx))*ref_f4(0.033333,0.033333,0.033333,0.000000)+(-asfloat(ref_u4(r12.xyzx))))).xyz;
// L318: mad r12.xyz, r5.wwww, r2.xyzx, r12.xyzx
r12.xyz=(asuint(asfloat(ref_u4(r5.wwww))*asfloat(ref_u4(r2.xyzx))+asfloat(ref_u4(r12.xyzx)))).xyz;
// L319: endif
}
// L320: sample_l_indexable(texture2d)(float,float,float,float) r2.xyz, r6.xyxx, t3.xyzw, s1, l(0.000000)
r2.xyz=(asuint(ref_sample_3(sampleUv(3,asfloat(ref_u4(r6.xyxx)).xy),ref_f4(0.000000).x).xyzw)).xyz;
// L321: mul r6.xyz, r7.xyzx, cb2[14].wwww
r6.xyz=(asuint(asfloat(ref_u4(r7.xyzx))*ref_f4(scope_reference_cb[14].wwww))).xyz;
// L322: add r12.xyz, -r10.xyzx, r12.xyzx
r12.xyz=(asuint((-asfloat(ref_u4(r10.xyzx)))+asfloat(ref_u4(r12.xyzx)))).xyz;
// L323: mad r10.xyz, cb2[14].xyzx, r12.xyzx, r10.xyzx
r10.xyz=(asuint(ref_f4(scope_reference_cb[14].xyzx)*asfloat(ref_u4(r12.xyzx))+asfloat(ref_u4(r10.xyzx)))).xyz;
// L324: mad r6.xyz, r6.xyzx, cb2[15].xyzx, r10.xyzx
r6.xyz=(asuint(asfloat(ref_u4(r6.xyzx))*ref_f4(scope_reference_cb[15].xyzx)+asfloat(ref_u4(r10.xyzx)))).xyz;
// L325: mad_sat r2.w, r4.x, cb2[10].x, r6.w
r2.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r4.x))*ref_f4(scope_reference_cb[10].x)+asfloat(ref_u4(r6.w)),ref_f4(0.)),ref_f4(1.)))).w;
// L326: add r10.xyz, r2.xyzx, -r6.xyzx
r10.xyz=(asuint(asfloat(ref_u4(r2.xyzx))+(-asfloat(ref_u4(r6.xyzx))))).xyz;
// L327: mad r6.xyz, r2.wwww, r10.xyzx, r6.xyzx
r6.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r10.xyzx))+asfloat(ref_u4(r6.xyzx)))).xyz;
// L328: log r2.w, r4.z
r2.w=(asuint(dx_log(asfloat(ref_u4(r4.z))))).w;
// L329: mul r2.w, r2.w, cb2[16].y
r2.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(scope_reference_cb[16].y))).w;
// L330: exp r2.w, r2.w
r2.w=(asuint(dx_exp(asfloat(ref_u4(r2.w))))).w;
// L331: min r2.w, r2.w, l(1.000000)
r2.w=(asuint(dx_min(asfloat(ref_u4(r2.w)),ref_f4(1.000000)))).w;
// L332: add r2.w, r2.w, l(-1.000000)
r2.w=(asuint(asfloat(ref_u4(r2.w))+ref_f4(-1.000000))).w;
// L333: mad r2.w, cb2[16].x, r2.w, l(1.000000)
r2.w=(asuint(ref_f4(scope_reference_cb[16].x)*asfloat(ref_u4(r2.w))+ref_f4(1.000000))).w;
// L334: mul r2.w, r2.w, r4.y
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r4.y)))).w;
// L335: mad r4.xyz, r6.xyzx, r5.xyzx, -r6.xyzx
r4.xyz=(asuint(asfloat(ref_u4(r6.xyzx))*asfloat(ref_u4(r5.xyzx))+(-asfloat(ref_u4(r6.xyzx))))).xyz;
// L336: mad r4.xyz, r2.wwww, r4.xyzx, r6.xyzx
r4.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r4.xyzx))+asfloat(ref_u4(r6.xyzx)))).xyz;
// L337: mul r4.xyz, r11.xyzx, r4.xyzx
r4.xyz=(asuint(asfloat(ref_u4(r11.xyzx))*asfloat(ref_u4(r4.xyzx)))).xyz;
// L338: mad r6.xyz, r2.xyzx, r5.xyzx, -r2.xyzx
r6.xyz=(asuint(asfloat(ref_u4(r2.xyzx))*asfloat(ref_u4(r5.xyzx))+(-asfloat(ref_u4(r2.xyzx))))).xyz;
// L339: mad r2.xyz, r2.wwww, r6.xyzx, r2.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r6.xyzx))+asfloat(ref_u4(r2.xyzx)))).xyz;
// L340: mul r2.xyz, r11.xyzx, r2.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r11.xyzx))*asfloat(ref_u4(r2.xyzx)))).xyz;
// L341: mad r6.xyz, r7.xyzx, r5.xyzx, -r7.xyzx
r6.xyz=(asuint(asfloat(ref_u4(r7.xyzx))*asfloat(ref_u4(r5.xyzx))+(-asfloat(ref_u4(r7.xyzx))))).xyz;
// L342: mad r6.xyz, r2.wwww, r6.xyzx, r7.xyzx
r6.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r6.xyzx))+asfloat(ref_u4(r7.xyzx)))).xyz;
// L343: mul r6.xyz, r11.xyzx, r6.xyzx
r6.xyz=(asuint(asfloat(ref_u4(r11.xyzx))*asfloat(ref_u4(r6.xyzx)))).xyz;
// L344: mul [precise(w)] r2.w, r3.z, cb2[16].z
r2.w=(asuint(asfloat(ref_u4(r3.z))*ref_f4(scope_reference_cb[16].z))).w;
// L345: add [precise(w)] r2.w, -r2.w, l(1.000000)
r2.w=(asuint((-asfloat(ref_u4(r2.w)))+ref_f4(1.000000))).w;
// L346: mul [precise(w)] r4.w, r2.w, cb2[16].w
r4.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(scope_reference_cb[16].w))).w;
// L347: mul [precise(w)] r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L348: mul [precise(w)] r4.w, r4.w, l(1.442695)
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(1.442695))).w;
// L349: exp [precise(w)] r4.w, r4.w
r4.w=(asuint(dx_exp(asfloat(ref_u4(r4.w))))).w;
// L350: div [precise(w)] r4.w, l(1.000000, 1.000000, 1.000000, 1.000000), r4.w
r4.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.w)))).w;
// L351: lt [precise(w)] r5.w, l(0.000010), |r2.w|
r5.w=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r2.w)))))).w;
// L352: ge [precise(w)] r2.w, r2.w, l(0.000000)
r2.w=(ref_mask(ref_ge(asfloat(ref_u4(r2.w)),ref_f4(0.000000)))).w;
// L353: add [precise(w)] r4.w, -r4.w, l(1.000000)
r4.w=(asuint((-asfloat(ref_u4(r4.w)))+ref_f4(1.000000))).w;
// L354: and [precise(w)] r2.w, r2.w, r4.w
r2.w=(ref_u4(r2.w)&ref_u4(r4.w)).w;
// L355: and [precise(w)] r2.w, r2.w, r5.w
r2.w=(ref_u4(r2.w)&ref_u4(r5.w)).w;
// L356: mul [precise(xy)] r7.xy, r3.zzzz, cb2[17].xwxx
r7.xy=(asuint(asfloat(ref_u4(r3.zzzz))*ref_f4(scope_reference_cb[17].xwxx))).xy;
// L357: add [precise(xy)] r7.xy, -r7.xyxx, l(1.000000, 1.000000, 0.000000, 0.000000)
r7.xy=(asuint((-asfloat(ref_u4(r7.xyxx)))+ref_f4(1.000000,1.000000,0.000000,0.000000))).xy;
// L358: mul [precise(w)] r4.w, r7.x, cb2[17].y
r4.w=(asuint(asfloat(ref_u4(r7.x))*ref_f4(scope_reference_cb[17].y))).w;
// L359: mul [precise(w)] r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L360: mul [precise(w)] r4.w, r4.w, l(1.442695)
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(1.442695))).w;
// L361: exp [precise(w)] r4.w, r4.w
r4.w=(asuint(dx_exp(asfloat(ref_u4(r4.w))))).w;
// L362: div [precise(w)] r4.w, l(1.000000, 1.000000, 1.000000, 1.000000), r4.w
r4.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.w)))).w;
// L363: lt [precise(zw)] r8.zw, l(0.000000, 0.000000, 0.000010, 0.000010), |r7.xxxy|
r8.zw=(ref_mask(ref_lt(ref_f4(0.000000,0.000000,0.000010,0.000010),abs(asfloat(ref_u4(r7.xxxy)))))).zw;
// L364: ge [precise(xz)] r7.xz, r7.xxyx, l(0.000000, 0.000000, 0.000000, 0.000000)
r7.xz=(ref_mask(ref_ge(asfloat(ref_u4(r7.xxyx)),ref_f4(0.000000,0.000000,0.000000,0.000000)))).xz;
// L365: add [precise(w)] r4.w, -r4.w, l(1.000000)
r4.w=(asuint((-asfloat(ref_u4(r4.w)))+ref_f4(1.000000))).w;
// L366: and [precise(w)] r4.w, r4.w, r7.x
r4.w=(ref_u4(r4.w)&ref_u4(r7.x)).w;
// L367: and [precise(w)] r4.w, r4.w, r8.z
r4.w=(ref_u4(r4.w)&ref_u4(r8.z)).w;
// L368: mul [precise(w)] r5.w, r4.w, cb2[17].z
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(scope_reference_cb[17].z))).w;
// L369: add_sat [precise(x)] r10.x, r2.w, r5.w
r10.x=(asuint(dx_min(dx_max(asfloat(ref_u4(r2.w))+asfloat(ref_u4(r5.w)),ref_f4(0.)),ref_f4(1.)))).x;
// L370: mul [precise(w)] r2.w, r7.y, cb2[18].x
r2.w=(asuint(asfloat(ref_u4(r7.y))*ref_f4(scope_reference_cb[18].x))).w;
// L371: mul [precise(w)] r2.w, r2.w, r2.w
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r2.w)))).w;
// L372: mul [precise(w)] r2.w, r2.w, l(1.442695)
r2.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(1.442695))).w;
// L373: exp [precise(w)] r2.w, r2.w
r2.w=(asuint(dx_exp(asfloat(ref_u4(r2.w))))).w;
// L374: div [precise(w)] r2.w, l(1.000000, 1.000000, 1.000000, 1.000000), r2.w
r2.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r2.w)))).w;
// L375: add [precise(w)] r2.w, -r2.w, l(1.000000)
r2.w=(asuint((-asfloat(ref_u4(r2.w)))+ref_f4(1.000000))).w;
// L376: and [precise(w)] r2.w, r2.w, r7.z
r2.w=(ref_u4(r2.w)&ref_u4(r7.z)).w;
// L377: and [precise(w)] r2.w, r2.w, r8.w
r2.w=(ref_u4(r2.w)&ref_u4(r8.w)).w;
// L378: mul [precise(w)] r5.w, r4.w, cb2[18].y
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(scope_reference_cb[18].y))).w;
// L379: add_sat [precise(y)] r10.y, r2.w, r5.w
r10.y=(asuint(dx_min(dx_max(asfloat(ref_u4(r2.w))+asfloat(ref_u4(r5.w)),ref_f4(0.)),ref_f4(1.)))).y;
// L380: mul [precise(w)] r2.w, r3.z, cb2[18].z
r2.w=(asuint(asfloat(ref_u4(r3.z))*ref_f4(scope_reference_cb[18].z))).w;
// L381: add [precise(w)] r2.w, -r2.w, l(1.000000)
r2.w=(asuint((-asfloat(ref_u4(r2.w)))+ref_f4(1.000000))).w;
// L382: mul [precise(z)] r3.z, r2.w, cb2[18].w
r3.z=(asuint(asfloat(ref_u4(r2.w))*ref_f4(scope_reference_cb[18].w))).z;
// L383: mul [precise(z)] r3.z, r3.z, r3.z
r3.z=(asuint(asfloat(ref_u4(r3.z))*asfloat(ref_u4(r3.z)))).z;
// L384: mul [precise(z)] r3.z, r3.z, l(1.442695)
r3.z=(asuint(asfloat(ref_u4(r3.z))*ref_f4(1.442695))).z;
// L385: exp [precise(z)] r3.z, r3.z
r3.z=(asuint(dx_exp(asfloat(ref_u4(r3.z))))).z;
// L386: div [precise(z)] r3.z, l(1.000000, 1.000000, 1.000000, 1.000000), r3.z
r3.z=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r3.z)))).z;
// L387: lt [precise(w)] r5.w, l(0.000010), |r2.w|
r5.w=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r2.w)))))).w;
// L388: ge [precise(w)] r2.w, r2.w, l(0.000000)
r2.w=(ref_mask(ref_ge(asfloat(ref_u4(r2.w)),ref_f4(0.000000)))).w;
// L389: add [precise(z)] r3.z, -r3.z, l(1.000000)
r3.z=(asuint((-asfloat(ref_u4(r3.z)))+ref_f4(1.000000))).z;
// L390: and [precise(w)] r2.w, r2.w, r3.z
r2.w=(ref_u4(r2.w)&ref_u4(r3.z)).w;
// L391: and [precise(w)] r2.w, r2.w, r5.w
r2.w=(ref_u4(r2.w)&ref_u4(r5.w)).w;
// L392: mul [precise(z)] r3.z, r4.w, cb2[19].x
r3.z=(asuint(asfloat(ref_u4(r4.w))*ref_f4(scope_reference_cb[19].x))).z;
// L393: add_sat [precise(z)] r10.z, r2.w, r3.z
r10.z=(asuint(dx_min(dx_max(asfloat(ref_u4(r2.w))+asfloat(ref_u4(r3.z)),ref_f4(0.)),ref_f4(1.)))).z;
// L394: mul r2.w, r3.y, cb2[19].y
r2.w=(asuint(asfloat(ref_u4(r3.y))*ref_f4(scope_reference_cb[19].y))).w;
// L395: mul r2.w, r2.w, r3.x
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r3.x)))).w;
// L396: mad r2.w, l(0.050000), r2.w, l(-0.025000)
r2.w=(asuint(ref_f4(0.050000)*asfloat(ref_u4(r2.w))+ref_f4(-0.025000))).w;
// L397: mad r3.xy, r9.xyxx, r2.wwww, v3.xyxx
r3.xy=(asuint(ref_f4(SCOPE_REFERENCE_PUPIL_VIEW(v3.xy,asfloat(r9.xy)).xyxy)*asfloat(ref_u4(r2.wwww))+ref_f4(v3.xyxx))).xy;
// L398: mad r3.xy, r3.xyxx, l(3.000300, 3.000300, 0.000000, 0.000000), l(-1.500150, -1.500150, 0.000000, 0.000000)
r3.xy=(asuint(asfloat(ref_u4(r3.xyxx))*ref_f4(3.000300,3.000300,0.000000,0.000000)+ref_f4(-1.500150,-1.500150,0.000000,0.000000))).xy;
// L399: dp2 r2.w, r3.xyxx, r3.xyxx
r2.w=(asuint(ref_f4(dot(asfloat(ref_u4(r3.xyxx)).xy,asfloat(ref_u4(r3.xyxx)).xy)))).w;
// L400: sqrt r2.w, r2.w
r2.w=(asuint(sqrt(asfloat(ref_u4(r2.w))))).w;
// L401: mad r3.x, -r2.w, cb2[19].z, l(1.000000)
r3.x=(asuint((-asfloat(ref_u4(r2.w)))*ref_f4(scope_reference_cb[19].z)+ref_f4(1.000000))).x;
// L402: mul r3.y, r3.x, cb2[19].w
r3.y=(asuint(asfloat(ref_u4(r3.x))*ref_f4(scope_reference_cb[19].w))).y;
// L403: mul r3.y, r3.y, r3.y
r3.y=(asuint(asfloat(ref_u4(r3.y))*asfloat(ref_u4(r3.y)))).y;
// L404: mul r3.y, r3.y, l(1.442695)
r3.y=(asuint(asfloat(ref_u4(r3.y))*ref_f4(1.442695))).y;
// L405: exp r3.y, r3.y
r3.y=(asuint(dx_exp(asfloat(ref_u4(r3.y))))).y;
// L406: div r3.y, l(1.000000, 1.000000, 1.000000, 1.000000), r3.y
r3.y=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r3.y)))).y;
// L407: lt r3.z, l(0.000010), |r3.x|
r3.z=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r3.x)))))).z;
// L408: ge r3.x, r3.x, l(0.000000)
r3.x=(ref_mask(ref_ge(asfloat(ref_u4(r3.x)),ref_f4(0.000000)))).x;
// L409: add r3.y, -r3.y, l(1.000000)
r3.y=(asuint((-asfloat(ref_u4(r3.y)))+ref_f4(1.000000))).y;
// L410: and r3.x, r3.y, r3.x
r3.x=(ref_u4(r3.y)&ref_u4(r3.x)).x;
// L411: and r3.x, r3.x, r3.z
r3.x=(ref_u4(r3.x)&ref_u4(r3.z)).x;
// L412: mad r3.yz, -r2.wwww, cb2[20].xxwx, l(0.000000, 1.000000, 1.000000, 0.000000)
r3.yz=(asuint((-asfloat(ref_u4(r2.wwww)))*ref_f4(scope_reference_cb[20].xxwx)+ref_f4(0.000000,1.000000,1.000000,0.000000))).yz;
// L413: mul r4.w, r3.y, cb2[20].y
r4.w=(asuint(asfloat(ref_u4(r3.y))*ref_f4(scope_reference_cb[20].y))).w;
// L414: mul r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L415: mul r4.w, r4.w, l(1.442695)
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(1.442695))).w;
// L416: exp r4.w, r4.w
r4.w=(asuint(dx_exp(asfloat(ref_u4(r4.w))))).w;
// L417: div r4.w, l(1.000000, 1.000000, 1.000000, 1.000000), r4.w
r4.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.w)))).w;
// L418: lt r7.xy, l(0.000010, 0.000010, 0.000000, 0.000000), |r3.yzyy|
r7.xy=(ref_mask(ref_lt(ref_f4(0.000010,0.000010,0.000000,0.000000),abs(asfloat(ref_u4(r3.yzyy)))))).xy;
// L419: ge r8.zw, r3.yyyz, l(0.000000, 0.000000, 0.000000, 0.000000)
r8.zw=(ref_mask(ref_ge(asfloat(ref_u4(r3.yyyz)),ref_f4(0.000000,0.000000,0.000000,0.000000)))).zw;
// L420: add r3.y, -r4.w, l(1.000000)
r3.y=(asuint((-asfloat(ref_u4(r4.w)))+ref_f4(1.000000))).y;
// L421: and r3.y, r3.y, r8.z
r3.y=(ref_u4(r3.y)&ref_u4(r8.z)).y;
// L422: and r3.y, r3.y, r7.x
r3.y=(ref_u4(r3.y)&ref_u4(r7.x)).y;
// L423: mad_sat r11.x, cb2[20].z, r3.y, r3.x
r11.x=(asuint(dx_min(dx_max(ref_f4(scope_reference_cb[20].z)*asfloat(ref_u4(r3.y))+asfloat(ref_u4(r3.x)),ref_f4(0.)),ref_f4(1.)))).x;
// L424: mul r3.x, r3.z, cb2[21].x
r3.x=(asuint(asfloat(ref_u4(r3.z))*ref_f4(scope_reference_cb[21].x))).x;
// L425: mul r3.x, r3.x, r3.x
r3.x=(asuint(asfloat(ref_u4(r3.x))*asfloat(ref_u4(r3.x)))).x;
// L426: mul r3.x, r3.x, l(1.442695)
r3.x=(asuint(asfloat(ref_u4(r3.x))*ref_f4(1.442695))).x;
// L427: exp r3.x, r3.x
r3.x=(asuint(dx_exp(asfloat(ref_u4(r3.x))))).x;
// L428: div r3.x, l(1.000000, 1.000000, 1.000000, 1.000000), r3.x
r3.x=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r3.x)))).x;
// L429: add r3.x, -r3.x, l(1.000000)
r3.x=(asuint((-asfloat(ref_u4(r3.x)))+ref_f4(1.000000))).x;
// L430: and r3.x, r3.x, r8.w
r3.x=(ref_u4(r3.x)&ref_u4(r8.w)).x;
// L431: and r3.x, r3.x, r7.y
r3.x=(ref_u4(r3.x)&ref_u4(r7.y)).x;
// L432: mad_sat r11.y, cb2[21].y, r3.y, r3.x
r11.y=(asuint(dx_min(dx_max(ref_f4(scope_reference_cb[21].y)*asfloat(ref_u4(r3.y))+asfloat(ref_u4(r3.x)),ref_f4(0.)),ref_f4(1.)))).y;
// L433: mad r2.w, -r2.w, cb2[21].z, l(1.000000)
r2.w=(asuint((-asfloat(ref_u4(r2.w)))*ref_f4(scope_reference_cb[21].z)+ref_f4(1.000000))).w;
// L434: mul r3.x, r2.w, cb2[21].w
r3.x=(asuint(asfloat(ref_u4(r2.w))*ref_f4(scope_reference_cb[21].w))).x;
// L435: mul r3.x, r3.x, r3.x
r3.x=(asuint(asfloat(ref_u4(r3.x))*asfloat(ref_u4(r3.x)))).x;
// L436: mul r3.x, r3.x, l(1.442695)
r3.x=(asuint(asfloat(ref_u4(r3.x))*ref_f4(1.442695))).x;
// L437: exp r3.x, r3.x
r3.x=(asuint(dx_exp(asfloat(ref_u4(r3.x))))).x;
// L438: div r3.x, l(1.000000, 1.000000, 1.000000, 1.000000), r3.x
r3.x=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r3.x)))).x;
// L439: lt r3.z, l(0.000010), |r2.w|
r3.z=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r2.w)))))).z;
// L440: ge r2.w, r2.w, l(0.000000)
r2.w=(ref_mask(ref_ge(asfloat(ref_u4(r2.w)),ref_f4(0.000000)))).w;
// L441: add r3.x, -r3.x, l(1.000000)
r3.x=(asuint((-asfloat(ref_u4(r3.x)))+ref_f4(1.000000))).x;
// L442: and r2.w, r2.w, r3.x
r2.w=(ref_u4(r2.w)&ref_u4(r3.x)).w;
// L443: and r2.w, r2.w, r3.z
r2.w=(ref_u4(r2.w)&ref_u4(r3.z)).w;
// L444: mad_sat r11.z, cb2[22].x, r3.y, r2.w
r11.z=(asuint(dx_min(dx_max(ref_f4(scope_reference_cb[22].x)*asfloat(ref_u4(r3.y))+asfloat(ref_u4(r2.w)),ref_f4(0.)),ref_f4(1.)))).z;
// L445: mul r3.xyz, r10.xyzx, r11.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r10.xyzx))*asfloat(ref_u4(r11.xyzx)))).xyz;
#ifdef SCOPE_REFERENCE_PUPIL_TRANSMISSION
SCOPE_REFERENCE_PUPIL_TRANSMISSION(asfloat(r3.xyz));
#endif
// L446: mul r2.xyz, r2.xyzx, r3.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r2.xyzx))*asfloat(ref_u4(r3.xyzx)))).xyz;
// L447: mul r4.xyz, r3.xyzx, r4.xyzx
r4.xyz=(asuint(asfloat(ref_u4(r3.xyzx))*asfloat(ref_u4(r4.xyzx)))).xyz;
// L448: mad r2.w, cb2[22].y, l(-0.050000), l(0.025000)
r2.w=(asuint(ref_f4(SCOPE_REFERENCE_RETICLE_CB22.y)*ref_f4(-0.050000)+ref_f4(0.025000))).w;
// L449: mad r7.xy, r9.xyxx, r2.wwww, v3.xyxx
r7.xy=(asuint(ref_f4(SCOPE_REFERENCE_RETICLE_VIEW(v3.xy,asfloat(r9.xy)).xyxy)*asfloat(ref_u4(r2.wwww))+ref_f4(v3.xyxx))).xy;
// L450: mad r7.xy, r7.xyxx, l(0.033333, 0.033333, 0.000000, 0.000000), l(-0.016667, -0.016667, 0.000000, 0.000000)
r7.xy=(asuint(asfloat(ref_u4(r7.xyxx))*ref_f4(0.033333,0.033333,0.000000,0.000000)+ref_f4(-0.016667,-0.016667,0.000000,0.000000))).xy;
// L451: mad r7.xy, r7.xyxx, cb2[22].zwzz, l(0.500000, 0.500000, 0.000000, 0.000000)
r7.xy=(asuint(asfloat(ref_u4(r7.xyxx))*ref_f4(SCOPE_REFERENCE_RETICLE_CB22.zwzz)+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L452: add r7.xy, r7.xyxx, -cb2[23].xyxx
r7.xy=(asuint(asfloat(ref_u4(r7.xyxx))+(-ref_f4(scope_reference_cb[23].xyxx)))).xy;
// L453: mad r2.w, cb2[23].z, l(-0.050000), l(0.025000)
r2.w=(asuint(ref_f4(scope_reference_cb[23].z)*ref_f4(-0.050000)+ref_f4(0.025000))).w;
// L454: mad r8.zw, r9.xxxy, r2.wwww, v3.xxxy
r8.zw=(asuint(asfloat(ref_u4(r9.xxxy))*asfloat(ref_u4(r2.wwww))+ref_f4(v3.xxxy))).zw;
// L455: add r8.zw, r8.zzzw, l(0.000000, 0.000000, -0.500000, -0.500000)
r8.zw=(asuint(asfloat(ref_u4(r8.zzzw))+ref_f4(0.000000,0.000000,-0.500000,-0.500000))).zw;
// L456: add [precise(w)] r2.w, cb2[23].w, -cb2[24].x
r2.w=(asuint(ref_f4(scope_reference_cb[23].w)+(-ref_f4(scope_reference_cb[24].x)))).w;
// L457: add [precise(zw)] r9.zw, r3.wwww, -cb2[24].xxxw
r9.zw=(asuint(asfloat(ref_u4(r3.wwww))+(-ref_f4(scope_reference_cb[24].xxxw)))).zw;
// L458: div [precise(w)] r2.w, l(1.000000, 1.000000, 1.000000, 1.000000), r2.w
r2.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r2.w)))).w;
// L459: mul_sat [precise(w)] r2.w, r2.w, r9.z
r2.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r9.z)),ref_f4(0.)),ref_f4(1.)))).w;
// L460: mul [precise(w)] r4.w, r2.w, l(-2.000000)
r4.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(-2.000000))).w;
// L461: add [precise(w)] r4.w, r4.w, l(3.000000)
r4.w=(asuint(asfloat(ref_u4(r4.w))+ref_f4(3.000000))).w;
// L462: mul [precise(w)] r2.w, r2.w, r2.w
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r2.w)))).w;
// L463: mul [precise(w)] r2.w, r2.w, r4.w
r2.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r4.w)))).w;
// L464: ge [precise(w)] r4.w, l(0.000000), r2.w
r4.w=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r2.w))))).w;
// L465: log [precise(w)] r2.w, r2.w
r2.w=(asuint(dx_log(asfloat(ref_u4(r2.w))))).w;
// L466: mul [precise(w)] r2.w, r2.w, cb2[24].y
r2.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(scope_reference_cb[24].y))).w;
// L467: exp [precise(w)] r2.w, r2.w
r2.w=(asuint(dx_exp(asfloat(ref_u4(r2.w))))).w;
// L468: movc [precise(w)] r2.w, r4.w, l(0), r2.w
{uint4 c=ref_u4(r4.w),yes=ref_u4(uint(0)),no=ref_u4(r2.w);r2.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L469: add [precise(w)] r4.w, -cb2[24].w, cb2[24].z
r4.w=(asuint((-ref_f4(scope_reference_cb[24].w))+ref_f4(scope_reference_cb[24].z))).w;
// L470: div [precise(w)] r4.w, l(1.000000, 1.000000, 1.000000, 1.000000), r4.w
r4.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.w)))).w;
// L471: mul_sat [precise(w)] r4.w, r4.w, r9.w
r4.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r9.w)),ref_f4(0.)),ref_f4(1.)))).w;
// L472: mul [precise(w)] r5.w, r4.w, l(-2.000000)
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-2.000000))).w;
// L473: add [precise(w)] r5.w, r5.w, l(3.000000)
r5.w=(asuint(asfloat(ref_u4(r5.w))+ref_f4(3.000000))).w;
// L474: mul [precise(w)] r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L475: mul [precise(w)] r4.w, r4.w, r5.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r5.w)))).w;
// L476: ge [precise(w)] r5.w, l(0.000000), r4.w
r5.w=(ref_mask(ref_ge(ref_f4(0.000000),asfloat(ref_u4(r4.w))))).w;
// L477: log [precise(w)] r4.w, r4.w
r4.w=(asuint(dx_log(asfloat(ref_u4(r4.w))))).w;
// L478: mul [precise(w)] r4.w, r4.w, cb2[25].x
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(scope_reference_cb[25].x))).w;
// L479: exp [precise(w)] r4.w, r4.w
r4.w=(asuint(dx_exp(asfloat(ref_u4(r4.w))))).w;
// L480: movc [precise(w)] r4.w, r5.w, l(0), r4.w
{uint4 c=ref_u4(r5.w),yes=ref_u4(uint(0)),no=ref_u4(r4.w);r4.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L481: add r4.w, -r2.w, r4.w
r4.w=(asuint((-asfloat(ref_u4(r2.w)))+asfloat(ref_u4(r4.w)))).w;
// L482: mad r2.w, cb2[25].y, r4.w, r2.w
r2.w=(asuint(ref_f4(scope_reference_cb[25].y)*asfloat(ref_u4(r4.w))+asfloat(ref_u4(r2.w)))).w;
// L483: mul r8.zw, r2.wwww, r8.zzzw
r8.zw=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r8.zzzw)))).zw;
// L484: mad r8.zw, -r8.zzzw, cb2[25].zzzz, r7.xxxy
r8.zw=(asuint((-asfloat(ref_u4(r8.zzzw)))*ref_f4(scope_reference_cb[25].zzzz)+asfloat(ref_u4(r7.xxxy)))).zw;
// L485: add [precise(w)] r2.w, r10.y, r10.x
r2.w=(asuint(asfloat(ref_u4(r10.y))+asfloat(ref_u4(r10.x)))).w;
// L486: add [precise(w)] r2.w, r10.z, r2.w
r2.w=(asuint(asfloat(ref_u4(r10.z))+asfloat(ref_u4(r2.w)))).w;
// L487: mul [precise(w)] r2.w, r2.w, l(0.333333)
r2.w=(asuint(asfloat(ref_u4(r2.w))*ref_f4(0.333333))).w;
// L488: add [precise(w)] r2.w, -r2.w, l(1.000000)
r2.w=(asuint((-asfloat(ref_u4(r2.w)))+ref_f4(1.000000))).w;
// L489: mul [precise(w)] r4.w, r3.w, cb2[25].w
r4.w=(asuint(asfloat(ref_u4(r3.w))*ref_f4(scope_reference_cb[25].w))).w;
// L490: add [precise(w)] r4.w, -r4.w, l(1.000000)
r4.w=(asuint((-asfloat(ref_u4(r4.w)))+ref_f4(1.000000))).w;
// L491: mul [precise(w)] r5.w, r4.w, cb2[26].x
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(scope_reference_cb[26].x))).w;
// L492: mul [precise(w)] r5.w, r5.w, r5.w
r5.w=(asuint(asfloat(ref_u4(r5.w))*asfloat(ref_u4(r5.w)))).w;
// L493: mul [precise(w)] r5.w, r5.w, l(1.442695)
r5.w=(asuint(asfloat(ref_u4(r5.w))*ref_f4(1.442695))).w;
// L494: exp [precise(w)] r5.w, r5.w
r5.w=(asuint(dx_exp(asfloat(ref_u4(r5.w))))).w;
// L495: div [precise(w)] r5.w, l(1.000000, 1.000000, 1.000000, 1.000000), r5.w
r5.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r5.w)))).w;
// L496: lt [precise(w)] r6.w, l(0.000010), |r4.w|
r6.w=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r4.w)))))).w;
// L497: ge [precise(w)] r4.w, r4.w, l(0.000000)
r4.w=(ref_mask(ref_ge(asfloat(ref_u4(r4.w)),ref_f4(0.000000)))).w;
// L498: add [precise(w)] r5.w, -r5.w, l(1.000000)
r5.w=(asuint((-asfloat(ref_u4(r5.w)))+ref_f4(1.000000))).w;
// L499: add [precise(w)] r5.w, -r5.w, l(1.000000)
r5.w=(asuint((-asfloat(ref_u4(r5.w)))+ref_f4(1.000000))).w;
// L500: movc [precise(w)] r4.w, r4.w, r5.w, l(1.000000)
{uint4 c=ref_u4(r4.w),yes=ref_u4(r5.w),no=ref_u4(asuint(1.000000));r4.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L501: movc [precise(w)] r4.w, r6.w, r4.w, l(1.000000)
{uint4 c=ref_u4(r6.w),yes=ref_u4(r4.w),no=ref_u4(asuint(1.000000));r4.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L502: mul [precise(w)] r5.w, r2.w, r4.w
r5.w=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r4.w)))).w;
// L503: rsq r0.z, r0.z
r0.z=(asuint(rsqrt(asfloat(ref_u4(r0.z))))).z;
// L504: mul r8.xy, r0.zzzz, r8.xyxx
r8.xy=(asuint(asfloat(ref_u4(r0.zzzz))*asfloat(ref_u4(r8.xyxx)))).xy;
// L505: sample_l_indexable(texture2d)(float,float,float,float) r10.xyz, r8.zwzz, t5.yxzw, s1, l(0.000000)
r10.xyz=(asuint(ref_sample_5(sampleUv(5,asfloat(ref_u4(r8.zwzz)).xy),ref_f4(0.000000).x).yxzw)).xyz;
// L506: add r11.xyz, cb2[27].wwww, cb2[27].xyzx
r11.xyz=(asuint(ref_f4(scope_reference_cb[27].wwww)+ref_f4(scope_reference_cb[27].xyzx))).xyz;
// L507: add r0.z, -r10.y, r10.z
r0.z=(asuint((-asfloat(ref_u4(r10.y)))+asfloat(ref_u4(r10.z)))).z;
// L508: mad r10.yzw, r11.xxyz, r0.zzzz, r10.yyyy
r10.yzw=(asuint(asfloat(ref_u4(r11.xxyz))*asfloat(ref_u4(r0.zzzz))+asfloat(ref_u4(r10.yyyy)))).yzw;
// L509: lt [precise(z)] r0.z, l(0.000000), r5.w
r0.z=(ref_mask(ref_lt(ref_f4(0.000000),asfloat(ref_u4(r5.w))))).z;
// L510: if_nz r0.z
if(ref_u4(r0.z).x!=0u){
// L511: mul [precise(w)] r6.w, r5.w, cb2[28].x
r6.w=(asuint(asfloat(ref_u4(r5.w))*ref_f4(scope_reference_cb[28].x))).w;
// L512: ftoi [precise(w)] r6.w, r6.w
r6.w=(ref_u4(ref_i4(trunc(asfloat(ref_u4(r6.w)))))).w;
// L513: imax [precise(w)] r6.w, r6.w, l(2)
r6.w=(ref_u4(max(ref_i4(ref_u4(r6.w)),ref_i4(ref_u4(uint(2)))))).w;
// L514: ushr [precise(w)] r6.w, r6.w, l(1)
r6.w=(ref_u4(r6.w)>>ref_u4(uint(1))).w;
// L515: ineg [precise(z)] r7.z, r6.w
r7.z=(ref_u4(-ref_i4(ref_u4(r6.w)))).z;
// L516: mov r12.xyz, l(0,0,0,0)
r12.xyz=(ref_u4(uint(0),uint(0),uint(0),uint(0))).xyz;
// L517: mov r9.z, l(0)
r9.z=(ref_u4(uint(0))).z;
// L518: mov [precise(w)] r9.w, r7.z
r9.w=(ref_u4(r7.z)).w;
// L519: loop
for(int guard=0;guard<512;guard++){
// L520: ige [precise(w)] r11.w, r9.w, r6.w
r11.w=(ref_mask(ref_ge(ref_i4(ref_u4(r9.w)),ref_i4(ref_u4(r6.w))))).w;
// L521: breakc_nz r11.w
if(ref_u4(r11.w).x!=0u)break;
// L522: itof r11.w, r9.w
r11.w=(asuint(ref_f4(ref_i4(ref_u4(r9.w))))).w;
// L523: mul r11.w, r11.w, cb2[28].y
r11.w=(asuint(asfloat(ref_u4(r11.w))*ref_f4(scope_reference_cb[28].y))).w;
// L524: mad r13.xy, r11.wwww, r8.xyxx, r8.zwzz
r13.xy=(asuint(asfloat(ref_u4(r11.wwww))*asfloat(ref_u4(r8.xyxx))+asfloat(ref_u4(r8.zwzz)))).xy;
// L525: sample_l_indexable(texture2d)(float,float,float,float) r13.xyz, r13.xyxx, t5.xyzw, s1, l(0.000000)
r13.xyz=(asuint(ref_sample_5(sampleUv(5,asfloat(ref_u4(r13.xyxx)).xy),ref_f4(0.000000).x).xyzw)).xyz;
// L526: movc [precise(w)] r11.w, r9.w, r9.w, l(1)
{uint4 c=ref_u4(r9.w),yes=ref_u4(r9.w),no=ref_u4(uint(1));r11.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L527: imax r11.w, -r11.w, r11.w
r11.w=(ref_u4(max((-ref_i4(ref_u4(r11.w))),ref_i4(ref_u4(r11.w))))).w;
// L528: itof r11.w, r11.w
r11.w=(asuint(ref_f4(ref_i4(ref_u4(r11.w))))).w;
// L529: log r11.w, r11.w
r11.w=(asuint(dx_log(asfloat(ref_u4(r11.w))))).w;
// L530: mul r11.w, r11.w, cb2[28].z
r11.w=(asuint(asfloat(ref_u4(r11.w))*ref_f4(scope_reference_cb[28].z))).w;
// L531: exp r11.w, r11.w
r11.w=(asuint(dx_exp(asfloat(ref_u4(r11.w))))).w;
// L532: div r11.w, l(1.000000, 1.000000, 1.000000, 1.000000), r11.w
r11.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r11.w)))).w;
// L533: mad r12.xyz, r13.xyzx, r11.wwww, r12.xyzx
r12.xyz=(asuint(asfloat(ref_u4(r13.xyzx))*asfloat(ref_u4(r11.wwww))+asfloat(ref_u4(r12.xyzx)))).xyz;
// L534: add r9.z, r9.z, r11.w
r9.z=(asuint(asfloat(ref_u4(r9.z))+asfloat(ref_u4(r11.w)))).z;
// L535: iadd [precise(w)] r9.w, r9.w, l(1)
r9.w=(ref_u4(ref_i4(ref_u4(r9.w))+ref_i4(ref_u4(uint(1))))).w;
// L536: endloop
}
// L537: div r12.xyz, r12.xyzx, r9.zzzz
r12.xyz=(asuint(asfloat(ref_u4(r12.xyzx))/asfloat(ref_u4(r9.zzzz)))).xyz;
// L538: add r6.w, -r12.x, r12.z
r6.w=(asuint((-asfloat(ref_u4(r12.x)))+asfloat(ref_u4(r12.z)))).w;
// L539: mad r11.xyz, r11.xyzx, r6.wwww, r12.xxxx
r11.xyz=(asuint(asfloat(ref_u4(r11.xyzx))*asfloat(ref_u4(r6.wwww))+asfloat(ref_u4(r12.xxxx)))).xyz;
// L540: log r6.w, r5.w
r6.w=(asuint(dx_log(asfloat(ref_u4(r5.w))))).w;
// L541: mul r6.w, r6.w, cb2[28].w
r6.w=(asuint(asfloat(ref_u4(r6.w))*ref_f4(scope_reference_cb[28].w))).w;
// L542: exp r6.w, r6.w
r6.w=(asuint(dx_exp(asfloat(ref_u4(r6.w))))).w;
// L543: min r6.w, r6.w, l(1.000000)
r6.w=(asuint(dx_min(asfloat(ref_u4(r6.w)),ref_f4(1.000000)))).w;
// L544: add r11.xyz, -r10.yzwy, r11.xyzx
r11.xyz=(asuint((-asfloat(ref_u4(r10.yzwy)))+asfloat(ref_u4(r11.xyzx)))).xyz;
// L545: mad r10.yzw, r6.wwww, r11.xxyz, r10.yyzw
r10.yzw=(asuint(asfloat(ref_u4(r6.wwww))*asfloat(ref_u4(r11.xxyz))+asfloat(ref_u4(r10.yyzw)))).yzw;
// L546: add r7.z, -r10.x, r12.y
r7.z=(asuint((-asfloat(ref_u4(r10.x)))+asfloat(ref_u4(r12.y)))).z;
// L547: mad r10.x, r6.w, r7.z, r10.x
r10.x=(asuint(asfloat(ref_u4(r6.w))*asfloat(ref_u4(r7.z))+asfloat(ref_u4(r10.x)))).x;
// L548: endif
}
// L549: mul r6.w, r10.x, cb2[26].y
r6.w=(asuint(asfloat(ref_u4(r10.x))*ref_f4(scope_reference_cb[26].y))).w;
// L550: log r6.w, r6.w
r6.w=(asuint(dx_log(asfloat(ref_u4(r6.w))))).w;
// L551: mul r6.w, r6.w, cb2[26].z
r6.w=(asuint(asfloat(ref_u4(r6.w))*ref_f4(scope_reference_cb[26].z))).w;
// L552: exp r6.w, r6.w
r6.w=(asuint(dx_exp(asfloat(ref_u4(r6.w))))).w;
// L553: ne [precise(z)] r7.z, cb2[29].x, l(0.000000)
r7.z=(ref_mask(ref_ne(ref_f4(scope_reference_cb[29].x),ref_f4(0.000000)))).z;
// L554: if_nz r7.z
if(ref_u4(r7.z).x!=0u){
// L555: dp2 r11.x, r8.zwzz, l(127.099998, 311.700012, 0.000000, 0.000000)
r11.x=(asuint(ref_f4(dot(asfloat(ref_u4(r8.zwzz)).xy,ref_f4(127.099998,311.700012,0.000000,0.000000).xy)))).x;
// L556: dp2 r11.y, r8.zwzz, l(269.500000, 183.300003, 0.000000, 0.000000)
r11.y=(asuint(ref_f4(dot(asfloat(ref_u4(r8.zwzz)).xy,ref_f4(269.500000,183.300003,0.000000,0.000000).xy)))).y;
// L557: mul r9.zw, r11.xxxy, l(0.000000, 0.000000, 0.100000, 0.100000)
r9.zw=(asuint(asfloat(ref_u4(r11.xxxy))*ref_f4(0.000000,0.000000,0.100000,0.100000))).zw;
// L558: sincos r9.zw, null, r9.zzzw
{float4 angle=asfloat(ref_u4(r9.zzzw));r9.zw=(asuint(sin(angle))).zw;}
// L559: mul r9.zw, r9.zzzw, l(0.000000, 0.000000, 43758.546875, 43758.546875)
r9.zw=(asuint(asfloat(ref_u4(r9.zzzw))*ref_f4(0.000000,0.000000,43758.546875,43758.546875))).zw;
// L560: frc r9.zw, r9.zzzw
r9.zw=(asuint(frac(asfloat(ref_u4(r9.zzzw))))).zw;
// L561: mad r9.zw, r9.zzzw, l(0.000000, 0.000000, 2.000000, 2.000000), l(0.000000, 0.000000, -1.000000, -1.000000)
r9.zw=(asuint(asfloat(ref_u4(r9.zzzw))*ref_f4(0.000000,0.000000,2.000000,2.000000)+ref_f4(0.000000,0.000000,-1.000000,-1.000000))).zw;
// L562: round_z r11.xyz, cb2[30].xyzx
r11.xyz=(asuint(trunc(ref_f4(scope_reference_cb[30].xyzx)))).xyz;
// L563: sample_l_indexable(texture2d)(float,float,float,float) r12.x, r8.zwzz, t5.yxzw, s1, r11.x
r12.x=(asuint(ref_sample_5(sampleUv(5,asfloat(ref_u4(r8.zwzz)).xy),asfloat(ref_u4(r11.x)).x).yxzw)).x;
// L564: mov r12.yz, l(0,0,0,0)
r12.yz=(ref_u4(uint(0),uint(0),uint(0),uint(0))).yz;
// L565: mov r7.z, l(0)
r7.z=(ref_u4(uint(0))).z;
// L566: loop
for(int guard=0;guard<512;guard++){
// L567: ige r10.x, r7.z, l(9)
r10.x=(ref_mask(ref_ge(ref_i4(ref_u4(r7.z)),ref_i4(ref_u4(uint(9)))))).x;
// L568: breakc_nz r10.x
if(ref_u4(r10.x).x!=0u)break;
// L569: mad r13.xy, r9.zwzz, cb2[30].wwww, icb[r7.z + 0].xyxx
r13.xy=(asuint(asfloat(ref_u4(r9.zwzz))*ref_f4(scope_reference_cb[30].wwww)+asfloat(asuint(ref_f4(icb[clamp(int(r7.z),0,8)].xyxx))))).xy;
// L570: mul r13.zw, r13.xxxy, cb2[32].wwww
r13.zw=(asuint(asfloat(ref_u4(r13.xxxy))*ref_f4(scope_reference_cb[32].wwww))).zw;
// L571: mad r13.xy, r13.xyxx, cb2[32].wwww, r8.zwzz
r13.xy=(asuint(asfloat(ref_u4(r13.xyxx))*ref_f4(scope_reference_cb[32].wwww)+asfloat(ref_u4(r8.zwzz)))).xy;
// L572: mad r13.zw, r13.zzzw, l(0.000000, 0.000000, 2.000000, 2.000000), r8.zzzw
r13.zw=(asuint(asfloat(ref_u4(r13.zzzw))*ref_f4(0.000000,0.000000,2.000000,2.000000)+asfloat(ref_u4(r8.zzzw)))).zw;
// L573: sample_l_indexable(texture2d)(float,float,float,float) r10.x, r13.xyxx, t5.yxzw, s1, r11.y
r10.x=(asuint(ref_sample_5(sampleUv(5,asfloat(ref_u4(r13.xyxx)).xy),asfloat(ref_u4(r11.y)).x).yxzw)).x;
// L574: sample_l_indexable(texture2d)(float,float,float,float) r11.w, r13.zwzz, t5.xzwy, s1, r11.z
r11.w=(asuint(ref_sample_5(sampleUv(5,asfloat(ref_u4(r13.zwzz)).xy),asfloat(ref_u4(r11.z)).x).xzwy)).w;
// L575: mad r12.y, r10.x, icb[r7.z + 0].z, r12.y
r12.y=(asuint(asfloat(ref_u4(r10.x))*asfloat(asuint(ref_f4(icb[clamp(int(r7.z),0,8)].z)))+asfloat(ref_u4(r12.y)))).y;
// L576: mad r12.z, r11.w, icb[r7.z + 0].z, r12.z
r12.z=(asuint(asfloat(ref_u4(r11.w))*asfloat(asuint(ref_f4(icb[clamp(int(r7.z),0,8)].z)))+asfloat(ref_u4(r12.z)))).z;
// L577: iadd r7.z, r7.z, l(1)
r7.z=(ref_u4(ref_i4(ref_u4(r7.z))+ref_i4(ref_u4(uint(1))))).z;
// L578: endloop
}
// L579: if_nz r0.z
if(ref_u4(r0.z).x!=0u){
// L580: ftoi r0.z, cb2[32].x
r0.z=(ref_u4(ref_i4(trunc(ref_f4(scope_reference_cb[32].x))))).z;
// L581: imax r0.z, r0.z, l(1)
r0.z=(ref_u4(max(ref_i4(ref_u4(r0.z)),ref_i4(ref_u4(uint(1)))))).z;
// L582: imin r0.z, r0.z, l(64)
r0.z=(ref_u4(min(ref_i4(ref_u4(r0.z)),ref_i4(ref_u4(uint(64)))))).z;
// L583: mov r7.z, l(0)
r7.z=(ref_u4(uint(0))).z;
// L584: mov r9.z, l(0)
r9.z=(ref_u4(uint(0))).z;
// L585: loop
for(int guard=0;guard<512;guard++){
// L586: ige r9.w, r9.z, r0.z
r9.w=(ref_mask(ref_ge(ref_i4(ref_u4(r9.z)),ref_i4(ref_u4(r0.z))))).w;
// L587: breakc_nz r9.w
if(ref_u4(r9.w).x!=0u)break;
// L588: itof r9.w, r9.z
r9.w=(asuint(ref_f4(ref_i4(ref_u4(r9.z))))).w;
// L589: mul r11.zw, r9.wwww, l(0.000000, 0.000000, 0.062500, 2.399963)
r11.zw=(asuint(asfloat(ref_u4(r9.wwww))*ref_f4(0.000000,0.000000,0.062500,2.399963))).zw;
// L590: sincos r10.x, r13.x, r11.w
{float4 angle=asfloat(ref_u4(r11.w));r10.x=(asuint(sin(angle))).x;r13.x=(asuint(cos(angle))).x;}
// L591: sqrt r9.w, r11.z
r9.w=(asuint(sqrt(asfloat(ref_u4(r11.z))))).w;
// L592: mov r13.y, r10.x
r13.y=(ref_u4(r10.x)).y;
// L593: mul r11.zw, r9.wwww, r13.xxxy
r11.zw=(asuint(asfloat(ref_u4(r9.wwww))*asfloat(ref_u4(r13.xxxy)))).zw;
// L594: mad r11.zw, r11.zzzw, cb2[32].yyyy, r8.zzzw
r11.zw=(asuint(asfloat(ref_u4(r11.zzzw))*ref_f4(scope_reference_cb[32].yyyy)+asfloat(ref_u4(r8.zzzw)))).zw;
// L595: sample_l_indexable(texture2d)(float,float,float,float) r9.w, r11.zwzz, t5.xzwy, s1, r11.x
r9.w=(asuint(ref_sample_5(sampleUv(5,asfloat(ref_u4(r11.zwzz)).xy),asfloat(ref_u4(r11.x)).x).xzwy)).w;
// L596: add r7.z, r7.z, r9.w
r7.z=(asuint(asfloat(ref_u4(r7.z))+asfloat(ref_u4(r9.w)))).z;
// L597: iadd r9.z, r9.z, l(1)
r9.z=(ref_u4(ref_i4(ref_u4(r9.z))+ref_i4(ref_u4(uint(1))))).z;
// L598: endloop
}
// L599: mul [precise(z)] r0.z, r5.w, cb2[32].z
r0.z=(asuint(asfloat(ref_u4(r5.w))*ref_f4(scope_reference_cb[32].z))).z;
// L600: ftoi [precise(z)] r0.z, r0.z
r0.z=(ref_u4(ref_i4(trunc(asfloat(ref_u4(r0.z)))))).z;
// L601: imax [precise(z)] r0.z, r0.z, l(2)
r0.z=(ref_u4(max(ref_i4(ref_u4(r0.z)),ref_i4(ref_u4(uint(2)))))).z;
// L602: ushr [precise(z)] r0.z, r0.z, l(1)
r0.z=(ref_u4(r0.z)>>ref_u4(uint(1))).z;
// L603: ineg [precise(z)] r9.z, r0.z
r9.z=(ref_u4(-ref_i4(ref_u4(r0.z)))).z;
// L604: mov r9.w, l(0)
r9.w=(ref_u4(uint(0))).w;
// L605: mov [precise(x)] r10.x, r9.z
r10.x=(ref_u4(r9.z)).x;
// L606: loop
for(int guard=0;guard<512;guard++){
// L607: ige [precise(x)] r11.x, r10.x, r0.z
r11.x=(ref_mask(ref_ge(ref_i4(ref_u4(r10.x)),ref_i4(ref_u4(r0.z))))).x;
// L608: breakc_nz r11.x
if(ref_u4(r11.x).x!=0u)break;
// L609: itof r11.x, r10.x
r11.x=(asuint(ref_f4(ref_i4(ref_u4(r10.x))))).x;
// L610: mul r11.x, r11.x, cb2[32].w
r11.x=(asuint(asfloat(ref_u4(r11.x))*ref_f4(scope_reference_cb[32].w))).x;
// L611: mad r11.xz, r11.xxxx, r8.xxyx, r8.zzwz
r11.xz=(asuint(asfloat(ref_u4(r11.xxxx))*asfloat(ref_u4(r8.xxyx))+asfloat(ref_u4(r8.zzwz)))).xz;
// L612: sample_l_indexable(texture2d)(float,float,float,float) r11.x, r11.xzxx, t5.yxzw, s1, r11.y
r11.x=(asuint(ref_sample_5(sampleUv(5,asfloat(ref_u4(r11.xzxx)).xy),asfloat(ref_u4(r11.y)).x).yxzw)).x;
// L613: movc [precise(z)] r11.z, r10.x, r10.x, l(1)
{uint4 c=ref_u4(r10.x),yes=ref_u4(r10.x),no=ref_u4(uint(1));r11.z=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).z;}
// L614: imax r11.z, -r11.z, r11.z
r11.z=(ref_u4(max((-ref_i4(ref_u4(r11.z))),ref_i4(ref_u4(r11.z))))).z;
// L615: itof r11.z, r11.z
r11.z=(asuint(ref_f4(ref_i4(ref_u4(r11.z))))).z;
// L616: log r11.z, r11.z
r11.z=(asuint(dx_log(asfloat(ref_u4(r11.z))))).z;
// L617: mul r11.z, r11.z, cb2[33].x
r11.z=(asuint(asfloat(ref_u4(r11.z))*ref_f4(scope_reference_cb[33].x))).z;
// L618: exp r11.z, r11.z
r11.z=(asuint(dx_exp(asfloat(ref_u4(r11.z))))).z;
// L619: div r11.z, l(1.000000, 1.000000, 1.000000, 1.000000), r11.z
r11.z=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r11.z)))).z;
// L620: mad r9.w, r11.x, r11.z, r9.w
r9.w=(asuint(asfloat(ref_u4(r11.x))*asfloat(ref_u4(r11.z))+asfloat(ref_u4(r9.w)))).w;
// L621: iadd [precise(x)] r10.x, r10.x, l(1)
r10.x=(ref_u4(ref_i4(ref_u4(r10.x))+ref_i4(ref_u4(uint(1))))).x;
// L622: endloop
}
// L623: log r0.z, r5.w
r0.z=(asuint(dx_log(asfloat(ref_u4(r5.w))))).z;
// L624: mul r0.z, r0.z, cb2[33].y
r0.z=(asuint(asfloat(ref_u4(r0.z))*ref_f4(scope_reference_cb[33].y))).z;
// L625: exp r0.z, r0.z
r0.z=(asuint(dx_exp(asfloat(ref_u4(r0.z))))).z;
// L626: min r0.z, r0.z, l(1.000000)
r0.z=(asuint(dx_min(asfloat(ref_u4(r0.z)),ref_f4(1.000000)))).z;
// L627: mad r12.x, r0.z, -r12.x, r12.x
r12.x=(asuint(asfloat(ref_u4(r0.z))*(-asfloat(ref_u4(r12.x)))+asfloat(ref_u4(r12.x)))).x;
// L628: add r5.w, r7.z, r9.w
r5.w=(asuint(asfloat(ref_u4(r7.z))+asfloat(ref_u4(r9.w)))).w;
// L629: add r5.w, -r12.y, r5.w
r5.w=(asuint((-asfloat(ref_u4(r12.y)))+asfloat(ref_u4(r5.w)))).w;
// L630: mad r12.y, r0.z, r5.w, r12.y
r12.y=(asuint(asfloat(ref_u4(r0.z))*asfloat(ref_u4(r5.w))+asfloat(ref_u4(r12.y)))).y;
// L631: mad r12.z, r0.z, -r12.z, r12.z
r12.z=(asuint(asfloat(ref_u4(r0.z))*(-asfloat(ref_u4(r12.z)))+asfloat(ref_u4(r12.z)))).z;
// L632: endif
}
// L633: mad r0.z, r2.w, r4.w, l(1.000000)
r0.z=(asuint(asfloat(ref_u4(r2.w))*asfloat(ref_u4(r4.w))+ref_f4(1.000000))).z;
// L634: mul r8.xyz, r12.xyzx, cb2[29].xxxx
r8.xyz=(asuint(asfloat(ref_u4(r12.xyzx))*ref_f4(scope_reference_cb[29].xxxx))).xyz;
// L635: mul r8.xyz, r8.xyzx, cb2[29].yzwy
r8.xyz=(asuint(asfloat(ref_u4(r8.xyzx))*ref_f4(scope_reference_cb[29].yzwy))).xyz;
// L636: mul r8.xyz, r0.zzzz, r8.xyzx
r8.xyz=(asuint(asfloat(ref_u4(r0.zzzz))*asfloat(ref_u4(r8.xyzx)))).xyz;
// L637: add r0.z, r8.y, r8.x
r0.z=(asuint(asfloat(ref_u4(r8.y))+asfloat(ref_u4(r8.x)))).z;
// L638: mad r0.z, r8.z, r8.z, r0.z
r0.z=(asuint(asfloat(ref_u4(r8.z))*asfloat(ref_u4(r8.z))+asfloat(ref_u4(r0.z)))).z;
// L639: else
}else{
// L640: mov r0.z, l(0)
r0.z=(ref_u4(uint(0))).z;
// L641: endif
}
// L642: mad r2.w, cb2[33].z, l(-0.050000), l(0.025000)
r2.w=(asuint(ref_f4(scope_reference_cb[33].z)*ref_f4(-0.050000)+ref_f4(0.025000))).w;
// L643: mad r8.xy, r9.xyxx, r2.wwww, v3.xyxx
r8.xy=(asuint(asfloat(ref_u4(r9.xyxx))*asfloat(ref_u4(r2.wwww))+ref_f4(v3.xyxx))).xy;
// L644: mad r8.xy, r8.xyxx, cb2[33].wwww, l(0.500000, 0.500000, 0.000000, 0.000000)
r8.xy=(asuint(asfloat(ref_u4(r8.xyxx))*ref_f4(scope_reference_cb[33].wwww)+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L645: mad r2.w, cb2[34].y, l(-0.050000), l(0.025000)
r2.w=(asuint(ref_f4(scope_reference_cb[34].y)*ref_f4(-0.050000)+ref_f4(0.025000))).w;
// L646: mad r8.zw, r9.xxxy, r2.wwww, v3.xxxy
r8.zw=(asuint(asfloat(ref_u4(r9.xxxy))*asfloat(ref_u4(r2.wwww))+ref_f4(v3.xxxy))).zw;
// L647: mad r8.zw, r8.zzzw, cb2[34].zzzz, l(0.000000, 0.000000, 0.500000, 0.500000)
r8.zw=(asuint(asfloat(ref_u4(r8.zzzw))*ref_f4(scope_reference_cb[34].zzzz)+ref_f4(0.000000,0.000000,0.500000,0.500000))).zw;
// L648: add r8.xyzw, r8.xyzw, -cb2[34].xxww
r8.xyzw=(asuint(asfloat(ref_u4(r8.xyzw))+(-ref_f4(scope_reference_cb[34].xxww)))).xyzw;
// L649: mad r9.zw, cb2[35].xxxw, l(0.000000, 0.000000, -0.050000, -0.050000), l(0.000000, 0.000000, 0.025000, 0.025000)
r9.zw=(asuint(ref_f4(scope_reference_cb[35].xxxw)*ref_f4(0.000000,0.000000,-0.050000,-0.050000)+ref_f4(0.000000,0.000000,0.025000,0.025000))).zw;
// L650: mad r9.xyzw, r9.xyxy, r9.zzww, v3.xyxy
r9.xyzw=(asuint(asfloat(ref_u4(r9.xyxy))*asfloat(ref_u4(r9.zzww))+ref_f4(v3.xyxy))).xyzw;
// L651: mad r9.xy, r9.xyxx, cb2[35].yyyy, l(0.500000, 0.500000, 0.000000, 0.000000)
r9.xy=(asuint(asfloat(ref_u4(r9.xyxx))*ref_f4(scope_reference_cb[35].yyyy)+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L652: add r9.xy, r9.xyxx, -cb2[35].zzzz
r9.xy=(asuint(asfloat(ref_u4(r9.xyxx))+(-ref_f4(scope_reference_cb[35].zzzz)))).xy;
// L653: mad r9.zw, r9.zzzw, cb2[36].xxxx, l(0.000000, 0.000000, 0.500000, 0.500000)
r9.zw=(asuint(asfloat(ref_u4(r9.zzzw))*ref_f4(scope_reference_cb[36].xxxx)+ref_f4(0.000000,0.000000,0.500000,0.500000))).zw;
// L654: add r9.zw, r9.zzzw, -cb2[36].yyyy
r9.zw=(asuint(asfloat(ref_u4(r9.zzzw))+(-ref_f4(scope_reference_cb[36].yyyy)))).zw;
// L655: add r7.xy, r7.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r7.xy=(asuint(asfloat(ref_u4(r7.xyxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L656: dp2 r2.w, r7.xyxx, r7.xyxx
r2.w=(asuint(ref_f4(dot(asfloat(ref_u4(r7.xyxx)).xy,asfloat(ref_u4(r7.xyxx)).xy)))).w;
// L657: sqrt r2.w, r2.w
r2.w=(asuint(sqrt(asfloat(ref_u4(r2.w))))).w;
// L658: mad r7.xy, -r2.wwww, l(10.000000, 2.500000, 0.000000, 0.000000), l(1.000000, 1.000000, 0.000000, 0.000000)
r7.xy=(asuint((-asfloat(ref_u4(r2.wwww)))*ref_f4(10.000000,2.500000,0.000000,0.000000)+ref_f4(1.000000,1.000000,0.000000,0.000000))).xy;
// L659: mul r11.xy, r7.xyxx, l(1.500000, 1.500000, 0.000000, 0.000000)
r11.xy=(asuint(asfloat(ref_u4(r7.xyxx))*ref_f4(1.500000,1.500000,0.000000,0.000000))).xy;
// L660: mul r11.xy, r11.xyxx, r11.xyxx
r11.xy=(asuint(asfloat(ref_u4(r11.xyxx))*asfloat(ref_u4(r11.xyxx)))).xy;
// L661: mul r11.xy, r11.xyxx, l(1.442695, 1.442695, 0.000000, 0.000000)
r11.xy=(asuint(asfloat(ref_u4(r11.xyxx))*ref_f4(1.442695,1.442695,0.000000,0.000000))).xy;
// L662: exp r11.xy, r11.xyxx
r11.xy=(asuint(dx_exp(asfloat(ref_u4(r11.xyxx))))).xy;
// L663: div r11.xy, l(1.000000, 1.000000, 1.000000, 1.000000), r11.xyxx
r11.xy=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r11.xyxx)))).xy;
// L664: lt r11.zw, l(0.000000, 0.000000, 0.000010, 0.000010), |r7.xxxy|
r11.zw=(ref_mask(ref_lt(ref_f4(0.000000,0.000000,0.000010,0.000010),abs(asfloat(ref_u4(r7.xxxy)))))).zw;
// L665: ge r7.xy, r7.xyxx, l(0.000000, 0.000000, 0.000000, 0.000000)
r7.xy=(ref_mask(ref_ge(asfloat(ref_u4(r7.xyxx)),ref_f4(0.000000,0.000000,0.000000,0.000000)))).xy;
// L666: add r11.xy, -r11.xyxx, l(1.000000, 1.000000, 0.000000, 0.000000)
r11.xy=(asuint((-asfloat(ref_u4(r11.xyxx)))+ref_f4(1.000000,1.000000,0.000000,0.000000))).xy;
// L667: add r11.xy, -r11.xyxx, l(1.000000, 1.000000, 0.000000, 0.000000)
r11.xy=(asuint((-asfloat(ref_u4(r11.xyxx)))+ref_f4(1.000000,1.000000,0.000000,0.000000))).xy;
// L668: movc r7.xy, r7.xyxx, r11.xyxx, l(1.000000,1.000000,0,0)
{uint4 c=ref_u4(r7.xyxx),yes=ref_u4(r11.xyxx),no=ref_u4(asuint(1.000000),asuint(1.000000),uint(0),uint(0));r7.xy=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).xy;}
// L669: movc r7.xy, r11.zwzz, r7.xyxx, l(1.000000,1.000000,0,0)
{uint4 c=ref_u4(r11.zwzz),yes=ref_u4(r7.xyxx),no=ref_u4(asuint(1.000000),asuint(1.000000),uint(0),uint(0));r7.xy=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).xy;}
// L670: mul r2.w, r7.y, r7.x
r2.w=(asuint(asfloat(ref_u4(r7.y))*asfloat(ref_u4(r7.x)))).w;
// L671: add r4.w, -cb2[47].y, cb2[47].z
r4.w=(asuint((-ref_f4(scope_reference_cb[47].y))+ref_f4(scope_reference_cb[47].z))).w;
// L672: add r5.w, r3.w, -cb2[47].y
r5.w=(asuint(asfloat(ref_u4(r3.w))+(-ref_f4(scope_reference_cb[47].y)))).w;
// L673: div r4.w, l(1.000000, 1.000000, 1.000000, 1.000000), r4.w
r4.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r4.w)))).w;
// L674: mul_sat r4.w, r4.w, r5.w
r4.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r5.w)),ref_f4(0.)),ref_f4(1.)))).w;
// L675: mad r5.w, r4.w, l(-2.000000), l(3.000000)
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-2.000000)+ref_f4(3.000000))).w;
// L676: mul r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L677: mul r4.w, r4.w, r5.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r5.w)))).w;
// L678: log r4.w, r4.w
r4.w=(asuint(dx_log(asfloat(ref_u4(r4.w))))).w;
// L679: mul r4.w, r4.w, cb2[47].w
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(scope_reference_cb[47].w))).w;
// L680: exp r4.w, r4.w
r4.w=(asuint(dx_exp(asfloat(ref_u4(r4.w))))).w;
// L681: add r5.w, -cb2[43].y, cb2[43].z
r5.w=(asuint((-ref_f4(scope_reference_cb[43].y))+ref_f4(scope_reference_cb[43].z))).w;
// L682: add r7.x, r3.w, -cb2[43].y
r7.x=(asuint(asfloat(ref_u4(r3.w))+(-ref_f4(scope_reference_cb[43].y)))).x;
// L683: div r5.w, l(1.000000, 1.000000, 1.000000, 1.000000), r5.w
r5.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r5.w)))).w;
// L684: mul_sat r5.w, r5.w, r7.x
r5.w=(asuint(dx_min(dx_max(asfloat(ref_u4(r5.w))*asfloat(ref_u4(r7.x)),ref_f4(0.)),ref_f4(1.)))).w;
// L685: mad r7.x, r5.w, l(-2.000000), l(3.000000)
r7.x=(asuint(asfloat(ref_u4(r5.w))*ref_f4(-2.000000)+ref_f4(3.000000))).x;
// L686: mul r5.w, r5.w, r5.w
r5.w=(asuint(asfloat(ref_u4(r5.w))*asfloat(ref_u4(r5.w)))).w;
// L687: mul r5.w, r5.w, r7.x
r5.w=(asuint(asfloat(ref_u4(r5.w))*asfloat(ref_u4(r7.x)))).w;
// L688: log r5.w, r5.w
r5.w=(asuint(dx_log(asfloat(ref_u4(r5.w))))).w;
// L689: mul r5.w, r5.w, cb2[43].w
r5.w=(asuint(asfloat(ref_u4(r5.w))*ref_f4(scope_reference_cb[43].w))).w;
// L690: exp r5.w, r5.w
r5.w=(asuint(dx_exp(asfloat(ref_u4(r5.w))))).w;
// L691: add r7.x, -cb2[44].y, cb2[44].z
r7.x=(asuint((-ref_f4(scope_reference_cb[44].y))+ref_f4(scope_reference_cb[44].z))).x;
// L692: add r7.y, r3.w, -cb2[44].y
r7.y=(asuint(asfloat(ref_u4(r3.w))+(-ref_f4(scope_reference_cb[44].y)))).y;
// L693: div r7.x, l(1.000000, 1.000000, 1.000000, 1.000000), r7.x
r7.x=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r7.x)))).x;
// L694: mul_sat r7.x, r7.x, r7.y
r7.x=(asuint(dx_min(dx_max(asfloat(ref_u4(r7.x))*asfloat(ref_u4(r7.y)),ref_f4(0.)),ref_f4(1.)))).x;
// L695: mad r7.y, r7.x, l(-2.000000), l(3.000000)
r7.y=(asuint(asfloat(ref_u4(r7.x))*ref_f4(-2.000000)+ref_f4(3.000000))).y;
// L696: mul r7.x, r7.x, r7.x
r7.x=(asuint(asfloat(ref_u4(r7.x))*asfloat(ref_u4(r7.x)))).x;
// L697: mul r7.x, r7.x, r7.y
r7.x=(asuint(asfloat(ref_u4(r7.x))*asfloat(ref_u4(r7.y)))).x;
// L698: log r7.x, r7.x
r7.x=(asuint(dx_log(asfloat(ref_u4(r7.x))))).x;
// L699: mul r7.x, r7.x, cb2[44].w
r7.x=(asuint(asfloat(ref_u4(r7.x))*ref_f4(scope_reference_cb[44].w))).x;
// L700: exp r7.x, r7.x
r7.x=(asuint(dx_exp(asfloat(ref_u4(r7.x))))).x;
// L701: add r7.y, -cb2[45].y, cb2[45].z
r7.y=(asuint((-ref_f4(scope_reference_cb[45].y))+ref_f4(scope_reference_cb[45].z))).y;
// L702: add r7.z, r3.w, -cb2[45].y
r7.z=(asuint(asfloat(ref_u4(r3.w))+(-ref_f4(scope_reference_cb[45].y)))).z;
// L703: div r7.y, l(1.000000, 1.000000, 1.000000, 1.000000), r7.y
r7.y=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r7.y)))).y;
// L704: mul_sat r7.y, r7.y, r7.z
r7.y=(asuint(dx_min(dx_max(asfloat(ref_u4(r7.y))*asfloat(ref_u4(r7.z)),ref_f4(0.)),ref_f4(1.)))).y;
// L705: mad r7.z, r7.y, l(-2.000000), l(3.000000)
r7.z=(asuint(asfloat(ref_u4(r7.y))*ref_f4(-2.000000)+ref_f4(3.000000))).z;
// L706: mul r7.y, r7.y, r7.y
r7.y=(asuint(asfloat(ref_u4(r7.y))*asfloat(ref_u4(r7.y)))).y;
// L707: mul r7.y, r7.y, r7.z
r7.y=(asuint(asfloat(ref_u4(r7.y))*asfloat(ref_u4(r7.z)))).y;
// L708: log r7.y, r7.y
r7.y=(asuint(dx_log(asfloat(ref_u4(r7.y))))).y;
// L709: mul r7.y, r7.y, cb2[45].w
r7.y=(asuint(asfloat(ref_u4(r7.y))*ref_f4(scope_reference_cb[45].w))).y;
// L710: exp r7.y, r7.y
r7.y=(asuint(dx_exp(asfloat(ref_u4(r7.y))))).y;
// L711: add r7.z, -cb2[46].y, cb2[46].z
r7.z=(asuint((-ref_f4(scope_reference_cb[46].y))+ref_f4(scope_reference_cb[46].z))).z;
// L712: add r10.x, r3.w, -cb2[46].y
r10.x=(asuint(asfloat(ref_u4(r3.w))+(-ref_f4(scope_reference_cb[46].y)))).x;
// L713: div r7.z, l(1.000000, 1.000000, 1.000000, 1.000000), r7.z
r7.z=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r7.z)))).z;
// L714: mul_sat r7.z, r7.z, r10.x
r7.z=(asuint(dx_min(dx_max(asfloat(ref_u4(r7.z))*asfloat(ref_u4(r10.x)),ref_f4(0.)),ref_f4(1.)))).z;
// L715: mad r10.x, r7.z, l(-2.000000), l(3.000000)
r10.x=(asuint(asfloat(ref_u4(r7.z))*ref_f4(-2.000000)+ref_f4(3.000000))).x;
// L716: mul r7.z, r7.z, r7.z
r7.z=(asuint(asfloat(ref_u4(r7.z))*asfloat(ref_u4(r7.z)))).z;
// L717: mul r7.z, r7.z, r10.x
r7.z=(asuint(asfloat(ref_u4(r7.z))*asfloat(ref_u4(r10.x)))).z;
// L718: log r7.z, r7.z
r7.z=(asuint(dx_log(asfloat(ref_u4(r7.z))))).z;
// L719: mul r7.z, r7.z, cb2[46].w
r7.z=(asuint(asfloat(ref_u4(r7.z))*ref_f4(scope_reference_cb[46].w))).z;
// L720: exp r7.z, r7.z
r7.z=(asuint(dx_exp(asfloat(ref_u4(r7.z))))).z;
// L721: add r4.w, r4.w, -r7.z
r4.w=(asuint(asfloat(ref_u4(r4.w))+(-asfloat(ref_u4(r7.z))))).w;
// L722: mad r4.w, cb2[47].x, r4.w, r7.z
r4.w=(asuint(ref_f4(scope_reference_cb[47].x)*asfloat(ref_u4(r4.w))+asfloat(ref_u4(r7.z)))).w;
// L723: mul r11.xy, r0.xyxx, cb2[43].xxxx
r11.xy=(asuint(asfloat(ref_u4(r0.xyxx))*ref_f4(scope_reference_cb[43].xxxx))).xy;
// L724: mad r11.xy, -r11.xyxx, r5.wwww, r8.xyxx
r11.xy=(asuint((-asfloat(ref_u4(r11.xyxx)))*asfloat(ref_u4(r5.wwww))+asfloat(ref_u4(r8.xyxx)))).xy;
// L725: mul r11.zw, r0.xxxy, cb2[44].xxxx
r11.zw=(asuint(asfloat(ref_u4(r0.xxxy))*ref_f4(scope_reference_cb[44].xxxx))).zw;
// L726: mad r7.xz, -r11.zzwz, r7.xxxx, r8.zzwz
r7.xz=(asuint((-asfloat(ref_u4(r11.zzwz)))*asfloat(ref_u4(r7.xxxx))+asfloat(ref_u4(r8.zzwz)))).xz;
// L727: mul r11.zw, r0.xxxy, cb2[45].xxxx
r11.zw=(asuint(asfloat(ref_u4(r0.xxxy))*ref_f4(scope_reference_cb[45].xxxx))).zw;
// L728: mad r11.zw, -r11.zzzw, r7.yyyy, r9.xxxy
r11.zw=(asuint((-asfloat(ref_u4(r11.zzzw)))*asfloat(ref_u4(r7.yyyy))+asfloat(ref_u4(r9.xxxy)))).zw;
// L729: mul r0.xy, r0.xyxx, cb2[46].xxxx
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))*ref_f4(scope_reference_cb[46].xxxx))).xy;
// L730: mad r0.xy, -r0.xyxx, r4.wwww, r9.zwzz
r0.xy=(asuint((-asfloat(ref_u4(r0.xyxx)))*asfloat(ref_u4(r4.wwww))+asfloat(ref_u4(r9.zwzz)))).xy;
// L731: add r12.xy, r11.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r12.xy=(asuint(asfloat(ref_u4(r11.xyxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L732: add r13.xyz, cb2[38].xzwx, l(1.000000, 1.000000, 1.000000, 0.000000)
r13.xyz=(asuint(ref_f4(scope_reference_cb[38].xzwx)+ref_f4(1.000000,1.000000,1.000000,0.000000))).xyz;
// L733: mad r12.xy, r12.xyxx, r13.xxxx, l(0.500000, 0.500000, 0.000000, 0.000000)
r12.xy=(asuint(asfloat(ref_u4(r12.xyxx))*asfloat(ref_u4(r13.xxxx))+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L734: sample_l_indexable(texture2d)(float,float,float,float) r12.x, r12.xyxx, t4.xyzw, s1, l(0.000000)
r12.x=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r12.xyxx)).xy),ref_f4(0.000000).x).xyzw)).x;
// L735: sample_l_indexable(texture2d)(float,float,float,float) r12.yz, r11.xyxx, t4.xyzw, s1, l(0.000000)
r12.yz=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r11.xyxx)).xy),ref_f4(0.000000).x).xyzw)).yz;
// L736: add r8.xy, r8.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r8.xy=(asuint(asfloat(ref_u4(r8.xyxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L737: dp2 r4.w, r8.xyxx, r8.xyxx
r4.w=(asuint(ref_f4(dot(asfloat(ref_u4(r8.xyxx)).xy,asfloat(ref_u4(r8.xyxx)).xy)))).w;
// L738: sqrt r4.w, r4.w
r4.w=(asuint(sqrt(asfloat(ref_u4(r4.w))))).w;
// L739: add r4.w, r4.w, l(-0.700000)
r4.w=(asuint(asfloat(ref_u4(r4.w))+ref_f4(-0.700000))).w;
// L740: mul r4.w, r4.w, l(-1.428571)
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-1.428571))).w;
// L741: max r4.w, r4.w, l(0.000000)
r4.w=(asuint(dx_max(asfloat(ref_u4(r4.w)),ref_f4(0.000000)))).w;
// L742: mad r5.w, r4.w, l(-2.000000), l(3.000000)
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-2.000000)+ref_f4(3.000000))).w;
// L743: mul r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L744: mul r4.w, r4.w, r5.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r5.w)))).w;
// L745: mul r14.xyzw, cb2[36].zzzz, cb2[37].xyzw
r14.xyzw=(asuint(ref_f4(scope_reference_cb[36].zzzz)*ref_f4(scope_reference_cb[37].xyzw))).xyzw;
// L746: mul r15.xyz, r14.xxxx, cb2[39].xyzx
r15.xyz=(asuint(asfloat(ref_u4(r14.xxxx))*ref_f4(scope_reference_cb[39].xyzx))).xyz;
// L747: mul r15.xyz, r4.wwww, r15.xyzx
r15.xyz=(asuint(asfloat(ref_u4(r4.wwww))*asfloat(ref_u4(r15.xyzx)))).xyz;
// L748: add r8.xy, r7.xzxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r8.xy=(asuint(asfloat(ref_u4(r7.xzxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L749: mad r4.w, cb2[38].y, l(-1.000000), l(1.000000)
r4.w=(asuint(ref_f4(scope_reference_cb[38].y)*ref_f4(-1.000000)+ref_f4(1.000000))).w;
// L750: mad r8.xy, r8.xyxx, r4.wwww, l(0.500000, 0.500000, 0.000000, 0.000000)
r8.xy=(asuint(asfloat(ref_u4(r8.xyxx))*asfloat(ref_u4(r4.wwww))+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L751: sample_l_indexable(texture2d)(float,float,float,float) r7.xy, r7.xzxx, t4.xyzw, s1, l(0.000000)
r7.xy=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r7.xzxx)).xy),ref_f4(0.000000).x).xyzw)).xy;
// L752: sample_l_indexable(texture2d)(float,float,float,float) r7.z, r8.xyxx, t4.xyzw, s1, l(0.000000)
r7.z=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r8.xyxx)).xy),ref_f4(0.000000).x).xyzw)).z;
// L753: add r8.xy, r8.zwzz, l(-0.500000, -0.500000, 0.000000, 0.000000)
r8.xy=(asuint(asfloat(ref_u4(r8.zwzz))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L754: dp2 r4.w, r8.xyxx, r8.xyxx
r4.w=(asuint(ref_f4(dot(asfloat(ref_u4(r8.xyxx)).xy,asfloat(ref_u4(r8.xyxx)).xy)))).w;
// L755: sqrt r4.w, r4.w
r4.w=(asuint(sqrt(asfloat(ref_u4(r4.w))))).w;
// L756: add r4.w, r4.w, l(-0.700000)
r4.w=(asuint(asfloat(ref_u4(r4.w))+ref_f4(-0.700000))).w;
// L757: mul r4.w, r4.w, l(-1.428571)
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-1.428571))).w;
// L758: max r4.w, r4.w, l(0.000000)
r4.w=(asuint(dx_max(asfloat(ref_u4(r4.w)),ref_f4(0.000000)))).w;
// L759: mad r5.w, r4.w, l(-2.000000), l(3.000000)
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-2.000000)+ref_f4(3.000000))).w;
// L760: mul r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L761: mul r4.w, r4.w, r5.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r5.w)))).w;
// L762: mul r8.xyz, r14.yyyy, cb2[40].xyzx
r8.xyz=(asuint(asfloat(ref_u4(r14.yyyy))*ref_f4(scope_reference_cb[40].xyzx))).xyz;
// L763: mul r8.xyz, r4.wwww, r8.xyzx
r8.xyz=(asuint(asfloat(ref_u4(r4.wwww))*asfloat(ref_u4(r8.xyzx)))).xyz;
// L764: mul r7.xyz, r8.xyzx, r7.xyzx
r7.xyz=(asuint(asfloat(ref_u4(r8.xyzx))*asfloat(ref_u4(r7.xyzx)))).xyz;
// L765: add r8.xy, r11.zwzz, l(-0.500000, -0.500000, 0.000000, 0.000000)
r8.xy=(asuint(asfloat(ref_u4(r11.zwzz))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L766: mad r8.xy, r8.xyxx, r13.yyyy, l(0.500000, 0.500000, 0.000000, 0.000000)
r8.xy=(asuint(asfloat(ref_u4(r8.xyxx))*asfloat(ref_u4(r13.yyyy))+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L767: sample_l_indexable(texture2d)(float,float,float,float) r11.xz, r11.zwzz, t4.xyzw, s1, l(0.000000)
r11.xz=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r11.zwzz)).xy),ref_f4(0.000000).x).xyzw)).xz;
// L768: sample_l_indexable(texture2d)(float,float,float,float) r11.y, r8.xyxx, t4.xyzw, s1, l(0.000000)
r11.y=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r8.xyxx)).xy),ref_f4(0.000000).x).xyzw)).y;
// L769: add r8.xy, r9.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r8.xy=(asuint(asfloat(ref_u4(r9.xyxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L770: dp2 r4.w, r8.xyxx, r8.xyxx
r4.w=(asuint(ref_f4(dot(asfloat(ref_u4(r8.xyxx)).xy,asfloat(ref_u4(r8.xyxx)).xy)))).w;
// L771: sqrt r4.w, r4.w
r4.w=(asuint(sqrt(asfloat(ref_u4(r4.w))))).w;
// L772: add r4.w, r4.w, l(-0.700000)
r4.w=(asuint(asfloat(ref_u4(r4.w))+ref_f4(-0.700000))).w;
// L773: mul r4.w, r4.w, l(-1.428571)
r4.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-1.428571))).w;
// L774: max r4.w, r4.w, l(0.000000)
r4.w=(asuint(dx_max(asfloat(ref_u4(r4.w)),ref_f4(0.000000)))).w;
// L775: mad r5.w, r4.w, l(-2.000000), l(3.000000)
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(-2.000000)+ref_f4(3.000000))).w;
// L776: mul r4.w, r4.w, r4.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r4.w)))).w;
// L777: mul r4.w, r4.w, r5.w
r4.w=(asuint(asfloat(ref_u4(r4.w))*asfloat(ref_u4(r5.w)))).w;
// L778: mul r8.xyz, r14.zzzz, cb2[41].xyzx
r8.xyz=(asuint(asfloat(ref_u4(r14.zzzz))*ref_f4(scope_reference_cb[41].xyzx))).xyz;
// L779: mul r8.xyz, r4.wwww, r8.xyzx
r8.xyz=(asuint(asfloat(ref_u4(r4.wwww))*asfloat(ref_u4(r8.xyzx)))).xyz;
// L780: add r9.xy, r0.xyxx, l(-0.500000, -0.500000, 0.000000, 0.000000)
r9.xy=(asuint(asfloat(ref_u4(r0.xyxx))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L781: mad r9.xy, r9.xyxx, r13.zzzz, l(0.500000, 0.500000, 0.000000, 0.000000)
r9.xy=(asuint(asfloat(ref_u4(r9.xyxx))*asfloat(ref_u4(r13.zzzz))+ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L782: sample_l_indexable(texture2d)(float,float,float,float) r13.xy, r0.xyxx, t4.xyzw, s1, l(0.000000)
r13.xy=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r0.xyxx)).xy),ref_f4(0.000000).x).xyzw)).xy;
// L783: sample_l_indexable(texture2d)(float,float,float,float) r13.z, r9.xyxx, t4.xyzw, s1, l(0.000000)
r13.z=(asuint(ref_sample_4(sampleUv(4,asfloat(ref_u4(r9.xyxx)).xy),ref_f4(0.000000).x).xyzw)).z;
// L784: add r0.xy, r9.zwzz, l(-0.500000, -0.500000, 0.000000, 0.000000)
r0.xy=(asuint(asfloat(ref_u4(r9.zwzz))+ref_f4(-0.500000,-0.500000,0.000000,0.000000))).xy;
// L785: dp2 r0.x, r0.xyxx, r0.xyxx
r0.x=(asuint(ref_f4(dot(asfloat(ref_u4(r0.xyxx)).xy,asfloat(ref_u4(r0.xyxx)).xy)))).x;
// L786: sqrt r0.x, r0.x
r0.x=(asuint(sqrt(asfloat(ref_u4(r0.x))))).x;
// L787: add r0.x, r0.x, l(-0.700000)
r0.x=(asuint(asfloat(ref_u4(r0.x))+ref_f4(-0.700000))).x;
// L788: mul r0.x, r0.x, l(-1.428571)
r0.x=(asuint(asfloat(ref_u4(r0.x))*ref_f4(-1.428571))).x;
// L789: max r0.x, r0.x, l(0.000000)
r0.x=(asuint(dx_max(asfloat(ref_u4(r0.x)),ref_f4(0.000000)))).x;
// L790: mad r0.y, r0.x, l(-2.000000), l(3.000000)
r0.y=(asuint(asfloat(ref_u4(r0.x))*ref_f4(-2.000000)+ref_f4(3.000000))).y;
// L791: mul r0.x, r0.x, r0.x
r0.x=(asuint(asfloat(ref_u4(r0.x))*asfloat(ref_u4(r0.x)))).x;
// L792: mul r0.x, r0.x, r0.y
r0.x=(asuint(asfloat(ref_u4(r0.x))*asfloat(ref_u4(r0.y)))).x;
// L793: mul r9.xyz, r14.wwww, cb2[42].xyzx
r9.xyz=(asuint(asfloat(ref_u4(r14.wwww))*ref_f4(scope_reference_cb[42].xyzx))).xyz;
// L794: mul r9.xyz, r0.xxxx, r9.xyzx
r9.xyz=(asuint(asfloat(ref_u4(r0.xxxx))*asfloat(ref_u4(r9.xyzx)))).xyz;
// L795: mad r7.xyz, r12.xyzx, r15.xyzx, r7.xyzx
r7.xyz=(asuint(asfloat(ref_u4(r12.xyzx))*asfloat(ref_u4(r15.xyzx))+asfloat(ref_u4(r7.xyzx)))).xyz;
// L796: mad r7.xyz, r11.xyzx, r8.xyzx, r7.xyzx
r7.xyz=(asuint(asfloat(ref_u4(r11.xyzx))*asfloat(ref_u4(r8.xyzx))+asfloat(ref_u4(r7.xyzx)))).xyz;
// L797: mad r7.xyz, r13.xyzx, r9.xyzx, r7.xyzx
r7.xyz=(asuint(asfloat(ref_u4(r13.xyzx))*asfloat(ref_u4(r9.xyzx))+asfloat(ref_u4(r7.xyzx)))).xyz;
// L798: mul r8.xyz, r2.wwww, r7.xyzx
r8.xyz=(asuint(asfloat(ref_u4(r2.wwww))*asfloat(ref_u4(r7.xyzx)))).xyz;
// L799: add r0.xy, v3.yxyy, -cb2[48].wzww
r0.xy=(asuint(ref_f4(v3.yxyy)+(-ref_f4(scope_reference_cb[48].wzww)))).xy;
// L800: max r9.xy, cb2[48].yxyy, l(0.000010, 0.000010, 0.000000, 0.000000)
r9.xy=(asuint(dx_max(ref_f4(scope_reference_cb[48].yxyy),ref_f4(0.000010,0.000010,0.000000,0.000000)))).xy;
// L801: div r0.xy, r0.xyxx, r9.xyxx
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))/asfloat(ref_u4(r9.xyxx)))).xy;
// L802: add r0.xy, r0.xyxx, cb2[48].wzww
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))+ref_f4(scope_reference_cb[48].wzww))).xy;
// L803: mul r4.w, cb2[49].z, l(0.017453)
r4.w=(asuint(ref_f4(scope_reference_cb[49].z)*ref_f4(0.017453))).w;
// L804: sincos r9.x, r10.x, r4.w
{float4 angle=asfloat(ref_u4(r4.w));r9.x=(asuint(sin(angle))).x;r10.x=(asuint(cos(angle))).x;}
// L805: add r0.xy, r0.xyxx, -cb2[50].yxyy
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))+(-ref_f4(scope_reference_cb[50].yxyy)))).xy;
// L806: mul r9.xy, r9.xxxx, r0.xyxx
r9.xy=(asuint(asfloat(ref_u4(r9.xxxx))*asfloat(ref_u4(r0.xyxx)))).xy;
// L807: mad r11.x, r0.y, r10.x, -r9.x
r11.x=(asuint(asfloat(ref_u4(r0.y))*asfloat(ref_u4(r10.x))+(-asfloat(ref_u4(r9.x))))).x;
// L808: mad r11.y, r0.x, r10.x, r9.y
r11.y=(asuint(asfloat(ref_u4(r0.x))*asfloat(ref_u4(r10.x))+asfloat(ref_u4(r9.y)))).y;
// L809: add r0.xy, r11.xyxx, cb2[49].xyxx
r0.xy=(asuint(asfloat(ref_u4(r11.xyxx))+ref_f4(scope_reference_cb[49].xyxx))).xy;
// L810: round_ni r9.xy, r0.xyxx
r9.xy=(asuint(floor(asfloat(ref_u4(r0.xyxx))))).xy;
// L811: frc r0.xy, r0.xyxx
r0.xy=(asuint(frac(asfloat(ref_u4(r0.xyxx))))).xy;
// L812: mul r9.xy, |r9.xyxx|, l(0.500000, 0.500000, 0.000000, 0.000000)
r9.xy=(asuint(abs(asfloat(ref_u4(r9.xyxx)))*ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L813: ge r9.zw, r9.xxxy, -r9.xxxy
r9.zw=(ref_mask(ref_ge(asfloat(ref_u4(r9.xxxy)),(-asfloat(ref_u4(r9.xxxy)))))).zw;
// L814: frc r9.xy, r9.xyxx
r9.xy=(asuint(frac(asfloat(ref_u4(r9.xyxx))))).xy;
// L815: movc r9.xy, r9.zwzz, r9.xyxx, -r9.xyxx
{uint4 c=ref_u4(r9.zwzz),yes=ref_u4(r9.xyxx),no=(ref_u4(r9.xyxx)^ref_u4(0x80000000u));r9.xy=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).xy;}
// L816: add r9.xy, r9.xyxx, r9.xyxx
r9.xy=(asuint(asfloat(ref_u4(r9.xyxx))+asfloat(ref_u4(r9.xyxx)))).xy;
// L817: add r9.zw, -r0.xxxy, l(0.000000, 0.000000, 1.000000, 1.000000)
r9.zw=(asuint((-asfloat(ref_u4(r0.xxxy)))+ref_f4(0.000000,0.000000,1.000000,1.000000))).zw;
// L818: add r9.zw, -r0.xxxy, r9.zzzw
r9.zw=(asuint((-asfloat(ref_u4(r0.xxxy)))+asfloat(ref_u4(r9.zzzw)))).zw;
// L819: mad r0.xy, r9.xyxx, r9.zwzz, r0.xyxx
r0.xy=(asuint(asfloat(ref_u4(r9.xyxx))*asfloat(ref_u4(r9.zwzz))+asfloat(ref_u4(r0.xyxx)))).xy;
// L820: mad r4.w, -r3.w, cb2[50].z, l(1.000000)
r4.w=(asuint((-asfloat(ref_u4(r3.w)))*ref_f4(scope_reference_cb[50].z)+ref_f4(1.000000))).w;
// L821: mul r5.w, r4.w, cb2[50].w
r5.w=(asuint(asfloat(ref_u4(r4.w))*ref_f4(scope_reference_cb[50].w))).w;
// L822: mul r5.w, r5.w, r5.w
r5.w=(asuint(asfloat(ref_u4(r5.w))*asfloat(ref_u4(r5.w)))).w;
// L823: mul r5.w, r5.w, l(1.442695)
r5.w=(asuint(asfloat(ref_u4(r5.w))*ref_f4(1.442695))).w;
// L824: exp r5.w, r5.w
r5.w=(asuint(dx_exp(asfloat(ref_u4(r5.w))))).w;
// L825: div r5.w, l(1.000000, 1.000000, 1.000000, 1.000000), r5.w
r5.w=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r5.w)))).w;
// L826: lt r8.w, l(0.000010), |r4.w|
r8.w=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r4.w)))))).w;
// L827: ge r4.w, r4.w, l(0.000000)
r4.w=(ref_mask(ref_ge(asfloat(ref_u4(r4.w)),ref_f4(0.000000)))).w;
// L828: add r5.w, -r5.w, l(1.000000)
r5.w=(asuint((-asfloat(ref_u4(r5.w)))+ref_f4(1.000000))).w;
// L829: add r5.w, -r5.w, l(1.000000)
r5.w=(asuint((-asfloat(ref_u4(r5.w)))+ref_f4(1.000000))).w;
// L830: movc r4.w, r4.w, r5.w, l(1.000000)
{uint4 c=ref_u4(r4.w),yes=ref_u4(r5.w),no=ref_u4(asuint(1.000000));r4.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L831: movc r4.w, r8.w, r4.w, l(1.000000)
{uint4 c=ref_u4(r8.w),yes=ref_u4(r4.w),no=ref_u4(asuint(1.000000));r4.w=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).w;}
// L832: mad r5.xyz, cb2[51].xyzx, r5.xyzx, -cb2[51].xyzx
r5.xyz=(asuint(ref_f4(scope_reference_cb[51].xyzx)*asfloat(ref_u4(r5.xyzx))+(-ref_f4(scope_reference_cb[51].xyzx)))).xyz;
// L833: mad r5.xyz, cb2[51].wwww, r5.xyzx, cb2[51].xyzx
r5.xyz=(asuint(ref_f4(scope_reference_cb[51].wwww)*asfloat(ref_u4(r5.xyzx))+ref_f4(scope_reference_cb[51].xyzx))).xyz;
// L834: mad r9.xyz, r5.xyzx, cb2[52].xyzx, -r5.xyzx
r9.xyz=(asuint(asfloat(ref_u4(r5.xyzx))*ref_f4(scope_reference_cb[52].xyzx)+(-asfloat(ref_u4(r5.xyzx))))).xyz;
// L835: mad r5.xyz, cb2[52].wwww, r9.xyzx, r5.xyzx
r5.xyz=(asuint(ref_f4(scope_reference_cb[52].wwww)*asfloat(ref_u4(r9.xyzx))+asfloat(ref_u4(r5.xyzx)))).xyz;
// L836: sample_l_indexable(texture2d)(float,float,float,float) r9.xyz, r0.xyxx, t6.xyzw, s1, l(0.000000)
r9.xyz=(asuint(ref_sample_6(sampleUv(6,asfloat(ref_u4(r0.xyxx)).xy),ref_f4(0.000000).x).xyzw)).xyz;
// L837: mul r5.xyz, r4.wwww, r5.xyzx
r5.xyz=(asuint(asfloat(ref_u4(r4.wwww))*asfloat(ref_u4(r5.xyzx)))).xyz;
// L838: mul r5.xyz, r5.xyzx, r9.xyzx
r5.xyz=(asuint(asfloat(ref_u4(r5.xyzx))*asfloat(ref_u4(r9.xyzx)))).xyz;
// L839: mad r3.xyz, r6.xyzx, r3.xyzx, -r2.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r6.xyzx))*asfloat(ref_u4(r3.xyzx))+(-asfloat(ref_u4(r2.xyzx))))).xyz;
// L840: mad r2.xyz, cb2[53].zzzz, r3.xyzx, r2.xyzx
r2.xyz=(asuint(ref_f4(scope_reference_cb[53].zzzz)*asfloat(ref_u4(r3.xyzx))+asfloat(ref_u4(r2.xyzx)))).xyz;
// L841: log r2.xyz, r2.xyzx
r2.xyz=(asuint(dx_log(asfloat(ref_u4(r2.xyzx))))).xyz;
// L842: mul r2.xyz, r2.xyzx, cb2[53].wwww
r2.xyz=(asuint(asfloat(ref_u4(r2.xyzx))*ref_f4(scope_reference_cb[53].wwww))).xyz;
// L843: exp r2.xyz, r2.xyzx
r2.xyz=(asuint(dx_exp(asfloat(ref_u4(r2.xyzx))))).xyz;
// L844: dp3 r0.x, r2.xyzx, l(0.212639, 0.715169, 0.072192, 0.000000)
r0.x=(asuint(ref_f4(dot(asfloat(ref_u4(r2.xyzx)).xyz,ref_f4(0.212639,0.715169,0.072192,0.000000).xyz)))).x;
// L845: add r2.xyz, -r0.xxxx, r2.xyzx
r2.xyz=(asuint((-asfloat(ref_u4(r0.xxxx)))+asfloat(ref_u4(r2.xyzx)))).xyz;
// L846: mad r2.xyz, cb2[54].xxxx, r2.xyzx, r0.xxxx
r2.xyz=(asuint(ref_f4(scope_reference_cb[54].xxxx)*asfloat(ref_u4(r2.xyzx))+asfloat(ref_u4(r0.xxxx)))).xyz;
// L847: mul r2.xyz, r2.xyzx, r5.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r2.xyzx))*asfloat(ref_u4(r5.xyzx)))).xyz;
// L848: dp3 r0.x, r8.xyzx, l(0.212639, 0.715169, 0.072192, 0.000000)
r0.x=(asuint(ref_f4(dot(asfloat(ref_u4(r8.xyzx)).xyz,ref_f4(0.212639,0.715169,0.072192,0.000000).xyz)))).x;
// L849: mad r3.xyz, r7.xyzx, r2.wwww, -r0.xxxx
r3.xyz=(asuint(asfloat(ref_u4(r7.xyzx))*asfloat(ref_u4(r2.wwww))+(-asfloat(ref_u4(r0.xxxx))))).xyz;
// L850: mad r3.xyz, cb2[54].yyyy, r3.xyzx, r0.xxxx
r3.xyz=(asuint(ref_f4(scope_reference_cb[54].yyyy)*asfloat(ref_u4(r3.xyzx))+asfloat(ref_u4(r0.xxxx)))).xyz;
// L851: mul r3.xyz, r3.xyzx, r5.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r3.xyzx))*asfloat(ref_u4(r5.xyzx)))).xyz;
// L852: mul r3.xyz, r3.xyzx, cb2[53].yyyy
r3.xyz=(asuint(asfloat(ref_u4(r3.xyzx))*ref_f4(scope_reference_cb[53].yyyy))).xyz;
// L853: mad_sat r2.xyz, r2.xyzx, cb2[53].xxxx, r3.xyzx
r2.xyz=(asuint(dx_min(dx_max(asfloat(ref_u4(r2.xyzx))*ref_f4(scope_reference_cb[53].xxxx)+asfloat(ref_u4(r3.xyzx)),ref_f4(0.)),ref_f4(1.)))).xyz;
// L854: add r0.xy, v3.yxyy, -cb2[55].yxyy
r0.xy=(asuint(ref_f4(v3.yxyy)+(-ref_f4(scope_reference_cb[55].yxyy)))).xy;
// L855: max r3.xy, cb2[54].wzww, l(0.000010, 0.000010, 0.000000, 0.000000)
r3.xy=(asuint(dx_max(ref_f4(scope_reference_cb[54].wzww),ref_f4(0.000010,0.000010,0.000000,0.000000)))).xy;
// L856: div r0.xy, r0.xyxx, r3.xyxx
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))/asfloat(ref_u4(r3.xyxx)))).xy;
// L857: add r0.xy, r0.xyxx, cb2[55].yxyy
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))+ref_f4(scope_reference_cb[55].yxyy))).xy;
// L858: mul r3.x, cb2[56].x, l(0.017453)
r3.x=(asuint(ref_f4(scope_reference_cb[56].x)*ref_f4(0.017453))).x;
// L859: sincos r3.x, r5.x, r3.x
{float4 angle=asfloat(ref_u4(r3.x));r3.x=(asuint(sin(angle))).x;r5.x=(asuint(cos(angle))).x;}
// L860: add r0.xy, r0.xyxx, -cb2[56].zyzz
r0.xy=(asuint(asfloat(ref_u4(r0.xyxx))+(-ref_f4(scope_reference_cb[56].zyzz)))).xy;
// L861: mul r3.xy, r3.xxxx, r0.xyxx
r3.xy=(asuint(asfloat(ref_u4(r3.xxxx))*asfloat(ref_u4(r0.xyxx)))).xy;
// L862: mad r6.x, r0.y, r5.x, -r3.x
r6.x=(asuint(asfloat(ref_u4(r0.y))*asfloat(ref_u4(r5.x))+(-asfloat(ref_u4(r3.x))))).x;
// L863: mad r6.y, r0.x, r5.x, r3.y
r6.y=(asuint(asfloat(ref_u4(r0.x))*asfloat(ref_u4(r5.x))+asfloat(ref_u4(r3.y)))).y;
// L864: add r0.xy, r6.xyxx, cb2[55].zwzz
r0.xy=(asuint(asfloat(ref_u4(r6.xyxx))+ref_f4(scope_reference_cb[55].zwzz))).xy;
// L865: round_ni r3.xy, r0.xyxx
r3.xy=(asuint(floor(asfloat(ref_u4(r0.xyxx))))).xy;
// L866: frc r0.xy, r0.xyxx
r0.xy=(asuint(frac(asfloat(ref_u4(r0.xyxx))))).xy;
// L867: mul r3.xy, |r3.xyxx|, l(0.500000, 0.500000, 0.000000, 0.000000)
r3.xy=(asuint(abs(asfloat(ref_u4(r3.xyxx)))*ref_f4(0.500000,0.500000,0.000000,0.000000))).xy;
// L868: ge r5.xy, r3.xyxx, -r3.xyxx
r5.xy=(ref_mask(ref_ge(asfloat(ref_u4(r3.xyxx)),(-asfloat(ref_u4(r3.xyxx)))))).xy;
// L869: frc r3.xy, r3.xyxx
r3.xy=(asuint(frac(asfloat(ref_u4(r3.xyxx))))).xy;
// L870: movc r3.xy, r5.xyxx, r3.xyxx, -r3.xyxx
{uint4 c=ref_u4(r5.xyxx),yes=ref_u4(r3.xyxx),no=(ref_u4(r3.xyxx)^ref_u4(0x80000000u));r3.xy=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).xy;}
// L871: add r3.xy, r3.xyxx, r3.xyxx
r3.xy=(asuint(asfloat(ref_u4(r3.xyxx))+asfloat(ref_u4(r3.xyxx)))).xy;
// L872: add r5.xy, -r0.xyxx, l(1.000000, 1.000000, 0.000000, 0.000000)
r5.xy=(asuint((-asfloat(ref_u4(r0.xyxx)))+ref_f4(1.000000,1.000000,0.000000,0.000000))).xy;
// L873: add r5.xy, -r0.xyxx, r5.xyxx
r5.xy=(asuint((-asfloat(ref_u4(r0.xyxx)))+asfloat(ref_u4(r5.xyxx)))).xy;
// L874: mad r0.xy, r3.xyxx, r5.xyxx, r0.xyxx
r0.xy=(asuint(asfloat(ref_u4(r3.xyxx))*asfloat(ref_u4(r5.xyxx))+asfloat(ref_u4(r0.xyxx)))).xy;
// L875: mad r3.x, -r3.w, cb2[56].w, l(1.000000)
r3.x=(asuint((-asfloat(ref_u4(r3.w)))*ref_f4(scope_reference_cb[56].w)+ref_f4(1.000000))).x;
// L876: mul r3.y, r3.x, cb2[57].x
r3.y=(asuint(asfloat(ref_u4(r3.x))*ref_f4(scope_reference_cb[57].x))).y;
// L877: mul r3.y, r3.y, r3.y
r3.y=(asuint(asfloat(ref_u4(r3.y))*asfloat(ref_u4(r3.y)))).y;
// L878: mul r3.y, r3.y, l(1.442695)
r3.y=(asuint(asfloat(ref_u4(r3.y))*ref_f4(1.442695))).y;
// L879: exp r3.y, r3.y
r3.y=(asuint(dx_exp(asfloat(ref_u4(r3.y))))).y;
// L880: div r3.y, l(1.000000, 1.000000, 1.000000, 1.000000), r3.y
r3.y=(asuint(ref_f4(1.000000,1.000000,1.000000,1.000000)/asfloat(ref_u4(r3.y)))).y;
// L881: lt r3.z, l(0.000010), |r3.x|
r3.z=(ref_mask(ref_lt(ref_f4(0.000010),abs(asfloat(ref_u4(r3.x)))))).z;
// L882: ge r3.x, r3.x, l(0.000000)
r3.x=(ref_mask(ref_ge(asfloat(ref_u4(r3.x)),ref_f4(0.000000)))).x;
// L883: add r3.y, -r3.y, l(1.000000)
r3.y=(asuint((-asfloat(ref_u4(r3.y)))+ref_f4(1.000000))).y;
// L884: add r3.y, -r3.y, l(1.000000)
r3.y=(asuint((-asfloat(ref_u4(r3.y)))+ref_f4(1.000000))).y;
// L885: movc r3.x, r3.x, r3.y, l(1.000000)
{uint4 c=ref_u4(r3.x),yes=ref_u4(r3.y),no=ref_u4(asuint(1.000000));r3.x=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).x;}
// L886: movc r3.x, r3.z, r3.x, l(1.000000)
{uint4 c=ref_u4(r3.z),yes=ref_u4(r3.x),no=ref_u4(asuint(1.000000));r3.x=(ref_u4(c.x!=0u?yes.x:no.x,c.y!=0u?yes.y:no.y,c.z!=0u?yes.z:no.z,c.w!=0u?yes.w:no.w)).x;}
// L887: sample_l_indexable(texture2d)(float,float,float,float) r3.yzw, r0.xyxx, t7.wxyz, s1, l(0.000000)
r3.yzw=(asuint(ref_sample_7(sampleUv(7,asfloat(ref_u4(r0.xyxx)).xy),ref_f4(0.000000).x).wxyz)).yzw;
// L888: dp3 r0.x, r3.yzwy, l(0.212639, 0.715169, 0.072192, 0.000000)
r0.x=(asuint(ref_f4(dot(asfloat(ref_u4(r3.yzwy)).xyz,ref_f4(0.212639,0.715169,0.072192,0.000000).xyz)))).x;
// L889: mul r0.y, r3.x, cb2[58].w
r0.y=(asuint(asfloat(ref_u4(r3.x))*ref_f4(scope_reference_cb[58].w))).y;
// L890: mul r0.x, r0.y, r0.x
r0.x=(asuint(asfloat(ref_u4(r0.y))*asfloat(ref_u4(r0.x)))).x;
// L891: mad r3.xyz, r10.yzwy, -r4.xyzx, r4.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r10.yzwy))*(-asfloat(ref_u4(r4.xyzx)))+asfloat(ref_u4(r4.xyzx)))).xyz;
// L892: max r0.y, r0.z, r6.w
r0.y=(asuint(dx_max(asfloat(ref_u4(r0.z)),asfloat(ref_u4(r6.w))))).y;
// L893: add r4.xyz, -r3.xyzx, cb2[59].xyzx
r4.xyz=(asuint((-asfloat(ref_u4(r3.xyzx)))+ref_f4(scope_reference_cb[59].xyzx))).xyz;
// L894: mad r3.xyz, r0.yyyy, r4.xyzx, r3.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r0.yyyy))*asfloat(ref_u4(r4.xyzx))+asfloat(ref_u4(r3.xyzx)))).xyz;
#ifdef SCOPE_REFERENCE_INSIDE_BLEND
{float3 imageColor=asfloat(r3.xyz);SCOPE_REFERENCE_INSIDE_BLEND(sourceUv,imageColor);r3.xyz=asuint(imageColor);}
#endif
// L895: mad r3.xyz, r7.xyzx, r2.wwww, r3.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r7.xyzx))*asfloat(ref_u4(r2.wwww))+asfloat(ref_u4(r3.xyzx)))).xyz;
// L896: add r3.xyz, -r3.xyzx, l(1.000000, 1.000000, 1.000000, 0.000000)
r3.xyz=(asuint((-asfloat(ref_u4(r3.xyzx)))+ref_f4(1.000000,1.000000,1.000000,0.000000))).xyz;
// L897: add r2.xyz, -r2.xyzx, l(1.000000, 1.000000, 1.000000, 0.000000)
r2.xyz=(asuint((-asfloat(ref_u4(r2.xyzx)))+ref_f4(1.000000,1.000000,1.000000,0.000000))).xyz;
// L898: mad r2.xyz, -r3.xyzx, r2.xyzx, l(1.000000, 1.000000, 1.000000, 0.000000)
r2.xyz=(asuint((-asfloat(ref_u4(r3.xyzx)))*asfloat(ref_u4(r2.xyzx))+ref_f4(1.000000,1.000000,1.000000,0.000000))).xyz;
// L899: mad r3.xyz, r2.xyzx, cb2[58].xyzx, -r2.xyzx
r3.xyz=(asuint(asfloat(ref_u4(r2.xyzx))*ref_f4(scope_reference_cb[58].xyzx)+(-asfloat(ref_u4(r2.xyzx))))).xyz;
// L900: mad r0.xyz, r0.xxxx, r3.xyzx, r2.xyzx
r0.xyz=(asuint(asfloat(ref_u4(r0.xxxx))*asfloat(ref_u4(r3.xyzx))+asfloat(ref_u4(r2.xyzx)))).xyz;
#ifdef SCOPE_REFERENCE_FOOTPRINT
r7.w=asuint(SCOPE_REFERENCE_FOOTPRINT);
#endif
// L901: mul r2.xyz, r7.wwww, r0.xyzx
r2.xyz=(asuint(asfloat(ref_u4(r7.wwww))*asfloat(ref_u4(r0.xyzx)))).xyz;
// L902: mad r0.xyz, -r0.xyzx, r7.wwww, cb2[60].yzwy
r0.xyz=(asuint((-asfloat(ref_u4(r0.xyzx)))*asfloat(ref_u4(r7.wwww))+ref_f4(scope_reference_cb[60].yzwy))).xyz;
// L903: mad r0.xyz, cb2[60].xxxx, r0.xyzx, r2.xyzx
r0.xyz=(asuint(ref_f4(scope_reference_cb[60].xxxx)*asfloat(ref_u4(r0.xyzx))+asfloat(ref_u4(r2.xyzx)))).xyz;
 #ifdef SCOPE_REFERENCE_TRANSMISSION
 SCOPE_REFERENCE_TRANSMISSION(saturate(asfloat(r7.w))*opticsActive);
 #endif
 return max(asfloat(r0.xyz),0)*opticsActive;
}
#endif

// Reflectance of a two-layer anti-reflection coating against the cosine of
// incidence. Unpolarised transfer matrix for air / n=1.38 92nm / n=2.15 120nm /
// n=1.52 glass, sampled at 650/550/450nm.
#ifndef SCOPE_GLASS_AR_COATING_H
#define SCOPE_GLASS_AR_COATING_H
#include "scope_glass_authoring.h"
float3 scope_glass_film_rgb(float cosIncidence)
{
 float ci=saturate(cosIncidence);
 const float3 a0 =float3(.007119539,.009629619,.006387995);
 const float3 a15=float3(.008003071,.008630801,.007761649);
 const float3 a30=float3(.012402306,.006061269,.012849918);
 const float3 a45=float3(.026549474,.005489650,.024435959);
 const float3 a60=float3(.072131552,.024636896,.058506852);
 const float3 a65=float3(.106109870,.046303592,.085662290);
 const float3 a70=float3(.160093307,.086766298,.131351862);
 const float3 a725=float3(.198350065,.118420432,.165153452);
 const float3 a75=float3(.247024532,.161289147,.209486443);
 const float3 a775=float3(.309094630,.219261366,.267786372);
 const float3 a80=float3(.388398287,.297584850,.344647470);
 const float3 a825=float3(.489876137,.403353437,.446247692);
 const float3 a85=float3(.619871445,.546178915,.580958348);
 const float3 a86=float3(.681601586,.616468006,.646549736);
 const float3 a87=float3(.749777033,.695751030,.720132968);
 const float3 a88=float3(.825067687,.785191755,.802746538);
 const float3 a89=float3(.908207736,.886109305,.895579981);
 if(ci>=.965925826) return lerp(a15,a0,(ci-.965925826)/(1-.965925826));
 if(ci>=.866025404) return lerp(a30,a15,(ci-.866025404)/(.965925826-.866025404));
 if(ci>=.707106781) return lerp(a45,a30,(ci-.707106781)/(.866025404-.707106781));
 if(ci>=.5) return lerp(a60,a45,(ci-.5)/(.707106781-.5));
 if(ci>=.422618262) return lerp(a65,a60,(ci-.422618262)/(.5-.422618262));
 if(ci>=.342020143) return lerp(a70,a65,(ci-.342020143)/(.422618262-.342020143));
 if(ci>=.300705800) return lerp(a725,a70,(ci-.300705800)/(.342020143-.300705800));
 if(ci>=.258819045) return lerp(a75,a725,(ci-.258819045)/(.300705800-.258819045));
 if(ci>=.216439614) return lerp(a775,a75,(ci-.216439614)/(.258819045-.216439614));
 if(ci>=.173648178) return lerp(a80,a775,(ci-.173648178)/(.216439614-.173648178));
 if(ci>=.130526192) return lerp(a825,a80,(ci-.130526192)/(.173648178-.130526192));
 if(ci>=.087155743) return lerp(a85,a825,(ci-.087155743)/(.130526192-.087155743));
 if(ci>=.069756474) return lerp(a86,a85,(ci-.069756474)/(.087155743-.069756474));
 if(ci>=.052335956) return lerp(a87,a86,(ci-.052335956)/(.069756474-.052335956));
 if(ci>=.034899497) return lerp(a88,a87,(ci-.034899497)/(.052335956-.034899497));
 if(ci>=.017452406) return lerp(a89,a88,(ci-.017452406)/(.034899497-.017452406));
 return lerp(float3(1,1,1),a89,ci/.017452406);
}
float3 scope_glass_coating_hue(float3 sourceCoefficient,float cosIncidence)
{
 const float3 y=float3(.2126,.7152,.0722);
 float sourceLuma=dot(max(sourceCoefficient,0),y);
 float3 spectrum=scope_glass_film_rgb(cosIncidence);
 float spectralLuma=dot(spectrum,y);
 // Keeps the luminance of the material coefficient and takes the hue of the coating,
 // then the optic's authored coating hue and brightness.
 return sourceLuma>0 ? svp_lens_authored_reflectance(spectrum*(sourceLuma/max(spectralLuma,1.e-6))) : 0;
}
#endif

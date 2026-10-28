// Split-sum DFG table (16x16 RG half-float texels, bilinear, clamped) and the
// GGX specular lobe with multiple scattering of three.js MeshPhysicalMaterial.
#ifndef SCOPE_REFERENCE_BRDF_H
#define SCOPE_REFERENCE_BRDF_H
static const uint2 scope_reference_dfg[256]={uint2(12469,15057),uint2(12620,14925),uint2(13266,14620),uint2(13807,14376),uint2(14323,13990),uint2(14545,13625),uint2(14713,13328),uint2(14840,12882),uint2(14931,12528),uint2(14996,12233),uint2(15039,11829),uint2(15066,11525),uint2(15080,11295),uint2(15085,10976),uint2(15082,10705),uint2(15073,10495),uint2(13880,14564),uint2(13898,14542),uint2(13977,14430),uint2(14158,14124),uint2(14393,13732),uint2(14556,13410),uint2(14702,12996),uint2(14814,12596),uint2(14891,12291),uint2(14937,11834),uint2(14957,11489),uint2(14958,11194),uint2(14943,10803),uint2(14921,10506),uint2(14893,10278),uint2(14858,9960),uint2(14484,14039),uint2(14487,14025),uint2(14499,13941),uint2(14524,13740),uint2(14574,13468),uint2(14654,13106),uint2(14743,12678),uint2(14818,12344),uint2(14867,11893),uint2(14889,11509),uint2(14893,11180),uint2(14881,10751),uint2(14852,10428),uint2(14812,10128),uint2(14765,9754),uint2(14712,9466),uint2(14764,13480),uint2(14764,13475),uint2(14766,13440),uint2(14766,13347),uint2(14769,13070),uint2(14786,12713),uint2(14816,12387),uint2(14844,11957),uint2(14860,11549),uint2(14868,11215),uint2(14855,10751),uint2(14825,10403),uint2(14782,10044),uint2(14729,9651),uint2(14666,9352),uint2(14599,9029),uint2(14967,12835),uint2(14966,12831),uint2(14963,12804),uint2(14954,12723),uint2(14936,12564),uint2(14917,12347),uint2(14900,11958),uint2(14886,11569),uint2(14878,11247),uint2(14859,10765),uint2(14828,10401),uint2(14784,10011),uint2(14727,9600),uint2(14660,9289),uint2(14586,8893),uint2(14508,8533),uint2(15111,12234),uint2(15110,12234),uint2(15104,12216),uint2(15092,12156),uint2(15067,12010),uint2(15028,11776),uint2(14981,11500),uint2(14942,11205),uint2(14902,10752),uint2(14861,10393),uint2(14812,9991),uint2(14752,9570),uint2(14682,9252),uint2(14603,8808),uint2(14519,8445),uint2(14431,8145),uint2(15209,11449),uint2(15208,11451),uint2(15202,11451),uint2(15190,11438),uint2(15163,11384),uint2(15117,11274),uint2(15055,10979),uint2(14994,10648),uint2(14932,10343),uint2(14871,9936),uint2(14803,9532),uint2(14729,9218),uint2(14645,8742),uint2(14556,8381),uint2(14461,8020),uint2(14365,7603),uint2(15273,10603),uint2(15272,10607),uint2(15267,10619),uint2(15256,10631),uint2(15231,10614),uint2(15182,10535),uint2(15118,10389),uint2(15042,10167),uint2(14963,9787),uint2(14883,9447),uint2(14800,9115),uint2(14710,8665),uint2(14615,8318),uint2(14514,7911),uint2(14411,7507),uint2(14279,7198),uint2(15314,9675),uint2(15313,9683),uint2(15309,9712),uint2(15298,9759),uint2(15277,9797),uint2(15229,9773),uint2(15166,9668),uint2(15084,9487),uint2(14995,9274),uint2(14898,8910),uint2(14800,8539),uint2(14697,8234),uint2(14590,7790),uint2(14479,7409),uint2(14367,7067),uint2(14178,6621),uint2(15337,8619),uint2(15337,8631),uint2(15333,8677),uint2(15325,8769),uint2(15305,8871),uint2(15264,8940),uint2(15202,8909),uint2(15119,8775),uint2(15022,8565),uint2(14916,8328),uint2(14804,8009),uint2(14688,7614),uint2(14569,7287),uint2(14448,6888),uint2(14321,6483),uint2(14088,6171),uint2(15350,7402),uint2(15350,7419),uint2(15347,7480),uint2(15340,7613),uint2(15322,7804),uint2(15287,7973),uint2(15229,8057),uint2(15148,8012),uint2(15046,7846),uint2(14933,7611),uint2(14810,7357),uint2(14682,7069),uint2(14552,6656),uint2(14421,6316),uint2(14251,5948),uint2(14007,5528),uint2(15356,5942),uint2(15356,5977),uint2(15353,6119),uint2(15348,6294),uint2(15332,6551),uint2(15302,6824),uint2(15249,7044),uint2(15171,7122),uint2(15070,7050),uint2(14949,6861),uint2(14818,6611),uint2(14679,6349),uint2(14538,6067),uint2(14398,5651),uint2(14189,5311),uint2(13935,4958),uint2(15359,4123),uint2(15359,4153),uint2(15356,4296),uint2(15353,4646),uint2(15338,5160),uint2(15311,5508),uint2(15263,5829),uint2(15188,6042),uint2(15088,6094),uint2(14966,6001),uint2(14826,5796),uint2(14678,5543),uint2(14527,5287),uint2(14377,4985),uint2(14133,4586),uint2(13869,4257),uint2(15360,1563),uint2(15360,1642),uint2(15358,2076),uint2(15354,2636),uint2(15341,3350),uint2(15317,4019),uint2(15273,4429),uint2(15203,4732),uint2(15105,4911),uint2(14981,4932),uint2(14836,4818),uint2(14679,4621),uint2(14517,4386),uint2(14359,4156),uint2(14083,3795),uint2(13808,3437),uint2(15360,122),uint2(15360,137),uint2(15358,285),uint2(15355,636),uint2(15344,1274),uint2(15322,2177),uint2(15281,2765),uint2(15215,3223),uint2(15120,3451),uint2(14995,3569),uint2(14846,3567),uint2(14681,3466),uint2(14511,3305),uint2(14344,3121),uint2(14037,2800),uint2(13753,2467),uint2(15360,0),uint2(15360,1),uint2(15359,21),uint2(15355,89),uint2(15346,253),uint2(15325,479),uint2(15287,796),uint2(15225,1148),uint2(15133,1492),uint2(15008,1749),uint2(14856,1882),uint2(14685,1886),uint2(14506,1783),uint2(14324,1608),uint2(13996,1398),uint2(13702,1183)};
float2 scope_reference_dfg_texel(int2 p) { p=clamp(p,0,15);return f16tof32(scope_reference_dfg[p.y*16+p.x]); }
float2 scope_reference_sample_dfg(float roughness,float dotNV) {
 float2 p=float2(roughness,dotNV)*16-.5;int2 i=(int2)floor(p);float2 f=frac(p);
 return lerp(lerp(scope_reference_dfg_texel(i),scope_reference_dfg_texel(i+int2(1,0)),f.x),
             lerp(scope_reference_dfg_texel(i+int2(0,1)),scope_reference_dfg_texel(i+1),f.x),f.y);
}
// Smallest roughness the specular lobe is evaluated at.
static const float scope_reference_min_roughness=.0525;
// Weight of the average Fresnel reflectance between f0 and white, 1/21.
static const float scope_reference_average_fresnel=.047619;
// Single plus multiple scattering for one light direction, without the N.L factor.
float3 scope_reference_ggx_specular(float3 f0,float f90,float roughness,float nl,float nv,float nh,float vh)
{
 float alpha=roughness*roughness,a2=alpha*alpha;
 float fresnel=exp2((-5.55473*vh-6.98316)*vh);
 float3 F=f0*(1-fresnel)+f90*fresnel;
 float gv=nl*sqrt(a2+(1-a2)*nv*nv);
 float gl=nv*sqrt(a2+(1-a2)*nl*nl);
 float visibility=.5/max(gv+gl,1.e-6);
 float denominator=nh*nh*(a2-1)+1;
 float distribution=a2/(3.14159265359*denominator*denominator);
 float2 dfgV=scope_reference_sample_dfg(roughness,nv),dfgL=scope_reference_sample_dfg(roughness,nl);
 float3 FssV=f0*dfgV.x+f90*dfgV.y,FssL=f0*dfgL.x+f90*dfgL.y;
 float EmsV=1-dfgV.x-dfgV.y,EmsL=1-dfgL.x-dfgL.y;
 float3 Favg=f0+(1-f0)*scope_reference_average_fresnel;
 float3 multi=FssV*FssL*Favg/(1-EmsV*EmsL*Favg+1.e-6)*(EmsV*EmsL);
 return F*visibility*distribution+multi;
}
#endif

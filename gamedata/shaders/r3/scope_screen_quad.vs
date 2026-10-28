// Full-screen quad in clip space (POSITIONT xy, as the deferred combine geometry stores it): position only,
// for the scope glass composition and the scope reflection passes.
#include "common.h"
struct ScopeScreenQuadInput
{
 float4 P:POSITIONT; // xy position, zw texture coordinates
 float2 tcJ:TEXCOORD0;
};
float4 main(ScopeScreenQuadInput I):SV_Position
{
 return float4(I.P.x,-I.P.y,0,1);
}

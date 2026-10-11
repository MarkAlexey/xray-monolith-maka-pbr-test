// Monolith: IX-Ray single-pass cascaded sun - fullscreen quad.
// Same vertex layout as combine_1.vs (g_combine: P.xy = clip position, P.zw = uv), without
// the tonemap fetch.
#include "common.h"

struct _in
{
	float4 P	: POSITIONT;	// xy = clip position (y flipped), zw = uv
	float2 tcJ	: TEXCOORD0;	// jitter coords
};

struct v2p_csm
{
	float2 tc0	: TEXCOORD0;
	float2 tcJ	: TEXCOORD1;
	float4 hpos	: SV_Position;
};

v2p_csm main( _in I )
{
	v2p_csm O;
	O.hpos	= float4( I.P.x, -I.P.y, 0, 1 );
	O.tc0	= I.P.zw;
	O.tcJ	= I.tcJ;
	return O;
}

FXVS;

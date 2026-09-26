#include "common.h"

Buffer<uint4>	occ_spans	: register(t12);	// first count flags record, box min and the k-th tested span, box max
cbuffer	dt_occ_cb	{ float4 dt_occ_box; };	// x first tested entry of the var, y frame stamp

struct	v2p_occ_box
{
	float4	hpos	: SV_Position;
	nointerpolation uint2	span	: TEXCOORD0;
};

// Box corner pulled 16 depth steps toward the camera
v2p_occ_box	main	(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	uint	s	= occ_spans[(uint(dt_occ_box.x) + iid) * 3 + 1].w;
	float3	lo	= asfloat(occ_spans[s * 3 + 1].xyz);
	float3	hi	= asfloat(occ_spans[s * 3 + 2].xyz);
	float3	p	= float3((vid & 1) ? hi.x : lo.x, (vid & 2) ? hi.y : lo.y, (vid & 4) ? hi.z : lo.z);

	v2p_occ_box	O;
	O.hpos	= mul(m_WVP, float4(p, 1));
	O.hpos.z	-= 16 * O.hpos.w / 16777216.0;
	O.span	= uint2(s, uint(dt_occ_box.y));
	return	O;
}
FXVS;

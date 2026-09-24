#ifndef	dt_inst_h_included
#define	dt_inst_h_included

Buffer<uint4>	dt_rows	: register(t8);	// 3x4 transform rows, then sun sun sun hemi
cbuffer	dt_draw_cb	{ float4 dt_draw; };	// x first instance of the draw, y pass fade scale

uint	dt_inst	(uint iid)	{ return uint(dt_draw.x) + iid; }

float4	dt_row	(uint n, uint k)
{
	float4	r	= asfloat(dt_rows[n * 4 + k]);
	precise float3	s	= r.xyz * dt_draw.y;
	return	float4(s, r.w);
}

#endif

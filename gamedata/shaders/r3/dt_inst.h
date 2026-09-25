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

#ifdef	DT_MERGE
Buffer<uint4>	dt_verts	: register(t9);	// detail vertices, position then the four shorts
#define	DT_MERGE_K	64

v_detail	dt_vertex	(uint vid, uint iid, out uint n)
{
	uint	count	= uint(dt_draw.w);
	uint	copy	= vid / count;
	uint	i	= uint(dt_draw.z) + vid - copy * count;
	uint4	m	= dt_verts[i * 2 + 1];
	v_detail	v;
	v.pos	= asfloat(dt_verts[i * 2]);
	v.misc	= int4(int(m.x << 16) >> 16, int(m.x) >> 16, int(m.y << 16) >> 16, int(m.y) >> 16);
	n	= uint(dt_draw.x) + iid * DT_MERGE_K + copy;
	return	v;
}
#endif

#endif

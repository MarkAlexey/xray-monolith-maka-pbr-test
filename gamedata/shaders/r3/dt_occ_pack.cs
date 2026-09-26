Buffer<uint4>	occ_spans	: register(t0);	// first count flags record, then the box min and max
Buffer<uint4>	occ_recs	: register(t1);	// out base, index count, first index, then the span range
Buffer<uint4>	occ_head	: register(t2);	// first span, end span, expand groups in x, out offset base, then frame stamp and mode
RWBuffer<uint>	occ_vis		: register(u0);	// span stamps, then the out offset of every span
RWBuffer<uint>	occ_idx		: register(u1);	// rows index of every kept instance
RWBuffer<uint>	occ_args	: register(u2);	// indexed instanced indirect args per record

#define	OCC_KEEP	1u
#define	OCC_HEAD	2u
#define	OCC_TAIL	4u
#define	OCC_NONE	0xffffffffu
#define	OCC_K		64u

groupshared uint2	occ_sum[1024];

bool	occ_keep	(uint4 sp, uint s, uint2 mode)
{
	bool	seen	= occ_vis[s] == mode.x;
	if (mode.y == 2)
		return	(sp.z & OCC_KEEP) == 0 && seen;
	return	(sp.z & OCC_KEEP) != 0 || seen;
}

[numthreads(1024, 1, 1)]
void	main	(uint t : SV_GroupIndex)
{
	uint4	head	= occ_head[0];
	uint2	mode	= occ_head[1].xy;
	uint	per	= (head.y - head.x + 1023) / 1024;
	uint	s0	= min(head.x + t * per, head.y);
	uint	s1	= min(s0 + per, head.y);

	// x kept instances since the last record start, y set when a start falls inside the chunk
	uint2	sum	= uint2(0, 0);
	for (uint s = s0; s < s1; s++)
	{
		uint4	sp	= occ_spans[s * 3];
		if (sp.z & OCC_HEAD)
			sum	= uint2(0, 1);
		if (occ_keep(sp, s, mode))
			sum.x	+= sp.y;
	}
	occ_sum[t]	= sum;
	GroupMemoryBarrierWithGroupSync();

	for (uint d = 1; d < 1024; d <<= 1)
	{
		uint2	v	= occ_sum[t];
		if (t >= d && v.y == 0)
		{
			uint2	p	= occ_sum[t - d];
			v	= uint2(v.x + p.x, p.y);
		}
		GroupMemoryBarrierWithGroupSync();
		occ_sum[t]	= v;
		GroupMemoryBarrierWithGroupSync();
	}

	uint	run	= t ? occ_sum[t - 1].x : 0;
	for (uint k = s0; k < s1; k++)
	{
		uint4	sp	= occ_spans[k * 3];
		uint4	rec	= occ_recs[sp.w * 2];
		if (sp.z & OCC_HEAD)
			run	= 0;
		bool	keep	= occ_keep(sp, k, mode);
		occ_vis[head.w + k]	= keep ? rec.x + run : OCC_NONE;
		if (keep)
			run	+= sp.y;
		if (sp.z & OCC_TAIL)
		{
			// Pads the unused merge copies with an index past the rows buffer
			uint	inst	= (run + OCC_K - 1) / OCC_K;
			for (uint i = run; i < inst * OCC_K; i++)
				occ_idx[rec.x + i]	= OCC_NONE;
			occ_args[sp.w * 5 + 0]	= rec.y;
			occ_args[sp.w * 5 + 1]	= inst;
			occ_args[sp.w * 5 + 2]	= rec.z;
			occ_args[sp.w * 5 + 3]	= 0;
			occ_args[sp.w * 5 + 4]	= 0;
		}
	}
}

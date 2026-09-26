Buffer<uint4>	occ_spans	: register(t0);	// first count flags record, then the box min and max
Buffer<uint4>	occ_head	: register(t2);	// first span, end span, expand groups in x, out offset base
RWBuffer<uint>	occ_vis		: register(u0);	// span stamps, then the out offset of every span
RWBuffer<uint>	occ_idx		: register(u1);	// rows index of every kept instance

[numthreads(64, 1, 1)]
void	main	(uint3 gid : SV_GroupID, uint t : SV_GroupIndex)
{
	uint4	head	= occ_head[0];
	uint	s	= head.x + gid.y * head.z + gid.x;
	if (s >= head.y)
		return;
	uint	at	= occ_vis[head.w + s];
	if (at == 0xffffffffu)
		return;
	uint4	sp	= occ_spans[s * 3];
	for (uint i = t; i < sp.y; i += 64)
		occ_idx[at + i]	= sp.x + i;
}

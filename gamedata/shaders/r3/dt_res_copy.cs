Buffer<uint4>	res_old	: register(t0);
Buffer<uint4>	res_up	: register(t1);	// header jobs gx rows base, then src dst count upload per job, then rows
RWBuffer<uint4>	res_new	: register(u0);

[numthreads(64, 1, 1)]
void	main	(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
	uint4	head	= res_up[0];
	uint	id	= gid.y * head.y + gid.x;
	if (id >= head.x)
		return;

	uint4	job	= res_up[1 + id];
	uint	n	= job.z * 4;
	uint	src	= job.x * 4;
	uint	dst	= job.y * 4;
	if (job.w)
	{
		for (uint i = tid.x; i < n; i += 64)
			res_new[dst + i] = res_up[src + i];
	}
	else
	{
		for (uint i = tid.x; i < n; i += 64)
			res_new[dst + i] = res_old[src + i];
	}
}

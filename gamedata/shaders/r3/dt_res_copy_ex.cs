Buffer<uint4>	res_old	: register(t0);
Buffer<uint4>	res_up	: register(t1);	// header jobs gx rows base ex base, then src dst count upload per job, then rows, then ex
Buffer<uint4>	res_ex_old	: register(t2);
RWBuffer<uint4>	res_new	: register(u0);
RWBuffer<uint4>	res_ex_new	: register(u1);

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
		for (uint e = tid.x; e < job.z; e += 64)
			res_ex_new[job.y + e] = res_up[head.w + job.x + e];
	}
	else
	{
		for (uint i = tid.x; i < n; i += 64)
			res_new[dst + i] = res_old[src + i];
		for (uint e = tid.x; e < job.z; e += 64)
			res_ex_new[job.y + e] = res_ex_old[job.x + e];
	}
}

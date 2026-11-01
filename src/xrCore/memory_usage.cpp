#include "stdafx.h"
#include <malloc.h>
#include <errno.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib") // GetProcessMemoryInfo whatever PSAPI_VERSION the SDK picks

XRCORE_API void vminfo(size_t* _free, size_t* reserved, size_t* committed)
{
	MEMORY_BASIC_INFORMATION memory_info;
	memory_info.BaseAddress = 0;
	*_free = *reserved = *committed = 0;
	while (VirtualQuery(memory_info.BaseAddress, &memory_info, sizeof(memory_info)))
	{
		switch (memory_info.State)
		{
		case MEM_FREE:
			*_free += memory_info.RegionSize;
			break;
		case MEM_RESERVE:
			*reserved += memory_info.RegionSize;
			break;
		case MEM_COMMIT:
			*committed += memory_info.RegionSize;
			break;
		}
		memory_info.BaseAddress = (char*)memory_info.BaseAddress + memory_info.RegionSize;
	}
}

xrCriticalSection mem_lock;
XRCORE_API void log_vminfo()
{
    xrCriticalSectionGuard g(mem_lock);
	PROF_EVENT("log_vminfo");
	size_t w_free, w_reserved, w_committed;
	vminfo(&w_free, &w_reserved, &w_committed);
	Msg(
		"* [win32]: free[%lld K], reserved[%lld K], committed[%lld K]",
		w_free / 1024,
		w_reserved / 1024,
		w_committed / 1024
	);
}

// Monolith: mem_usage() runs at the end of every loading phase ("phase cmem"), around
// IGame_Persistent::Prefetch and in stat_memory (main menu). It used to walk the whole CRT heap
// with _heapwalk, which holds the heap lock for the entire walk: on a GAMMA-sized heap that is
// seconds of loading screen, with every other thread's allocations blocked meanwhile.
// It now returns the process's private bytes (GetProcessMemoryInfo, constant time). That is
// more than the CRT heap alone (D3D / driver and other allocators' memory count too), so the
// numbers in the log are larger than before; the deltas around Prefetch stay meaningful.
// -mem_heapwalk on the command line brings the heap walk back: it also checks the heap's
// integrity (FATAL on a corrupt heap when assert is set).
// (Stall found by TheMockinJay, maka-pbr-merged, who replaced it with a tbbmalloc counter.)
static bool mem_usage_heapwalk()
{
	static const bool walk = Core.Params && strstr(Core.Params, "-mem_heapwalk");
	return walk;
}

size_t xrMemory::mem_usage(bool assert)
{
	if (!mem_usage_heapwalk())
	{
		PROCESS_MEMORY_COUNTERS_EX counters = {};
		counters.cb = sizeof(counters);
		if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
			return counters.PrivateUsage;
	}
    xrCriticalSectionGuard g(mem_lock);
	PROF_EVENT("mem_usage");
	_HEAPINFO hinfo = {};
	int status;
	size_t bytesUsed = 0;
	while ((status = _heapwalk(&hinfo)) == _HEAPOK)
	{
		if (hinfo._useflag == _USEDENTRY)
			bytesUsed += hinfo._size;
	}
	switch (status)
	{
	case _HEAPEMPTY:
		break;
	case _HEAPEND:
		break;
	case _HEAPBADPTR:
		if (assert) FATAL("bad pointer to heap");
		break;
	case _HEAPBADBEGIN:
        if (assert) FATAL("bad start of heap");
		break;
	case _HEAPBADNODE:
        if (assert) FATAL("bad node in heap");
		break;
	}
	return bytesUsed;
}

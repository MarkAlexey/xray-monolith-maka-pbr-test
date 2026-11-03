#include "stdafx.h"
#pragma hdrstop

#include "xrMemory_align.h"
#include "xrMemory_pure.h"
#include <atomic>
#include <cerrno>

#ifndef __BORLANDC__

#ifndef DEBUG_MEMORY_MANAGER
# define debug_mode 0
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
XRCORE_API void* g_globalCheckAddr = NULL;
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
extern void save_stack_trace();
#endif // DEBUG_MEMORY_MANAGER

MEMPOOL mem_pools[mem_pools_count];

// MSVC
ICF u8* acc_header(void* P)
{
	u8* _P = (u8*)P;
	return _P - 1;
}

ICF u32 get_header(void* P) { return (u32)*acc_header(P); }
ICF u32 get_pool(size_t size)
{
	u32 pid = u32(size / mem_pools_ebase);
	if (pid >= mem_pools_count) return mem_generic;
	else return pid;
}

#define PURE_ALLOC
#define PURE_MEMORY_FILL_ZERO
#define PURE_MEMORY_ALIGNMENT 1 << 4

// ------------------------------------------------------------------------------------------
// Monolith: oneTBB's scalable allocator (tbbmalloc) as an opt-in alternative to the CRT heap
//
// -tbbmalloc on the command line routes every xrMemory allocation (xr_new, xr_alloc, the xr_*
// containers' xalloc) through tbbmalloc.dll instead of _aligned_malloc. Without it nothing
// changes. Meant for A/B measurements: same exe, one launch option.
//
// Settings / behaviour
//   - Decided once, at the first allocation (static initialisation, before Core.Params exists),
//     from GetCommandLineA(). Every block is freed by the heap that allocated it because the
//     choice never changes afterwards.
//   - tbbmalloc.dll is loaded at run time from the exe's folder (the one in sdk/binaries,
//     oneTBB 2023.0.0 like tbb12.dll, Intel-signed). Missing DLL or export: the CRT heap is used
//     and xr_memory_heap_name() says why (logged at startup).
//   - mem_compact() (load boundaries) also hands tbbmalloc's cached free memory back to the
//     system (TBBMALLOC_CLEAN_ALL_BUFFERS).
//   - Every block goes back to the heap that allocated it, whatever the switch says at that
//     moment: while tbbmalloc.dll is loaded, mem_free and mem_realloc ask tbbmalloc whether it
//     owns the block (__TBB_malloc_safer_free / __TBB_malloc_safer_aligned_msize, the checks
//     oneTBB's own malloc proxy runs on every CRT free; foreign pointers are probed under SEH)
//     and hand blocks it does not own to the CRT. Only new allocations follow the switch.
//     Reason: a crash dump (level change, CCoverManager::clear) showed mem_free taking the CRT
//     branch for a tbbmalloc block in a session that had run on tbbmalloc from the start, and
//     _aligned_free / HeapFree on it faulted in ntdll. The switch is now also kept with a check
//     copy (~choice); if they ever disagree the allocator recovers (tbbmalloc if it was loaded)
//     and mem_compact() logs it once, with the value it read (xr_memory_heap_check()).
//
// The zero-fill contract, kept for both heaps:
//   mem_alloc(zeroMemory) returns zeroed memory, and mem_realloc zero-fills the part of a block
//   it grows (xrMemory_subst_msvc.cpp has done that since PURE_MEMORY_FILL_ZERO; 681e9b72a also
//   zeroes the bytes a shrink drops, so they come back clean). The CRT reports the exact size of
//   a block (_aligned_msize); tbbmalloc only reports its usable size (scalable_msize), which
//   can be larger than what was asked for. So with tbbmalloc every block keeps an invariant:
//   the slack between the requested size and the usable size is zero. Then "grow from the old
//   usable size" zero-fills exactly what the CRT path does. Cost: one scalable_msize per
//   allocation / realloc and a memset of the slack (at most one size-class step).
//
// Caveats
//   - Memory that bypasses xrMemory (plain malloc/new in third-party code, Lua's allocator,
//     D3D) is not affected.
//   - Memory.mem_usage() reports process private bytes either way (memory_usage.cpp).
// (Idea from TheMockinJay's maka-pbr-merged, which linked tbbmalloc unconditionally.)
// ------------------------------------------------------------------------------------------
namespace
{
// Plain data, zero-initialised before any code runs (static initialisation of other files may
// allocate before this file's dynamic initialisers would)
struct tbb_heap_api
{
	void* (__cdecl* aligned_malloc)(size_t, size_t);
	void* (__cdecl* aligned_realloc)(void*, size_t, size_t);
	void (__cdecl* aligned_free)(void*);
	size_t (__cdecl* msize)(void*);
	int (__cdecl* command)(int, void*);
	// Ownership test and the free oneTBB's malloc proxy uses for CRT calls: they return / free
	// only blocks tbbmalloc allocated, and pass anything else to the function given
	void (__cdecl* safer_free)(void*, void (__cdecl*)(void*));
	size_t (__cdecl* safer_aligned_msize)(void*, size_t, size_t, size_t (__cdecl*)(void*, size_t, size_t));
};

tbb_heap_api tbb_heap_storage;
const char* tbb_heap_state = "CRT heap";
// 0: not decided yet, 1: CRT heap, 2: tbbmalloc. tbb_heap_choice_check holds ~choice once decided.
std::atomic<int> tbb_heap_choice{0};
std::atomic<int> tbb_heap_choice_check{0};
// tbbmalloc.dll with every export above: set once with the decision, never cleared. Frees and
// reallocs consult it (not the switch) to find the owner of a block.
std::atomic<bool> tbb_heap_loaded{false};
// The switch read a value its check copy did not match: that value, for xr_memory_heap_check()
std::atomic<int> tbb_heap_mismatch{0};
std::atomic<bool> tbb_heap_mismatch_seen{false};

const tbb_heap_api* tbb_heap_decide()
{
	// The first allocation happens during static initialisation, on the main thread, before any
	// other thread exists. Should two threads still get here, both compute the same answer
	// (same command line, same DLL; LoadLibrary only counts up), so the race is harmless.
	int choice = 1;
	const char* cmd = GetCommandLineA();
	if (cmd && strstr(cmd, "-tbbmalloc"))
	{
		if (HMODULE module = LoadLibraryA("tbbmalloc.dll"))
		{
			tbb_heap_api& a = tbb_heap_storage;
			a.aligned_malloc = (decltype(a.aligned_malloc))GetProcAddress(module, "scalable_aligned_malloc");
			a.aligned_realloc = (decltype(a.aligned_realloc))GetProcAddress(module, "scalable_aligned_realloc");
			a.aligned_free = (decltype(a.aligned_free))GetProcAddress(module, "scalable_aligned_free");
			a.msize = (decltype(a.msize))GetProcAddress(module, "scalable_msize");
			a.command = (decltype(a.command))GetProcAddress(module, "scalable_allocation_command");
			a.safer_free = (decltype(a.safer_free))GetProcAddress(module, "__TBB_malloc_safer_free");
			a.safer_aligned_msize = (decltype(a.safer_aligned_msize))GetProcAddress(module, "__TBB_malloc_safer_aligned_msize");
			if (a.aligned_malloc && a.aligned_realloc && a.aligned_free && a.msize && a.safer_free && a.safer_aligned_msize)
			{
				tbb_heap_state = "tbbmalloc (-tbbmalloc)";
				choice = 2;
				tbb_heap_loaded.store(true, std::memory_order_release);
			}
			else
				tbb_heap_state = "CRT heap (-tbbmalloc: tbbmalloc.dll lacks the scalable_aligned_* / __TBB_malloc_safer_* exports)";
		}
		else
			tbb_heap_state = "CRT heap (-tbbmalloc: tbbmalloc.dll not found beside the exe)";
	}
	tbb_heap_choice_check.store(~choice, std::memory_order_relaxed);
	tbb_heap_choice.store(choice, std::memory_order_release);
	return choice == 2 ? &tbb_heap_storage : nullptr;
}

// The switch and its check copy disagree: something overwrote one of them. Remember what was
// read (reported by xr_memory_heap_check(), outside the allocator) and restore the decision from
// tbb_heap_loaded. No logging here: Msg allocates.
const tbb_heap_api* tbb_heap_recover(int seen)
{
	if (!tbb_heap_mismatch_seen.exchange(true, std::memory_order_relaxed))
		tbb_heap_mismatch.store(seen, std::memory_order_relaxed);
	const int choice = tbb_heap_loaded.load(std::memory_order_acquire) ? 2 : 1;
	tbb_heap_choice_check.store(~choice, std::memory_order_relaxed);
	tbb_heap_choice.store(choice, std::memory_order_release);
	return choice == 2 ? &tbb_heap_storage : nullptr;
}

// nullptr: CRT heap. After the first call: one plain load and a compare (x64 acquire = mov),
// no function-local static guard on the allocation path.
inline const tbb_heap_api* tbb_heap()
{
	const int choice = tbb_heap_choice.load(std::memory_order_acquire);
	if (choice == 0)
		return tbb_heap_decide();
	if (choice != ~tbb_heap_choice_check.load(std::memory_order_relaxed))
		return tbb_heap_recover(choice);
	return choice == 2 ? &tbb_heap_storage : nullptr;
}

// tbbmalloc, if it is loaded at all (even if the switch says CRT): the owner test for frees and
// reallocs. Makes sure the decision was taken first.
inline const tbb_heap_api* tbb_heap_any()
{
	tbb_heap();
	return tbb_heap_loaded.load(std::memory_order_acquire) ? &tbb_heap_storage : nullptr;
}

// Does tbbmalloc own this block? Its usable size, or 0 for a block of any other heap (CRT).
// __TBB_malloc_safer_aligned_msize sets errno for foreign blocks; keep the caller's errno.
size_t tbb_owned_size(const tbb_heap_api* api, void* block)
{
	const int saved = errno;
	const size_t size = api->safer_aligned_msize(block, PURE_MEMORY_ALIGNMENT, 0, nullptr);
	errno = saved;
	return size;
}

// The CRT's free for blocks tbbmalloc does not own (passed to __TBB_malloc_safer_free)
void __cdecl crt_aligned_free(void* block)
{
	_aligned_free(block);
}

// Zero [from, usable) of a tbbmalloc block: keeps the slack invariant described above
void tbb_zero_tail(const tbb_heap_api* api, void* block, size_t from)
{
	const size_t usable = api->msize(block);
	if (usable > from)
		memset(static_cast<u8*>(block) + from, 0, usable - from);
}
}

XRCORE_API const char* xr_memory_heap_name()
{
	tbb_heap();
	return tbb_heap_state;
}

// mem_compact(): give tbbmalloc's cached free memory back to the system
void xr_memory_heap_compact()
{
	if (const tbb_heap_api* api = tbb_heap())
		if (api->command)
			api->command(0 /* TBBMALLOC_CLEAN_ALL_BUFFERS */, nullptr);
}

// mem_compact(): report, once, that the allocator switch was found overwritten (see above)
void xr_memory_heap_check()
{
	static bool reported = false;
	if (reported || !tbb_heap_mismatch_seen.load(std::memory_order_relaxed))
		return;
	reported = true;
	Msg("! [Memory] the allocator switch was found overwritten (read %d, expected %d) and was restored; "
		"blocks still went back to their own heap. Please send this log.",
		tbb_heap_mismatch.load(std::memory_order_relaxed), tbb_heap_loaded.load() ? 2 : 1);
}

void* xrMemory::mem_alloc(size_t size
# ifdef DEBUG_MEMORY_NAME
                          , const char* _name
# endif // DEBUG_MEMORY_NAME
, bool zeroMemory)
{
	stat_calls++;

#ifdef PURE_ALLOC
	if (const tbb_heap_api* api = tbb_heap())
	{
		// size 0: _aligned_malloc returns a unique block, keep that
		void* result = api->aligned_malloc(size ? size : 1, PURE_MEMORY_ALIGNMENT);
		if (result)
			tbb_zero_tail(api, result, zeroMemory ? 0 : size);
#ifdef USE_MEMORY_MONITOR
        memory_monitor::monitor_alloc(result, size, _name);
#endif // USE_MEMORY_MONITOR
		return result;
	}
	{
		//void* result = malloc(size);
		void* result = _aligned_malloc(size, PURE_MEMORY_ALIGNMENT);
#ifdef PURE_MEMORY_FILL_ZERO
		if (result && zeroMemory)
			memset(result, 0, size);
#endif // PURE_MEMORY_FILL_ZERO

#ifdef USE_MEMORY_MONITOR
        memory_monitor::monitor_alloc(result, size, _name);
#endif // USE_MEMORY_MONITOR
		return (result);
	}
#endif // PURE_ALLOC

#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Enter();
#endif // DEBUG_MEMORY_MANAGER

	u32 _footer = debug_mode ? 4 : 0;
	void* _ptr = 0;

	//
	if (!mem_initialized /*|| debug_mode*/)
	{
		// generic
		// Igor: Reserve 1 byte for xrMemory header
		void* _real = xr_aligned_offset_malloc(1 + size + _footer, 16, 0x1);
		//void* _real = xr_aligned_offset_malloc (size + _footer, 16, 0x1);
		_ptr = (void*)(((u8*)_real) + 1);
		*acc_header(_ptr) = mem_generic;
	}
	else
	{
#ifdef DEBUG_MEMORY_MANAGER
        save_stack_trace();
#endif // DEBUG
		// accelerated
		// Igor: Reserve 1 byte for xrMemory header
		u32 pool = get_pool(1 + size + _footer);
		//u32 pool = get_pool (size+_footer);
		if (mem_generic == pool)
		{
			// generic
			// Igor: Reserve 1 byte for xrMemory header
			void* _real = xr_aligned_offset_malloc(1 + size + _footer, 16, 0x1);
			//void* _real = xr_aligned_offset_malloc (size + _footer,16,0x1);
			_ptr = (void*)(((u8*)_real) + 1);
			*acc_header(_ptr) = mem_generic;
		}
		else
		{
			// pooled
			// Igor: Reserve 1 byte for xrMemory header
			// Already reserved when getting pool id
			void* _real = mem_pools[pool].create();
			_ptr = (void*)(((u8*)_real) + 1);
			*acc_header(_ptr) = (u8)pool;
		}
	}

#ifdef DEBUG_MEMORY_MANAGER
    if (debug_mode) dbg_register(_ptr, size, _name);
    if (mem_initialized) debug_cs.Leave();
    //if(g_globalCheckAddr==_ptr){
	// __asm int 3;
	//}
	//if (_name && (0==strcmp(_name,"class ISpatial *")) && (size==376))
	//{
	// __asm int 3;
	//}
#endif // DEBUG_MEMORY_MANAGER
#ifdef USE_MEMORY_MONITOR
    memory_monitor::monitor_alloc(_ptr, size, _name);
#endif // USE_MEMORY_MONITOR
    if (zeroMemory)
	    memset(_ptr, 0, size);
	return _ptr;
}

void xrMemory::mem_free(void* P)
{
	stat_calls++;
#ifdef USE_MEMORY_MONITOR
    memory_monitor::monitor_free(P);
#endif // USE_MEMORY_MONITOR

#ifdef PURE_ALLOC
	// By owner, not by the switch: tbbmalloc frees its own blocks and passes the rest to the CRT
	if (const tbb_heap_api* api = tbb_heap_any())
	{
		if (P)
			api->safer_free(P, &crt_aligned_free);
		return;
	}
	{
		//free(P);
		_aligned_free(P);
		return;
	}
#endif // PURE_ALLOC

#ifdef DEBUG_MEMORY_MANAGER
    if (g_globalCheckAddr == P)
        __asm int 3;
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Enter();
#endif // DEBUG_MEMORY_MANAGER
	if (debug_mode) dbg_unregister(P);
	u32 pool = get_header(P);
	void* _real = (void*)(((u8*)P) - 1);
	if (mem_generic == pool)
	{
		// generic
		xr_aligned_free(_real);
	}
	else
	{
		// pooled
		VERIFY2(pool < mem_pools_count, "Memory corruption");
		mem_pools[pool].destroy(_real);
	}
#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Leave();
#endif // DEBUG_MEMORY_MANAGER
}

extern BOOL g_bDbgFillMemory;

void* xrMemory::mem_realloc(void* P, size_t size
#ifdef DEBUG_MEMORY_NAME
                            , const char* _name
#endif // DEBUG_MEMORY_NAME
)
{
	stat_calls++;

	if (0 == P)
	{
		return mem_alloc(size
# ifdef DEBUG_MEMORY_NAME
			, _name
# endif // DEBUG_MEMORY_NAME
		);
	}

#ifdef PURE_ALLOC
	// By owner, not by the switch: a tbbmalloc block is resized by tbbmalloc, any other block by
	// the CRT below (it stays in the CRT heap)
	const tbb_heap_api* owner = tbb_heap_any();
	const size_t tbb_usable = owner ? tbb_owned_size(owner, P) : 0;
	if (tbb_usable)
	{
		const tbb_heap_api* api = owner;
		// Slack invariant: [requested, usable) of every block is zero, so growing from the old
		// usable size zero-fills exactly the new bytes, as the CRT path below does
		const size_t old_usable = tbb_usable;
		void* result = api->aligned_realloc(P, size, PURE_MEMORY_ALIGNMENT);
		if (result)
		{
			if (size > old_usable)
				memset(static_cast<u8*>(result) + old_usable, 0, size - old_usable);
			// a shrink in place leaves the dropped bytes behind; a move or an in-place growth
			// leaves whatever was there: zero the new slack either way
			tbb_zero_tail(api, result, size);
		}
# ifdef USE_MEMORY_MONITOR
        memory_monitor::monitor_free(P);
        memory_monitor::monitor_alloc(result, size, _name);
# endif // USE_MEMORY_MONITOR
		return result;
	}
	{
#ifdef PURE_MEMORY_FILL_ZERO
		size_t old_size = P ? _aligned_msize(P, PURE_MEMORY_ALIGNMENT, 0) : 0;

		// A shrink can stay in place with _aligned_msize still reporting the old size,
		// so the growth zeroing below would start past the bytes being dropped now.
		// Zero them here so they come back clean when the block grows again.
		if (P && size && size < old_size)
			memset((u8*)P + size, 0, old_size - size);
#endif // PURE_MEMORY_FILL_ZERO

		//void* result = realloc(P, size);
		void* result = _aligned_realloc(P, size, PURE_MEMORY_ALIGNMENT);

#ifdef PURE_MEMORY_FILL_ZERO
		if (result && size > old_size)
			memset((u8*)result + old_size, 0, size - old_size);
#endif // PURE_MEMORY_FILL_ZERO

# ifdef USE_MEMORY_MONITOR
        memory_monitor::monitor_free(P);
        memory_monitor::monitor_alloc(result, size, _name);
# endif // USE_MEMORY_MONITOR
		return (result);
	}
#endif // PURE_ALLOC

#ifdef DEBUG_MEMORY_MANAGER
    if (g_globalCheckAddr == P)
        __asm int 3;
#endif // DEBUG_MEMORY_MANAGER

#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Enter();
#endif // DEBUG_MEMORY_MANAGER
	u32 p_current = get_header(P);
	// Igor: Reserve 1 byte for xrMemory header
	u32 p_new = get_pool(1 + size + (debug_mode ? 4 : 0));
	//u32 p_new = get_pool (size+(debug_mode?4:0));
	u32 p_mode;

	if (mem_generic == p_current)
	{
		if (p_new < p_current) p_mode = 2;
		else p_mode = 0;
	}
	else p_mode = 1;

	void* _real = (void*)(((u8*)P) - 1);
	void* _ptr = NULL;
	if (0 == p_mode)
	{
		u32 _footer = debug_mode ? 4 : 0;
#ifdef DEBUG_MEMORY_MANAGER
        if (debug_mode)
        {
            g_bDbgFillMemory = false;
            dbg_unregister(P);
            g_bDbgFillMemory = true;
        }
#endif // DEBUG_MEMORY_MANAGER
		// Igor: Reserve 1 byte for xrMemory header
		void* _real2 = xr_aligned_offset_realloc(_real, 1 + size + _footer, 16, 0x1);
		//void* _real2 = xr_aligned_offset_realloc (_real,size+_footer,16,0x1);
		_ptr = (void*)(((u8*)_real2) + 1);
		*acc_header(_ptr) = mem_generic;
#ifdef DEBUG_MEMORY_MANAGER
        if (debug_mode) dbg_register(_ptr, size, _name);
#endif // DEBUG_MEMORY_MANAGER
#ifdef USE_MEMORY_MONITOR
        memory_monitor::monitor_free(P);
        memory_monitor::monitor_alloc(_ptr, size, _name);
#endif // USE_MEMORY_MONITOR
	}
	else if (1 == p_mode)
	{
		// pooled realloc
		R_ASSERT2(p_current < mem_pools_count, "Memory corruption");
		u32 s_current = mem_pools[p_current].get_element();
		u32 s_dest = (u32)size;
		void* p_old = P;

		void* p_new = mem_alloc(size
#ifdef DEBUG_MEMORY_NAME
                                , _name
#endif // DEBUG_MEMORY_NAME
		);
		// Igor: Reserve 1 byte for xrMemory header
		// Don't bother in this case?
		mem_copy(p_new, p_old, _min(s_current - 1, s_dest));
		//mem_copy (p_new,p_old,_min(s_current,s_dest));
		mem_free(p_old);
		_ptr = p_new;
	}
	else if (2 == p_mode)
	{
		// relocate into another mmgr(pooled) from real
		void* p_old = P;
		void* p_new = mem_alloc(size
# ifdef DEBUG_MEMORY_NAME
                                , _name
# endif // DEBUG_MEMORY_NAME
		);
		mem_copy(p_new, p_old, (u32)size);
		mem_free(p_old);
		_ptr = p_new;
	}

#ifdef DEBUG_MEMORY_MANAGER
    if (mem_initialized) debug_cs.Leave();

    if (g_globalCheckAddr == _ptr)
        __asm int 3;
#endif // DEBUG_MEMORY_MANAGER

	return _ptr;
}

#endif // __BORLANDC__

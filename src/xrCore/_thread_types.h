#pragma once

#include <tbb/task_group.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>
#include <tbb/task_arena.h>
#include <atomic>
#include <exception>
#include <functional>
#include <utility>
#include <algorithm>
#include <iterator>
#include <type_traits>

// Atomic types
using xr_atomic_u32 = std::atomic_uint32_t;
using xr_atomic_s32 = std::atomic_int;
using xr_atomic_bool = std::atomic_bool;

// Tasks Redefinition
//
// oneTBB's tbb::task_group has a noexcept(false) destructor (it cancels and
// waits on teardown, and can propagate). The PPL task_group it replaced was
// effectively noexcept, and several engine classes holding one override a
// noexcept base destructor (IGame_Level, CRender, CLevel) -- C++ forbids an
// overriding virtual destructor from being less restrictive than its base.
//
// This thin wrapper restores noexcept by draining the group in its own
// destructor and swallowing anything wait() throws. Because the group is
// already drained by then, the member tbb::task_group destructor that runs
// afterwards has nothing left to cancel and will not throw.
//
// Msg() lives in log.h, which xrCore.h includes *after* this header, so it is
// forward-declared here (declaration matches log.h exactly).
void XRCORE_API Msg(const char* format, ...);

class xr_task_group
{
public:
    xr_task_group() = default;

    xr_task_group(const xr_task_group&) = delete;
    xr_task_group& operator=(const xr_task_group&) = delete;

    ~xr_task_group() noexcept
    {
        try
        {
            m_group.wait();
        }
        catch (const std::exception& e)
        {
            Msg("! xr_task_group: exception escaped task_group::wait() during destruction: %s", e.what());
        }
        catch (...)
        {
            Msg("! xr_task_group: unknown exception escaped task_group::wait() during destruction");
        }
    }

    template <typename F>
    void run(F&& Functor)
    {
        m_group.run(std::forward<F>(Functor));
    }

    void wait()
    {
        m_group.wait();
    }

private:
    tbb::task_group m_group;
};

// Monolith: background loading off the frame's threads (oneTBB task arenas).
//
// Every xr_task_group shares the default arena, and a thread waiting on one (the main thread in
// Device's end-of-frame secondary_tasks.wait(), every frame) may run ANY queued task of that
// arena in the meantime, including a texture load queued by the streaming code (DDS decode and
// texture creation, several milliseconds for a large one). That is a frame spike.
//
// xr_background_task_group runs its tasks in a separate, low-priority arena with a few workers of
// its own (xr_background_arena, xrCore.cpp). Threads of the default arena never take tasks from
// another arena, so frame work can no longer pick up a load; the loads still use spare cores,
// and the low priority makes TBB hand idle workers to frame work first.
//   - run(): enqueues; never joins the arena, never blocks (safe from the render thread)
//   - wait(): joins the background arena and helps until the group is done (load screens,
//     device destroy), which is what waiting on loads should do anyway
// Workers: -bg_threads N on the command line (1-16), default a quarter of the hardware threads,
// at least 1 and at most 4 (xrCore.cpp).
// Caveat: a task that itself runs xr_parallel_for inside the background arena gets only that
// arena's workers. The texture loads do not.
XRCORE_API tbb::task_arena& xr_background_arena();

class xr_background_task_group
{
public:
    xr_background_task_group() = default;
    xr_background_task_group(const xr_background_task_group&) = delete;
    xr_background_task_group& operator=(const xr_background_task_group&) = delete;

    ~xr_background_task_group() noexcept
    {
        try
        {
            wait();
        }
        catch (...)
        {
            Msg("! xr_background_task_group: exception escaped wait() during destruction");
        }
    }

    template <typename F>
    void run(F&& Functor)
    {
        xr_background_arena().enqueue(std::forward<F>(Functor), m_group);
    }

    void wait()
    {
        xr_background_arena().wait_for(m_group);
    }

private:
    tbb::task_group m_group;
};

// -tbb_threads N: caps how many threads TBB uses in total (main thread included), to leave cores
// to the renderer's own recording threads (sun cascades, local shadow lanes, rain). Default: TBB's
// own choice (all hardware threads). Called once from xrCore::_initialize, after the file system
// (it logs).
XRCORE_API void xr_tbb_initialize();

template <typename T, typename U>
using xr_concurrent_unordered_map = tbb::concurrent_unordered_map<T, U>;

template <typename T, typename allocator = xalloc<T>>
using xr_concurrent_vector = tbb::concurrent_vector<T, allocator>;

template<typename BlockRangeType, typename Body>
inline void xr_parallel_for(BlockRangeType Begin, BlockRangeType End, Body Functor)
{
    tbb::parallel_for(Begin, End, Functor);
}

template<typename Index, typename Body>
inline void xr_parallel_foreach(Index Begin, Index End, Body Functor)
{
    tbb::parallel_for_each(Begin, End, Functor);
}

// Helper to deduce the value type for the default predicate
template <typename RandomIt>
using IterValueT = typename std::iterator_traits<RandomIt>::value_type;

// Helper to check if an iterator is Random Access
template <typename It>
using IsRandomAccess = std::is_base_of<
    std::random_access_iterator_tag,
    typename std::iterator_traits<It>::iterator_category
>;

template<typename RandomIt, typename P = std::less<IterValueT<RandomIt>>>
IC void xr_parallel_sort(RandomIt first, RandomIt last, P pred = {})
{
    tbb::parallel_sort(first, last, pred);
}

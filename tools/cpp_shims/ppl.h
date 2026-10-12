// check_cpp.py shim for MSVC <ppl.h> (Concurrency Runtime). Not used by the real build.
#pragma once
#include <algorithm>
#include <functional>
namespace concurrency {
class task_group {
public:
	template <class F> void run(const F& f) { f(); }
	template <class F> void run_and_wait(const F& f) { f(); }
	void wait() {}
	void cancel() {}
};
class critical_section { public: void lock() {} void unlock() {} bool try_lock() { return true; } };
template <class I, class F> void parallel_for(I first, I last, const F& f) { for (; first < last; ++first) f(first); }
template <class I, class F> void parallel_for(I first, I last, I step, const F& f) { for (; first < last; first += step) f(first); }
template <class It, class F> void parallel_for_each(It first, It last, const F& f) { std::for_each(first, last, f); }
template <class It> void parallel_sort(It first, It last) { std::sort(first, last); }
template <class It, class P> void parallel_sort(It first, It last, const P& p) { std::sort(first, last, p); }
template <class It> void parallel_buffered_sort(It first, It last) { std::sort(first, last); }
template <class It, class P> void parallel_buffered_sort(It first, It last, const P& p) { std::sort(first, last, p); }
template <class... F> void parallel_invoke(const F&... f) { (f(), ...); }
}
namespace Concurrency = concurrency;

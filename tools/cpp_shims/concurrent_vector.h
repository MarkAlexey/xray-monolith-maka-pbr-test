// check_cpp.py shim for MSVC <concurrent_vector.h>. Not used by the real build.
#pragma once
#include <vector>
namespace concurrency {
template <class T, class A = std::allocator<T>>
class concurrent_vector : public std::vector<T> {
public:
	using std::vector<T>::vector;
	typename std::vector<T>::iterator grow_by(size_t n) { size_t o = this->size(); this->resize(o + n); return this->begin() + o; }
};
}

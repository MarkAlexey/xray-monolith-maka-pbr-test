// check_cpp.py shim for MSVC <concurrent_unordered_map.h>. Not used by the real build.
#pragma once
#include <unordered_map>
namespace concurrency {
template <class K, class V, class H = std::hash<K>, class E = std::equal_to<K>, class A = std::allocator<std::pair<const K, V>>>
class concurrent_unordered_map : public std::unordered_map<K, V, H, E> {
public:
	using std::unordered_map<K, V, H, E>::unordered_map;
	template <class It> void unsafe_erase(It it) { this->erase(it); }
	void unsafe_erase(const K& k) { this->erase(k); }
};
}

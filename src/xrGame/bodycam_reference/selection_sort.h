#pragma once
#include <cstddef>
#include <utility>
#include <vector>

namespace BodycamReference
{
// Orders items by descending weight the way the source's small inline arrays do: the lightest remaining
// item is moved to the back, earliest first among equals. Equal weights do not keep their order.
template <class T, class Weight>
void SortByDescendingWeight(std::vector<T>& items, const Weight& weight)
{
	for (std::size_t end = items.size(); end > 1; --end)
	{
		std::size_t least = 0;
		for (std::size_t i = 1; i < end; ++i)
			if (weight(items[i]) < weight(items[least]))
				least = i;
		std::swap(items[least], items[end - 1]);
	}
}
}

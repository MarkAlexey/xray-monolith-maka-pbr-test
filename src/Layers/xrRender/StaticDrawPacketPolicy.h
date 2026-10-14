#pragma once
#include <cstdint>

namespace aoe_render
{
// A shared immutable index buffer contains each member consecutively. Missing
// or incompatible members leave a gap: never bridge it by drawing hidden items.
inline bool adjacent_static_range(std::uint32_t first, std::uint32_t count, std::uint32_t next)
{
    return count <= UINT32_MAX - first && first + count == next;
}

inline bool static_vertex_span(std::uint32_t base, std::uint32_t end)
{
    return end >= base && end - base <= 65536;
}
}

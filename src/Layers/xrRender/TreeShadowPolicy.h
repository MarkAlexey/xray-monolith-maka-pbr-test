#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace aoe_render
{
using TreeShadowVertex = std::array<std::uint8_t, 20>; // FLOAT3 position + SHORT4 UV, exact bytes.

// Load-only. Preserve triangle order and every shader-visible input, including
// signed zero/NaN payloads and UV padding. Never weld by a geometric tolerance.
inline bool tree_shadow_indices(const TreeShadowVertex* vertices, std::uint32_t vertex_count,
    const std::uint16_t* indices, std::uint32_t count, bool uv,
    std::vector<std::uint16_t>& output, std::uint32_t& unique)
{
    output.clear(); unique = 0;
    if (!vertices || !indices || !vertex_count || vertex_count > 65536 || !count || count % 3) return false;
    std::map<TreeShadowVertex, std::uint16_t> first;
    std::vector<std::uint32_t> remap(vertex_count, UINT32_MAX);
    output.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto index = indices[i];
        if (index >= vertex_count) { output.clear(); return false; }
        if (remap[index] == UINT32_MAX)
        {
            auto key = vertices[index];
            if (!uv) std::memset(key.data() + 12, 0, 8);
            remap[index] = first.emplace(key, index).first->second;
        }
        output.push_back(static_cast<std::uint16_t>(remap[index]));
    }
    unique = static_cast<std::uint32_t>(first.size());
    return true;
}
}

#pragma once
#include <cstdint>
#include <limits>

namespace aoe_render
{
constexpr std::uint32_t shadow_batch_members = 32;
constexpr std::uint32_t shadow_batch_indices = 65536;
constexpr std::uint32_t shadow_batch_bytes = 64u * 1024u * 1024u;

inline bool rebase_shadow_index(std::uint32_t index, std::uint32_t source_base,
    std::uint32_t source_vertices, std::uint32_t batch_base, std::uint16_t& output)
{
    if (index >= source_vertices || source_base < batch_base || index > 65535u ||
        source_base - batch_base > 65535u - index) return false;
    output = std::uint16_t(source_base - batch_base + index);
    return true;
}

// Owner-only, one material-list invocation. No stale visibility across views,
// duplicate members or partial groups may become a combined draw.
struct ShadowBatchSelection
{
    std::uint32_t frame = UINT32_MAX, ticket = 0, mask = 0;
    bool duplicate = false, emitted = false;
    bool begin(std::uint32_t next_frame, std::uint32_t next_ticket)
    {
        if (frame == next_frame && ticket == next_ticket) return false;
        frame = next_frame; ticket = next_ticket; mask = 0; duplicate = emitted = false;
        return true;
    }
    void add(std::uint32_t member, std::uint32_t count)
    {
        if (member >= count || count > shadow_batch_members) { duplicate = true; return; }
        const std::uint32_t bit = std::uint32_t(1) << member;
        duplicate |= (mask & bit) != 0;
        mask |= bit;
    }
    bool complete(std::uint32_t count) const
    {
        return count >= 2 && count <= shadow_batch_members && !duplicate &&
            mask == (count == 32 ? UINT32_MAX : (std::uint32_t(1) << count) - 1);
    }
};
}

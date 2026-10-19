#pragma once
#include <cstdint>
#include <vector>

// OGSR smapvis: test one static caster at a time, never wait for the GPU.
// The retained MT queue keeps stable indices instead of graph feedback markers.
struct LocalShadowVisibility
{
    static constexpr std::uint32_t none = UINT32_MAX;
    std::vector<std::uint8_t> hidden;
    std::uint32_t next = 0, pending = none;
    bool failed = false;

    void Reset(std::uint32_t count)
    {
        hidden.assign(count, 0);
        next = 0;
        pending = none;
        failed = false;
    }

    std::uint32_t Select() const
    {
        return !failed && pending == none && next < hidden.size() ? next : none;
    }

    void Resolve(bool ready, bool valid, std::uint64_t samples)
    {
        if (pending == none || !ready)
            return;
        if (!valid)
        {
            // An API error is never evidence of invisibility.
            Reset(static_cast<std::uint32_t>(hidden.size()));
            failed = true;
            return;
        }
        hidden[pending] = samples == 0;
        next = pending + 1;
        pending = none;
    }
};

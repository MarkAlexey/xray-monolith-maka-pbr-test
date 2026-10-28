#pragma once
#include <cstddef>
#include <cstring>

namespace SvpReferenceMaterials
{
// The texture manager queues raw pointers for loading. When a texture changes,
// drain waits for the queue while cache and candidate still hold both sets.
template<class Material, std::size_t Count, std::size_t NameSize, class Create, class Drain>
bool Acquire(Material (&cache)[Count], Material (&candidate)[Count],
    const char (&names)[Count][NameSize], Create create, Drain drain)
{
    for (std::size_t i = 0; i < Count; ++i)
        if (!names[i][0] || !std::memchr(names[i], 0, NameSize)) return false;
    bool changed = false;
    for (std::size_t i = 0; i < Count; ++i)
    {
        candidate[i] = cache[i];
        create(candidate[i], names[i]);
        changed = changed || candidate[i] != cache[i];
    }
    if (changed) drain();
    for (std::size_t i = 0; i < Count; ++i) cache[i] = candidate[i];
    return true;
}

// The surface a material slot was last found usable with. The slot holds a
// reference to it, so its address cannot pass to another surface, and a
// reloaded material is checked again.
template<class Surface>
class QualifiedSurface
{
public:
    QualifiedSurface() = default;
    QualifiedSurface(const QualifiedSurface&) = delete;
    QualifiedSurface& operator=(const QualifiedSurface&) = delete;
    ~QualifiedSurface() { if (surface_) surface_->Release(); }
    // Takes over the caller's reference to surface; usable runs only for a surface the slot does not hold.
    template<class Usable>
    bool Qualify(Surface* surface, Usable usable)
    {
        if (surface && surface == surface_)
        {
            surface->Release();
            return true;
        }
        if (surface_) surface_->Release();
        surface_ = nullptr;
        if (!surface) return false;
        if (!usable(surface))
        {
            surface->Release();
            return false;
        }
        surface_ = surface;
        return true;
    }

private:
    Surface* surface_ = nullptr;
};
}

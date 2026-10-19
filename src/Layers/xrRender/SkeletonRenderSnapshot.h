#pragma once
#include <memory>
#include <vector>

// Immutable render inputs. Gameplay bones/callbacks retain their normal owner
// and frequency; command recording never holds the parent mutex over API draws.
struct SkeletonRenderSnapshot
{
    u32 frame = 0;
    u16 mode = 0;
    Fmatrix single, previous_world;
    std::vector<Fvector4> rows;
    std::vector<Fmatrix> soft_transforms;
};

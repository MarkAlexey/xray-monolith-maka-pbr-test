#pragma once
#include <cmath>
namespace SvpReferenceCapture
{
// Screen height divided by the on-screen height of the round aperture. The lens
// is drawn with the HUD projection, so its _22 term is used. Returns 1 when
// the lens is not drawn this frame or the inputs are unusable.
inline float CurrentFit(float radius, float depth, float hudProjection22, bool fresh)
{
    if (!fresh || !std::isfinite(radius) || !std::isfinite(depth)
        || !std::isfinite(hudProjection22) || radius <= 0.00001f
        || depth <= 0.00001f || hudProjection22 <= 0.f)
        return 1.f;
    const float ndcHeight = 2.f * radius * hudProjection22 / depth;
    if (!std::isfinite(ndcHeight) || ndcHeight <= 0.0001f)
        return 1.f;
    return 2.f / ndcHeight;
}
}

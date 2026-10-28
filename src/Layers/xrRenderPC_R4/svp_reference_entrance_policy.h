#pragma once
#include <cmath>
namespace SvpReferenceCapture
{
// The inferred clip-on front can be weapon geometry such as a front sight, so
// Reference effects keep the registered objective.
inline bool UseInferredClipOn(bool reference, bool enabled, float inferredFront, float objectiveFront)
{
    return !reference && enabled && std::isfinite(inferredFront)
        && inferredFront > objectiveFront;
}
}

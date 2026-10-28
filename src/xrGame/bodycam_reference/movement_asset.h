#pragma once
#include "movement.h"
#include "recoil_curve.h"
#include <array>
#include <cstdint>
namespace BodycamReference
{
// The sampler owns its curve data and outlives the package bytes.
MovementCurveSampler LoadMovementSampler(const std::vector<std::uint8_t>& bytes);
}

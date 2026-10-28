#pragma once
#include "engine_basis.h"
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
// cam_Prepare has already limited torso roll against camera collision, and actor.floorClearance
// carries the vertical part of the lean arc. This adds the lateral displacement and camera roll.
inline EngineRowMatrix ApplyLean(EngineRowMatrix camera, float torsoRoll, float worldX, float worldZ)
{
	if (!std::isfinite(torsoRoll) || !std::isfinite(worldX) || !std::isfinite(worldZ))
		throw std::invalid_argument("Lean roll or displacement is not finite");
	const float angle = -torsoRoll * .25f;
	const float c = std::cos(angle), s = std::sin(angle);
	const auto original = camera;
	for (unsigned i = 0; i < 3; ++i)
	{
		camera[i] = original[i] * c + original[4 + i] * s;
		camera[4 + i] = original[4 + i] * c - original[i] * s;
	}
	camera[12] += worldX;
	camera[14] += worldZ;
	return camera;
}
}

#pragma once
#include "skeleton_pose.h"

namespace BodycamReference
{
struct SpineChain
{
	std::array<unsigned, 3> spine;
	unsigned neck, head;
};
struct ProceduralGates
{
	float ikOffsetAlpha, yawAlpha, pitchAlpha;
};
ProceduralGates MovementProceduralGates(double disableIkOffset, double enableSpineRotation);
void ApplySpineAimOffset(
	SkeletonPose&, const SpineChain&, double pitch, double yaw, double leanAlpha, double yawAlpha = 1, double pitchAlpha = 1);
}

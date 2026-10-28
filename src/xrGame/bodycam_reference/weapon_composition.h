#pragma once
#include "skeleton_pose.h"

namespace BodycamReference
{
// All transforms are in the Reference model space, in metres. Multiply(A, B) applies B, then A.
struct WeaponCompositionInput
{
	Matrix baselineWeapon; // engine hip or ADS anchor * authored item offset
	Matrix proceduralWeapon; // target from the procedural graph, in that same space
	Matrix authoredRightHand; // current animation, including detached hands
	Matrix authoredLeftFromRight; // current animated left hand relative to the right
};
struct WeaponTargets
{
	Matrix modelMotion;
	Matrix rightHand;
	Matrix leftHand;
};
inline WeaponTargets ComposeWeaponTargets(const WeaponCompositionInput& input)
{
	// The delta is in model space, independent of the wrist orientation, so reload and bolt hand
	// motion do not move the gun.
	const auto motion = Multiply(input.proceduralWeapon, InverseAffine(input.baselineWeapon));
	const auto right = Multiply(motion, input.authoredRightHand);
	return { motion, right, Multiply(right, input.authoredLeftFromRight) };
}
}

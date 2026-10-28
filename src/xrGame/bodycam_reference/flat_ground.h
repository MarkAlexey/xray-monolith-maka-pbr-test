#pragma once
#include "skeleton_pose.h"
#include <vector>

namespace BodycamReference
{
struct FlatFloorAdjustment
{
	float distance, move;
};
FlatFloorAdjustment AdjustFlatFloorDistance(double distanceCm);
struct FlatGroundState
{
	std::array<Vector, 2> targets{};
	Vector offset{}, target{};
	std::array<float, 2> enables{};
	double alpha = 0;
	bool hit = false;
};
class FlatGroundPelvis
{
public:
	const FlatGroundState& Update(
		double dt, double enableLeft, double enableRight, double rootWorldZ, double floorZ = 0, bool walkable = true);
	const FlatGroundState& State() const { return state_; }

private:
	FlatGroundState state_;
};
// Raises or lowers the pelvis by offsetCm; assumes a flat floor and a yaw-only mesh rotation.
Matrix ApplyFlatPelvisControl(
	SkeletonPose&, unsigned pelvis, unsigned head, double offsetCm, double alpha, const std::vector<Matrix*>& flattened);
}

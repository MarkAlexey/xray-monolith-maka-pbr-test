#pragma once
#include "packed_pose_blend.h"
#include "compressed_curves.h"
#include "directional_cache.h"
#include "locomotion_machine.h"
namespace BodycamReference
{
struct PoseCurveBundle
{
	PackedPose pose;
	NamedCurves curves;
};
struct TwoWayPoseState
{
	float alpha;
	std::array<bool, 2> relevant;
};
using PoseReadCache = std::function<PoseCurveBundle(unsigned, const std::function<PoseCurveBundle()>&)>;
NamedCurves TwoWayCurves(const NamedCurves&, const NamedCurves&, float alpha);
NamedCurves MultiWayCurves(const std::vector<WeightedNamedCurves>&);
NamedCurves TransitionCurves(const NamedCurves&, const NamedCurves&, float alpha);
PoseCurveBundle EvaluateDirectionalInput(unsigned state, const DirectionEvaluationSnapshot&, TwoWayPoseState sprint,
	const std::function<PoseCurveBundle(unsigned)>& player, const PoseReadCache&, const std::function<PoseCurveBundle()>& reference);
struct TransitionPoseProfile
{
	std::vector<int> indices;
	std::vector<TransitionWeights> weights;
};
PoseCurveBundle EvaluateStatePoseStack(unsigned stateCount, unsigned current, const std::vector<LocomotionTransitionRecord>&,
	const std::function<PoseCurveBundle(unsigned)>& source,
	const std::function<TransitionPoseProfile(const LocomotionTransitionRecord&, unsigned, std::size_t)>& profile);
}

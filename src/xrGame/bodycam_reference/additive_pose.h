#pragma once
#include "directional_pose_evaluate.h"
namespace BodycamReference
{
PackedTransform ApplyLocalAdditive(const PackedTransform& base, const PackedTransform& delta, float alpha);
NamedCurves AdditiveCurves(const NamedCurves& base, const NamedCurves& delta, float alpha);
PoseCurveBundle EvaluateLocalAdditive(
	float alpha, const std::function<PoseCurveBundle()>& base, const std::function<PoseCurveBundle()>& additive);
PoseCurveBundle EvaluateDetailState(unsigned state, float alpha, std::array<float, 4> weights,
	const std::function<PoseCurveBundle()>& cycles, const std::function<PoseCurveBundle(unsigned)>& player,
	const std::function<PoseCurveBundle()>& reference);
}

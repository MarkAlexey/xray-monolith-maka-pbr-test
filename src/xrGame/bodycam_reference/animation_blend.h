#pragma once
#include "transform.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace BodycamReference
{
struct AdditivePose
{
	Vector position{}, scaleDelta{};
	Quaternion rotation{ 0, 0, 0, 1 };
};
using AnimationCurves = std::map<std::string, float, std::less<>>;
// The curve called name; 0 when it is absent.
inline float CurveValue(const AnimationCurves& curves, std::string_view name)
{
	const auto found = curves.find(name);
	return found == curves.end() ? 0 : found->second;
}
LocalPose BlendAnimationLocal(const LocalPose& from, const LocalPose& to, double alpha);
AnimationCurves BlendAnimationCurves(const AnimationCurves& from, const AnimationCurves& to, double alpha);
// Working buffers of AccumulateMeshSpacePose, kept by a caller that runs it every frame.
struct MeshSpaceScratch
{
	std::vector<Quaternion> current, components;
};
// Writes into result, which must not be base.
void AccumulateMeshSpacePose(const std::vector<LocalPose>& base, const std::vector<int>& parents, const std::vector<AdditivePose>& deltas,
	double weight, MeshSpaceScratch& scratch, std::vector<LocalPose>& result);
std::vector<LocalPose> AccumulateMeshSpacePose(
	const std::vector<LocalPose>& base, const std::vector<int>& parents, const std::vector<AdditivePose>& deltas, double weight);
}

#include "spine_pose.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
ProceduralGates MovementProceduralGates(double disable, double spine)
{
	return { float(1. - float(disable)), float(spine), float(spine) };
}
namespace
{
void AddComponentRotation(SkeletonPose& skeleton, unsigned bone, const Vector& axis, double degrees, double alpha = 1)
{
	const float weight = float(std::clamp(alpha, 0., 1.));
	if (std::abs(degrees) < 1e-7 || weight <= kBlendEpsilon)
		return;
	const auto original = skeleton.Local(bone);
	auto component = Decompose(skeleton.Component(bone));
	const auto parent = Decompose(skeleton.ParentComponent(bone));
	const double half = -(degrees * kRadiansPerDegree) * .5, sine = std::sin(half);
	const Quaternion delta{ axis[0] * sine, axis[1] * sine, axis[2] * sine, std::cos(half) };
	component.rotation = NormalizeQuaternion(QuaternionProduct(delta, component.rotation));
	auto target = original;
	target.rotation = NormalizeQuaternion(QuaternionProduct(Conjugate(parent.rotation), component.rotation));
	skeleton.SetLocal(bone, weight < kBlendFull ? BlendSkeletalControl(original, target, weight) : target);
}
}
void ApplySpineAimOffset(
	SkeletonPose& skeleton, const SpineChain& chain, double pitch, double yaw, double leanAlpha, double yawAlpha, double pitchAlpha)
{
	// Lean and yaw are spread over the three spine bones, pitch over the spine, neck and head.
	const double lean = leanAlpha * 2;
	for (unsigned bone : chain.spine)
		AddComponentRotation(skeleton, bone, { 0, 0, 1 }, -lean / 3);
	for (unsigned bone : chain.spine)
		AddComponentRotation(skeleton, bone, { 0, 1, 0 }, yaw / 3, yawAlpha);
	const double radians = yaw * kRadiansPerDegree;
	Vector axis{ std::cos(radians), 0, std::sin(radians) };
	const double length = std::sqrt(axis[0] * axis[0] + axis[2] * axis[2]);
	for (auto& v : axis)
		v /= length;
	for (unsigned bone : chain.spine)
		AddComponentRotation(skeleton, bone, axis, pitch / 5, pitchAlpha);
	AddComponentRotation(skeleton, chain.neck, axis, pitch / 5, pitchAlpha);
	AddComponentRotation(skeleton, chain.head, axis, pitch / 5, pitchAlpha);
}
}

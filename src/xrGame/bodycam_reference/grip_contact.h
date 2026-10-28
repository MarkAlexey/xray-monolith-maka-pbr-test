#pragma once
#include "skeleton_pose.h"
#include "animation_blend.h"
#include "viewmodel_helpers.h"
#include <algorithm>
#include <cmath>
namespace BodycamReference
{
// Arm IK onto the gun with body and gun motion applied separately: the authored root moves with
// the body, the hand target with the gun. seed is working storage with the topology of authored.
// Returns how far the shoulder had to move.
inline double SolveReferenceContact(std::vector<Matrix>& components, SkeletonPose& seed, const SkeletonPose& authored,
	const ArmChain& chain, const Matrix& engineRoot, const Matrix& bodyMotion, const Matrix& weaponMotion, double influence = 1,
	const Vector& elbowCm = Vector{})
{
	const auto fade = [influence](const Matrix& m)
	{ return influence >= 1 ? m : Compose(BlendAnimationLocal(LocalPose{}, Decompose(m), std::max(influence, 0.))); };
	const auto body = Multiply(fade(bodyMotion), engineRoot), gun = Multiply(fade(weaponMotion), engineRoot);
	// Carry the authored hierarchy into the body frame.
	for (unsigned i = 0; i < authored.Size(); ++i)
		seed.SetLocal(i, authored.Parent(i) < 0 ? Decompose(Multiply(body, Compose(authored.Local(i)))) : authored.Local(i));
	const auto target = Multiply(gun, authored.Component(chain.hand));
	const auto difference = [](const Vector& a, const Vector& b) { return Vector{ a[0] - b[0], a[1] - b[1], a[2] - b[2] }; };
	const auto length = [](const Vector& a) { return std::hypot(a[0], a[1], a[2]); };
	const auto shoulder = Translation(seed.Component(chain.upper)), elbow = Translation(seed.Component(chain.lower)),
			   hand = Translation(seed.Component(chain.hand));
	const double upper = length(difference(elbow, shoulder)), lower = length(difference(hand, elbow));
	auto direction = difference(Translation(target), shoulder);
	const double distance = length(direction);
	const double minimum = std::abs(upper - lower) + 1e-7, maximum = upper + lower - 1e-7;
	const double accommodation = distance - std::clamp(distance, minimum, maximum);
	// Move the shoulder only when the target is out of reach; the segments keep their lengths.
	if (std::abs(accommodation) > 0)
	{
		if (distance > 1e-9)
		{
			for (auto& v : direction)
				v /= distance;
		}
		else
		{
			direction = difference(hand, shoulder);
			const double size = length(direction);
			if (size > 1e-9)
			{
				for (auto& v : direction)
					v /= size;
			}
			else
			{
				direction = { 0, 0, 1 };
			}
		}
		for (unsigned i = 0; i < seed.Size(); ++i)
		{
			if (seed.Parent(i) >= 0)
				continue;
			auto root = seed.Local(i);
			for (unsigned k = 0; k < 3; ++k)
				root.position[k] += direction[k] * accommodation;
			seed.SetLocal(i, root);
		}
	}
	auto pole = CopySwayPole(Multiply(gun, authored.Component(chain.lower)), seed.Component(chain.lower), target);
	LocalPose offset;
	offset.position = { elbowCm[0] * .01 * influence, elbowCm[2] * .01 * influence, elbowCm[1] * .01 * influence };
	pole = Multiply(pole, Compose(offset));
	ApplyArmIK(seed, chain, target, pole);
	// Convert the whole target into parent space so inherited non-uniform scale is kept.
	auto wrist = Decompose(Multiply(InverseAffine(seed.ParentComponent(chain.hand)), target));
	wrist.position = seed.Local(chain.hand).position;
	seed.SetLocal(chain.hand, wrist);
	components.resize(seed.Size());
	for (unsigned i = 0; i < seed.Size(); ++i)
		components[i] = seed.Component(i);
	return std::abs(accommodation);
}

}

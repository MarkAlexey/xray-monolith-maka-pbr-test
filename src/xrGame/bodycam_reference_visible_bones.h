#pragma once
#include "bodycam_reference/engine_basis.h"
#include "bodycam_reference/skeleton_pose.h"
#include <functional>
#include <stdexcept>
#include <string>

namespace BodycamReference
{
// Bone visibility is a render contract: hidden descendants can contain
// deliberately singular matrices. They are outside this adapter's pose domain.
struct VisibleBoneProjection
{
	std::vector<unsigned> poseToEngine, engineToPose;
	std::vector<int> parents;
};
inline VisibleBoneProjection ProjectVisibleBones(const std::vector<int>& engineParents, const std::vector<unsigned char>& visible)
{
	const unsigned count = unsigned(engineParents.size());
	if (!count || visible.size() != count)
		throw std::invalid_argument("Bone visibility/topology size mismatch");
	VisibleBoneProjection result;
	result.engineToPose.assign(count, ~0u);
	std::vector<unsigned char> visited(count);
	std::function<void(unsigned)> visit = [&](unsigned bone)
	{
		if (visited[bone] == 2)
			return;
		if (visited[bone] == 1)
			throw std::invalid_argument("Cyclic skeleton at bone " + std::to_string(bone));
		visited[bone] = 1;
		const int parent = engineParents[bone];
		if (parent < -1 || parent >= int(count))
			throw std::invalid_argument("Invalid parent at bone " + std::to_string(bone));
		if (parent >= 0)
			visit(unsigned(parent));
		if (visible[bone])
		{
			if (parent >= 0 && !visible[parent])
				throw std::invalid_argument("Visible bone has hidden parent: " + std::to_string(bone));
			result.engineToPose[bone] = unsigned(result.poseToEngine.size());
			result.poseToEngine.push_back(bone);
			result.parents.push_back(parent < 0 ? -1 : int(result.engineToPose[parent]));
		}
		visited[bone] = 2;
	};
	for (unsigned bone = 0; bone < count; ++bone)
		visit(bone);
	if (result.parents.empty())
		throw std::invalid_argument("Skeleton has no visible bones");
	return result;
}
template <class ReadMatrix, class BoneName>
std::vector<LocalPose> CaptureVisiblePose(
	const std::vector<unsigned>& poseToEngine, const std::vector<int>& parents, const EngineBasis& basis, ReadMatrix read, BoneName name)
{
	std::vector<LocalPose> locals;
	locals.reserve(parents.size());
	std::vector<Matrix> components;
	components.reserve(parents.size());
	for (unsigned i = 0; i < parents.size(); ++i)
	{
		const unsigned id = poseToEngine[i];
		try
		{
			components.push_back(basis.ToReference(read(id)));
			const auto local = parents[i] < 0 ? components.back() : Multiply(InverseAffine(components[parents[i]]), components.back());
			locals.push_back(Decompose(local));
		}
		catch (const std::exception& e)
		{
			throw std::invalid_argument("Skeleton capture bone " + std::to_string(id) + " [" + name(id) + "]: " + e.what());
		}
	}
	return locals;
}
}

#include "../xrEngine/stdafx.h"
#include "bodycam_reference_skeleton.h"
#include "bodycam_reference_visible_bones.h"
#include "bodycam_math.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "../Layers/xrRender/Animation.h"
#include "../Layers/xrRender/AnimationKeyCalculate.h"
#include "../xrEngine/bone.h"
#include <stdexcept>
#include <utility>
#include <cmath>

namespace BodycamReference
{
EngineSkeletonAdapter::EngineSkeletonAdapter(IKinematics& model, EngineBasis basis) : model_(model), basis_(std::move(basis))
{
	const unsigned count = model_.LL_BoneCount();
	if (!count)
		throw std::invalid_argument("Reference skeleton has no bones");
	std::vector<int> engineParents;
	engineParents.reserve(count);
	visibility_.reserve(count);
	for (unsigned bone = 0; bone < count; ++bone)
	{
		const auto parent = model_.LL_GetData(u16(bone)).GetParentID();
		engineParents.push_back(parent == BI_NONE ? -1 : int(parent));
		visibility_.push_back(model_.LL_GetBoneVisible(u16(bone)) ? 1 : 0);
	}
	auto projection = ProjectVisibleBones(engineParents, visibility_);
	engineToPose_ = std::move(projection.engineToPose);
	poseToEngine_ = std::move(projection.poseToEngine);
	parents_ = std::move(projection.parents);
}
std::vector<LocalPose> EngineSkeletonAdapter::Capture() const
{
	xrCriticalSectionGuard lock(&model_.UCalc_Mutex);
	if (model_.LL_BoneCount() != visibility_.size())
		throw std::invalid_argument("Skeleton capture: bone count changed; rebind required");
	for (unsigned id = 0; id < visibility_.size(); ++id)
		if ((model_.LL_GetBoneVisible(u16(id)) ? 1 : 0) != visibility_[id])
			throw std::invalid_argument("Skeleton capture: visibility changed at bone " + std::to_string(id) + " [" +
				model_.LL_GetData(u16(id)).name.c_str() + "]; rebind required");
	return CaptureVisiblePose(
		poseToEngine_, parents_, basis_, [&](unsigned id) { return Bodycam::ToRows(model_.LL_GetTransform(u16(id))); },
		[&](unsigned id) { return std::string(model_.LL_GetData(u16(id)).name.c_str()); });
}
SkeletonPose EngineSkeletonAdapter::SampleAuthoredPose(const char* motionName, float seconds) const
{
	auto* animated = model_.dcast_PKinematicsAnimated();
	if (!animated || !motionName || !*motionName)
		throw std::invalid_argument("Named authored calibration motion required");
	const MotionID motion = animated->ID_Cycle_Safe(motionName);
	if (!motion.valid())
		throw std::invalid_argument(std::string("Missing calibration motion: ") + motionName);
	if (seconds < 0 || seconds > animated->get_animation_length(motion))
		throw std::invalid_argument("Valid authored calibration motion/time required");
	xrCriticalSectionGuard lock(&model_.UCalc_Mutex);
	CBlend sample{};
	sample.timeCurrent = seconds;
	std::vector<LocalPose> locals;
	locals.reserve(parents_.size());
	for (unsigned engineBone : poseToEngine_)
	{
		const auto* channel = animated->LL_GetMotion(motion, u16(engineBone));
		if (!channel || !channel->get_count())
			throw std::invalid_argument("Incomplete authored calibration clip");
		CKey key;
		Dequantize(key, sample, *channel);
		Fmatrix local;
		local.mk_xform(key.Q, key.T);
		locals.push_back(Decompose(basis_.ToReference(Bodycam::ToRows(local))));
	}
	return SkeletonPose(parents_, std::move(locals));
}
void EngineSkeletonAdapter::EngineTransforms(const std::vector<Matrix>& components, std::vector<EngineRowMatrix>& transforms) const
{
	if (components.size() != parents_.size())
		throw std::invalid_argument("Engine component binding size mismatch");
	transforms.clear();
	for (const auto& component : components)
	{
		transforms.push_back(basis_.ToEngine(component));
		for (float v : transforms.back())
			if (!std::isfinite(v))
				throw std::invalid_argument("Nonfinite bone transform");
	}
}
void EngineSkeletonAdapter::Publish(const std::vector<EngineRowMatrix>& transforms)
{
	xrCriticalSectionGuard lock(&model_.UCalc_Mutex);
	// Hidden bones are not part of the pose and keep the engine's matrices.
	for (unsigned i = 0; i < parents_.size(); ++i)
	{
		const u16 id = u16(poseToEngine_[i]);
		auto& bone = model_.LL_GetBoneInstance(id);
		bone.mTransform = Bodycam::FromRows(transforms[i]);
		bone.mRenderTransform.mul_43(bone.mTransform, model_.LL_GetData(id).m2b_transform);
	}
}
std::optional<unsigned> EngineSkeletonAdapter::FindBone(const char* name) const
{
	const u16 bone = model_.LL_BoneID(name);
	if (bone == BI_NONE || engineToPose_[bone] == ~0u)
		return {};
	return engineToPose_[bone];
}
}

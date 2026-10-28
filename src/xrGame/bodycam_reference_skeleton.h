#pragma once
#include "bodycam_reference/engine_basis.h"
#include "bodycam_reference/skeleton_pose.h"
#include <optional>

class IKinematics;
namespace BodycamReference
{
// The visible bones of one engine model as a Reference skeleton. The model
// belongs to player_hud; build a new adapter when its visual changes.
class EngineSkeletonAdapter
{
public:
	EngineSkeletonAdapter(IKinematics&, EngineBasis);
	EngineSkeletonAdapter(const EngineSkeletonAdapter&) = delete;
	EngineSkeletonAdapter& operator=(const EngineSkeletonAdapter&) = delete;
	// The local poses the engine calculated this frame. Throws when a bone has
	// been shown or hidden since construction.
	std::vector<LocalPose> Capture() const;
	// One frame of a motion, read from its keys without playing it.
	SkeletonPose SampleAuthoredPose(const char* motionName, float seconds = 0) const;
	// Model-space bone matrices in Reference coordinates, as engine bone transforms.
	void EngineTransforms(const std::vector<Matrix>& components, std::vector<EngineRowMatrix>& transforms) const;
	// Call before the HUD renders.
	void Publish(const std::vector<EngineRowMatrix>& transforms);
	std::optional<unsigned> FindBone(const char* name) const;

private:
	IKinematics& model_;
	EngineBasis basis_;
	std::vector<unsigned char> visibility_;
	std::vector<unsigned> poseToEngine_, engineToPose_;
	std::vector<int> parents_;
};
}

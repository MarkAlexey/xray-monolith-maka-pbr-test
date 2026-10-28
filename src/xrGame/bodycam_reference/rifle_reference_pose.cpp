#include "rifle_reference_pose.h"
#include "asset_reader.h"
#include "viewmodel_pose.h"
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
// The camera socket relative to the head bone, in metres.
constexpr Vector kCameraSocket{ .10471164, 2.4425174e-9, .15560489 };
Matrix CameraPoint(const Matrix& head)
{
	LocalPose socket;
	socket.position = kCameraSocket;
	const auto matrix = Multiply(head, Compose(socket));
	LocalPose point;
	point.position = Translation(matrix);
	return Compose(point);
}
}
RifleReferencePose::RifleReferencePose(std::vector<RifleTrack> tracks, std::vector<LocalPose> cclamp, std::vector<LocalPose> aim)
	: tracks_(std::move(tracks)), cclamp_(std::move(cclamp)), aim_(std::move(aim))
{
	if (tracks_.size() != kPhysicalBoneCount || cclamp_.size() != kPhysicalBoneCount || aim_.size() != kPhysicalBoneCount)
		throw std::invalid_argument("Reference pose needs 68 tracks, C-clamp bones and aim bones");
	std::map<std::string, unsigned> names;
	for (unsigned i = 0; i < tracks_.size(); ++i)
	{
		parents_.push_back(tracks_[i].parent);
		if (!names.emplace(LowerAscii(tracks_[i].name), i).second)
			throw std::invalid_argument("Duplicate reference-pose bone");
		cclamp_[i].rotation = NormalizeQuaternion(cclamp_[i].rotation);
		aim_[i].rotation = NormalizeQuaternion(aim_[i].rotation);
	}
	gun_ = names.at("ik_hand_gun");
	pole_ = names.at("lowerarm_l");
	head_ = names.at("head");
	left_[0] = names.at("hand_l");
	unsigned next = 1;
	for (const auto* prefix : { "thumb", "ring", "pinky", "middle", "index" })
		for (unsigned i = 1; i <= 3; ++i)
			left_[next++] = names.at(std::string(prefix) + "_0" + std::to_string(i) + "_l");
	const auto reference = ComponentMatrices(cclamp_, parents_);
	referenceAppliedGun_ = ApplyComponentAdditive(reference[gun_], M4PoseOffset());
	referenceCamera_ = CameraPoint(reference[head_]);
}
LeftHandTargets RifleReferencePose::EvaluateLeft(double alpha) const
{
	std::vector<LocalPose> blended;
	blended.reserve(tracks_.size());
	for (unsigned i = 0; i < tracks_.size(); ++i)
		blended.push_back(BlendAnimationLocal(cclamp_[i], aim_[i], alpha));
	const auto components = ComponentMatrices(blended, parents_);
	return { components[gun_], components[left_[0]], components[pole_] };
}
void RifleReferencePose::ApplyPhysicalLeft(std::vector<LocalPose>& pose, double alpha, double attach) const
{
	if (!std::isfinite(attach) || attach < 0 || attach > 1)
		throw std::invalid_argument("Left-hand attachment weight must be in [0, 1]");
	for (unsigned bone : left_)
	{
		const auto target = BlendAnimationLocal(cclamp_[bone], aim_[bone], alpha);
		auto current = pose[bone];
		current.rotation = NormalizeQuaternion(current.rotation);
		pose[bone].rotation = BlendAnimationLocal(current, target, attach).rotation;
		for (unsigned i = 0; i < 3; ++i)
		{
			pose[bone].position[i] += (target.position[i] - pose[bone].position[i]) * attach;
			pose[bone].scale[i] += (target.scale[i] - pose[bone].scale[i]) * attach;
		}
	}
}
Matrix RifleReferencePose::CurrentCameraPoint(const std::vector<LocalPose>& physical) const
{
	return CameraPoint(ComponentMatrices(physical, parents_)[head_]);
}
}

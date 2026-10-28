#pragma once
#include "packed_pose_blend.h"
#include "locomotion_marker_tick.h"
#include <variant>
namespace BodycamReference
{
class PoseAssetBank
{
public:
	static PoseAssetBank Load(const std::vector<std::uint8_t>& bytes);
	std::vector<SourceBoneTransform> Sample(const std::string& identity, double seconds) const;
	PackedPose Evaluate(const std::vector<BlendMarkerSample>& samples) const;
	const std::vector<SourceBoneTransform>& InitialPose() const { return initial_; }

private:
	PoseAssetBank() = default;
	struct Entry
	{
		double length;
		std::variant<PerTrackPose, std::vector<SourceBoneTransform>> data;
	};
	std::vector<SourceBoneTransform> Sample(const Entry&, double seconds) const;
	std::optional<RsqrtSeedTable> rsqrt_;
	std::vector<SourceBoneTransform> initial_;
	std::vector<int> mapping_, modes_;
	std::map<std::string, Entry> entries_;
};
}

#pragma once
#include "control.h"
#include "pose.h"
#include <cstdint>

namespace BodycamReference
{
Quaternion RecoilQuaternion(const Rotation&);
Rotation RecoilRotation(const Quaternion&);
Quaternion ProceduralYXZQuaternion(const Rotation&);
struct RecoilPose
{
	Rotation rotation{};
	Vector location{};
};
RecoilPose ComposeRecoilSway(const Rotation& base, const Rotation& sway, const Vector& pivot);
struct RecoilPoseSnapshot
{
	RecoilPose pose;
	std::uint64_t publication = 0, captures = 0;
};
class RecoilPoseCache
{
public:
	bool PreUpdate(const RecoilPose&, bool autoUpdate, bool componentValid, std::uint64_t publication);
	RecoilPoseSnapshot Evaluate() const;

private:
	Quaternion rotation_{ 0, 0, 0, 1 };
	Vector location_{};
	std::uint64_t publication_ = 0, captures_ = 0;
};
}

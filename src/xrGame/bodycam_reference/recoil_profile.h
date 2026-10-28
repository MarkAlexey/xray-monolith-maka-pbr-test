#pragma once
#include "recoil_random.h"

namespace BodycamReference
{
struct RecoilPoseProfile
{
	std::array<double, 2> pitch{};
	std::array<double, 4> yaw{}, roll{};
	std::array<double, 2> kick{}, right{}, up{};
};
struct RecoilSourceProfile
{
	RecoilPoseProfile hip, aim;
	double maxKick = 0, maxUp = 0;
	bool smoothRoll = false;
};
struct RecoilAttributes
{
	float vertical = .9000001f, horizontal = 1.f;
	float kick = .9250001f, up = .9250001f;
	float magnifiedKick = .6f, magnifiedUp = 1.f;
	float magnifiedMaxKick = .1f, magnifiedMaxUp = 1.f;
};
RecoilRandomProfile BuildRecoilProfile(const RecoilSourceProfile&, bool aiming, bool capture, const RecoilAttributes&);
}

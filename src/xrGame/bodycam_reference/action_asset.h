#pragma once
#include "animation_blend.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace BodycamReference
{
// One authored action: per-bone pose tracks sampled at a fixed rate, and keyed curves.
class AuthoredActionClip
{
public:
	LocalPose Pose(const std::string& bone, double time, const LocalPose& fallback) const;
	AnimationCurves Curves(double time) const;

private:
	friend class ActionAssetBank;
	struct Key
	{
		double time, value;
		bool linear;
	};
	double rate_ = 0;
	std::map<std::string, std::vector<LocalPose>> tracks_;
	std::map<std::string, std::vector<Key>> curves_;
};
class ActionAssetBank
{
public:
	static ActionAssetBank Load(const std::vector<std::uint8_t>&);
	const AuthoredActionClip& Clip(unsigned index) const { return clips_.at(index); }
	unsigned Size() const { return unsigned(clips_.size()); }

private:
	std::vector<AuthoredActionClip> clips_;
};
}

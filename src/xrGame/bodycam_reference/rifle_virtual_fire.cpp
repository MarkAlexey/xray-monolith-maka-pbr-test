#include "rifle_virtual_fire.h"
#include "asset_reader.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
namespace BodycamReference
{
RifleVirtualFire::RifleVirtualFire(std::vector<RifleTrack> tracks, std::vector<VirtualFireTrack> frames)
	: tracks_(std::move(tracks)), physicalTracks_(tracks_.begin(), tracks_.begin() + kPhysicalBoneCount)
{
	if (tracks_.size() != kSkeletonBoneCount || frames.size() != kVirtualBoneCount)
		throw std::invalid_argument("Virtual fire needs 89 rig tracks and 21 virtual tracks");
	std::map<std::string, std::array<LocalPose, kVirtualFireFrames>> byName;
	for (auto& track : frames)
	{
		for (const auto& p : track.frames)
			for (double v : p.scale)
				if (v != 0)
					throw std::invalid_argument("Virtual fire requires zero additive scale");
		if (!byName.emplace(LowerAscii(track.name), std::move(track.frames)).second)
			throw std::invalid_argument("Duplicate virtual fire track name");
	}
	for (unsigned i = kPhysicalBoneCount; i < kSkeletonBoneCount; ++i)
		frames_[i - kPhysicalBoneCount] = byName.at(LowerAscii(tracks_[i].name));
}
std::vector<LocalPose> RifleVirtualFire::Apply(
	const std::vector<LocalPose>& before, const std::vector<LocalPose>& physical, double weight, double time) const
{
	auto out = before;
	std::copy(physical.begin(), physical.end(), out.begin());
	if (weight <= kBlendEpsilon)
		return out;
	const auto old = RiflePoseComponents(before, tracks_), current = RiflePoseComponents(physical, physicalTracks_);
	std::array<Quaternion, kSkeletonBoneCount> rotations;
	for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
		rotations[i] = Decompose(current[i]).rotation;
	const double frame = std::clamp(time * kVirtualFireRate, 0., double(kVirtualFireFrames - 1));
	const unsigned i = unsigned(std::floor(frame)), j = std::min(kVirtualFireFrames - 1, i + 1);
	const float alpha = float(frame - i);
	for (unsigned k = kPhysicalBoneCount; k < kSkeletonBoneCount; ++k)
	{
		const auto& frames = frames_[k - kPhysicalBoneCount];
		const auto d = i == j ? frames[i] : BlendAnimationLocal(frames[i], frames[j], alpha);
		auto& p = out[k];
		for (unsigned n = 0; n < 3; ++n)
			p.position[n] = before[k].position[n] + d.position[n] * weight;
		auto q = d.rotation;
		if (q[3] < 0)
			for (auto& v : q)
				v = -v;
		const auto weighted = NormalizeQuaternion({ q[0] * weight, q[1] * weight, q[2] * weight, 1 + (q[3] - 1) * weight });
		rotations[k] = NormalizeQuaternion(QuaternionProduct(weighted, Decompose(old[k]).rotation));
		const int parent = tracks_[k].parent;
		if (parent < 0)
			p.rotation = rotations[k];
		else
		{
			const auto& parentQ = rotations[parent];
			p.rotation = NormalizeQuaternion(QuaternionProduct(Conjugate(parentQ), rotations[k]));
		}
	}
	return out;
}
}

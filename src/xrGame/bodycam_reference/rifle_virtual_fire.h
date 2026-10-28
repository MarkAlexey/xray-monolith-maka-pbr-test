#pragma once
#include "rifle_overlay.h"
#include "animation_blend.h"
#include "constants.h"
namespace BodycamReference
{
// The fire animation of the virtual bones: mesh-space additive frames in the exported basis.
constexpr unsigned kVirtualFireFrames = 28, kVirtualFireRate = 30;
struct VirtualFireTrack
{
	std::string name;
	std::array<LocalPose, kVirtualFireFrames> frames;
};
class RifleVirtualFire
{
public:
	RifleVirtualFire(std::vector<RifleTrack>, std::vector<VirtualFireTrack>);
	std::vector<LocalPose> Apply(
		const std::vector<LocalPose>& before, const std::vector<LocalPose>& afterPhysical, double weight, double time) const;

private:
	std::vector<RifleTrack> tracks_, physicalTracks_;
	std::array<std::array<LocalPose, kVirtualFireFrames>, kVirtualBoneCount> frames_;
};
}

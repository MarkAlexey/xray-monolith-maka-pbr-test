#pragma once
#include "authored_channel.h"
#include "rifle_virtual_fire.h"
#include "fire_montages.h"
#include "camera_intensity.h"
#include <optional>
namespace BodycamReference
{
class FirePose
{
public:
	static FirePose Load(const std::vector<std::uint8_t>&);
	std::vector<LocalPose> Apply(const std::vector<LocalPose>&, const std::vector<FireMontageSample>&) const;
	AnimationCurves CameraCurves(const std::vector<FireMontageSample>&, AnimationCurves base) const;

private:
	struct PhysicalTrack
	{
		LocalPose reference;
		Quaternion referenceComponent;
		std::array<AuthoredChannel, 3> channels;
	};
	std::vector<RifleTrack> tracks_, physicalTracks_;
	std::vector<PhysicalTrack> physical_;
	std::optional<RifleVirtualFire> virtual_;
	std::array<std::vector<std::pair<double, double>>, 3> curves_;
};
}

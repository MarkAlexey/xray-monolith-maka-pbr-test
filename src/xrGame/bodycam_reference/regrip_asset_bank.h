#pragma once
#include "regrip_clip.h"
#include "regrip_pose.h"
#include "regrip_montages.h"
namespace BodycamReference
{
class RegripAssetBank
{
public:
	static RegripAssetBank Load(const std::vector<std::uint8_t>&, const std::vector<std::string>& physicalNames);
	const std::vector<double>& Durations() const { return durations_; }
	std::vector<RegripSample> SamplePose(const std::vector<RegripMontageSample>&) const;
	// Fills output, reusing its storage.
	void SamplePose(const std::vector<RegripMontageSample>&, std::vector<RegripSample>& output) const;
	AnimationCurves CameraCurves(const std::vector<RegripMontageSample>&, AnimationCurves base = {}) const;

private:
	std::vector<RegripClip> clips_;
	std::vector<std::vector<unsigned>> indices_;
	std::vector<double> durations_;
};
}

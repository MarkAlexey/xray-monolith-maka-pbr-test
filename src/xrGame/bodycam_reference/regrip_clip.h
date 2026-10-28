#pragma once
#include "animation_blend.h"
#include <cstdint>

namespace BodycamReference
{
struct RegripClipData
{
	double duration = 0;
	float sampleRate = 30;
	unsigned samples = 0;
	std::vector<std::int64_t> trackOffsets;
	std::vector<float> values;
	std::map<std::string, std::vector<float>> curves;
};
struct RegripIndices
{
	unsigned first, second;
	float alpha;
};
// A stored sample in the source basis, in centimetres.
struct RegripDelta
{
	std::array<float, 3> position{};
	std::array<float, 4> rotation{ 0, 0, 0, 1 };
};
// One decoded regrip clip: stored samples at 30 Hz, linearly interpolated.
class RegripClip
{
public:
	explicit RegripClip(RegripClipData data);
	RegripIndices Indices(double time) const;
	RegripDelta Sample(unsigned track, double time) const;
	AnimationCurves Curves(double time) const;

private:
	RegripDelta Stored(unsigned track, unsigned sample) const;
	float SampleTime(double time) const;
	RegripClipData data_;
};
AdditivePose ExportRegripDelta(const RegripDelta&);
}

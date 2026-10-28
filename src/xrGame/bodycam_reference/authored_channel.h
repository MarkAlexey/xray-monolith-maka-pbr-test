#pragma once
#include <array>
#include <vector>
namespace BodycamReference
{
struct AuthoredChannelData
{
	unsigned width = 3;
	bool quaternion = false, step = false;
	std::vector<float> times, values;
};
// A keyframe track. Keys and results are float; interpolation and quaternion maths run in double.
class AuthoredChannel
{
public:
	explicit AuthoredChannel(AuthoredChannelData);
	std::array<float, 4> Sample(double time) const;

private:
	AuthoredChannelData data_;
};
}

// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "regrip_clip.h"
#include "constants.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
namespace
{
// Each stored sample is a position (3 floats) and a rotation (4 floats).
constexpr unsigned kValuesPerSample = 7;
float Lerp(float a, float b, float w)
{
	return (a - w * a) + w * b;
}
}
RegripClip::RegripClip(RegripClipData data) : data_(std::move(data))
{
	if (data_.duration <= 0 || data_.sampleRate != 30 || !data_.samples)
		throw std::invalid_argument("Regrip clip needs a positive duration, 30 samples per second and at least one sample");
	for (auto offset : data_.trackOffsets)
		if (offset < -1 ||
			(offset >= 0 &&
				(std::uint64_t(offset) > data_.values.size() ||
					std::uint64_t(data_.samples) * kValuesPerSample > data_.values.size() - std::uint64_t(offset))))
			throw std::invalid_argument("Regrip track offset lies outside the clip data");
	for (const auto& curve : data_.curves)
		if (curve.second.empty())
			throw std::invalid_argument("Regrip curve has no keys");
}
float RegripClip::SampleTime(double time) const
{
	return float(std::clamp(time, 0., data_.duration)) * data_.sampleRate;
}
RegripIndices RegripClip::Indices(double time) const
{
	const float sample = SampleTime(time);
	if (sample >= data_.samples)
		return { 0, 0, 0 };
	const auto first = unsigned(std::floor(sample));
	return { first, first + 1 == data_.samples ? 0 : first + 1, sample - float(first) };
}
RegripDelta RegripClip::Stored(unsigned track, unsigned sample) const
{
	const auto offset = data_.trackOffsets.at(track);
	if (offset < 0)
		return {};
	const auto at = std::size_t(offset) + std::size_t(sample) * kValuesPerSample;
	RegripDelta result;
	std::copy_n(data_.values.begin() + at, 3, result.position.begin());
	std::copy_n(data_.values.begin() + at + 3, 4, result.rotation.begin());
	return result;
}
RegripDelta RegripClip::Sample(unsigned track, double time) const
{
	const auto indices = Indices(time);
	auto a = Stored(track, indices.first);
	if (indices.alpha == 0)
		return a;
	const auto b = Stored(track, indices.second);
	std::array<float, 4> products;
	for (unsigned i = 0; i < 4; ++i)
		products[i] = a.rotation[i] * b.rotation[i];
	const float dot = ((products[1] + products[0]) + products[2]) + products[3];
	const float sign = std::signbit(dot) ? -1.f : 1.f;
	for (unsigned i = 0; i < 4; ++i)
		a.rotation[i] = Lerp(a.rotation[i], sign * b.rotation[i], indices.alpha);
	std::array<float, 4> squares;
	for (unsigned i = 0; i < 4; ++i)
		squares[i] = a.rotation[i] * a.rotation[i];
	const float length2 = ((squares[0] + squares[1]) + squares[2]) + squares[3];
	const float inverse = float(1. / float(std::sqrt(double(length2))));
	for (auto& v : a.rotation)
		v *= inverse;
	for (unsigned i = 0; i < 3; ++i)
		a.position[i] = Lerp(a.position[i], b.position[i], indices.alpha);
	return a;
}
AnimationCurves RegripClip::Curves(double time) const
{
	const float sample = SampleTime(time);
	AnimationCurves output;
	for (const auto& curve : data_.curves)
	{
		const auto& keys = curve.second;
		const auto first = std::size_t(std::min(double(std::floor(sample)), double(keys.size() - 1)));
		const auto second = std::min(first + 1, keys.size() - 1);
		output[curve.first] = first == second ? keys[first] : Lerp(keys[first], keys[second], sample - float(first));
	}
	return output;
}
AdditivePose ExportRegripDelta(const RegripDelta& delta)
{
	const auto& p = delta.position;
	const auto& q = delta.rotation;
	const double norm = std::hypot(std::hypot(double(q[0]), double(q[1])), std::hypot(double(q[2]), double(q[3])));
	if (norm == 0)
		throw std::invalid_argument("Regrip rotation is zero");
	return { { p[0] * kMetresPerCentimetre, p[2] * kMetresPerCentimetre, p[1] * kMetresPerCentimetre }, {},
		{ -q[0] / norm, -q[2] / norm, -q[1] / norm, q[3] / norm } };
}
}

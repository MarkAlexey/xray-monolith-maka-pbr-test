#include "authored_channel.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace BodycamReference
{
namespace
{
// Quaternion keys closer than this (by dot product) blend linearly and renormalise instead of slerping.
constexpr double kSlerpLinearDot = .9995;
}
AuthoredChannel::AuthoredChannel(AuthoredChannelData data) : data_(std::move(data))
{
	if (data_.width < 1 || data_.width > 4 || (data_.quaternion && data_.width != 4) || data_.times.empty() ||
		data_.values.size() != data_.times.size() * data_.width)
		throw std::invalid_argument("Channel width, key count and value count do not agree");
	if (!std::is_sorted(data_.times.begin(), data_.times.end()) ||
		!std::all_of(data_.times.begin(), data_.times.end(), [](float v) { return std::isfinite(v); }) ||
		!std::all_of(data_.values.begin(), data_.values.end(), [](float v) { return std::isfinite(v); }))
		throw std::invalid_argument("Channel keys must be finite and ordered in time");
}
std::array<float, 4> AuthoredChannel::Sample(double time) const
{
	const auto end = std::upper_bound(data_.times.begin(), data_.times.end(), time);
	const auto next = std::size_t(end - data_.times.begin());
	std::array<float, 4> result{};
	const auto copy = [&](std::size_t key) { std::copy_n(data_.values.begin() + key * data_.width, data_.width, result.begin()); };
	if (next == 0)
	{
		copy(0);
		return result;
	}
	if (next == data_.times.size() || data_.step)
	{
		copy(next - 1);
		return result;
	}
	const double alpha = (time - data_.times[next - 1]) / (double(data_.times[next]) - data_.times[next - 1]);
	const auto left = (next - 1) * data_.width, right = next * data_.width;
	if (!data_.quaternion)
	{
		for (unsigned i = 0; i < data_.width; ++i)
			result[i] = float(data_.values[left + i] * (1 - alpha) + data_.values[right + i] * alpha);
		return result;
	}
	std::array<double, 4> a{}, b{};
	bool different = false;
	for (unsigned i = 0; i < 4; ++i)
	{
		a[i] = data_.values[left + i];
		b[i] = data_.values[right + i];
		different |= a[i] != b[i];
	}
	if (different)
	{
		double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
		if (dot < 0)
		{
			for (auto& v : b)
				v = -v;
			dot = -dot;
		}
		double s = 1 - alpha, t = alpha;
		if (dot < kSlerpLinearDot)
		{
			const double angle = std::acos(dot), sine = std::sin(angle);
			s = std::sin(s * angle) / sine;
			t = std::sin(t * angle) / sine;
			for (unsigned i = 0; i < 4; ++i)
				a[i] = a[i] * s + b[i] * t;
		}
		else
		{
			for (unsigned i = 0; i < 4; ++i)
				a[i] = a[i] * s + b[i] * t;
			const double inverse = 1 / std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] + a[3] * a[3]);
			for (auto& v : a)
				v *= inverse;
		}
	}
	for (unsigned i = 0; i < 4; ++i)
		result[i] = float(a[i]);
	return result;
}
}

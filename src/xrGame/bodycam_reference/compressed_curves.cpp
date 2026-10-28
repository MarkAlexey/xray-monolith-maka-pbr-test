// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "compressed_curves.h"
#include "constants.h"
#include "recoil_curve.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
constexpr std::size_t kDescriptorBytes = 12;
// Descriptor format: one constant, linear keys of one float, or mixed keys of value and two tangents.
enum : std::uint8_t
{
	kConstantCurve = 1,
	kLinearKeys = 2,
	kMixedKeys = 4
};
// How a key interpolates towards the next one.
enum : std::uint8_t
{
	kConstantKey = 1,
	kLinearKey = 2,
	kCubicKey = 3
};
}
CompressedLocomotionCurves::CompressedLocomotionCurves(const std::vector<std::uint8_t>& bytes, const std::vector<std::string>& names)
{
	const auto u32 = [&](std::size_t offset)
	{
		if (offset > bytes.size() || bytes.size() - offset < 4)
			throw std::invalid_argument("Truncated curve word");
		return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) |
			(std::uint32_t(bytes[offset + 3]) << 24);
	};
	const auto scalar = [&](std::size_t offset)
	{
		const auto bits = u32(offset);
		float value;
		std::memcpy(&value, &bits, 4);
		if (!std::isfinite(value))
			throw std::invalid_argument("Nonfinite compressed curve scalar");
		return value;
	};
	if (names.size() > bytes.size() / kDescriptorBytes)
		throw std::invalid_argument("Truncated curve descriptors");
	std::set<std::string> distinct;
	for (std::size_t i = 0; i < names.size(); ++i)
	{
		if (names[i].empty() || !distinct.insert(names[i]).second)
			throw std::invalid_argument("Curve names must be distinct and not empty");
		const auto offset = i * kDescriptorBytes;
		const auto format = bytes[offset];
		const auto keyOffset = u32(offset + 8);
		if ((format != kConstantCurve && format != kLinearKeys && format != kMixedKeys) || bytes[offset + 1] != 1 ||
			bytes[offset + 2] != 4 || bytes[offset + 3] != 4 || keyOffset < names.size() * kDescriptorBytes || keyOffset > bytes.size())
			throw std::invalid_argument("Unsupported curve descriptor");
		Curve curve;
		curve.name = names[i];
		if (format == kConstantCurve)
			curve.constant = scalar(offset + 4);
		else
		{
			// Mixed keys start with one mode byte per key, padded to four bytes.
			const bool mixed = format == kMixedKeys;
			const std::size_t count = u32(offset + 4), times = keyOffset + (mixed ? ((count + 3) / 4) * 4 : 0), values = times + 4 * count,
							  stride = mixed ? 12 : 4;
			if (count < 2 || count > bytes.size() || values > bytes.size() || count > (bytes.size() - values) / stride)
				throw std::invalid_argument("Truncated curve keys");
			for (std::size_t k = 0; k < count; ++k)
			{
				const std::uint8_t mode = mixed ? bytes[keyOffset + k] : kLinearKey;
				const float time = scalar(times + 4 * k), value = scalar(values + stride * k);
				if ((mode != kConstantKey && mode != kLinearKey && mode != kCubicKey) || (k && !(time > curve.keys.back().time)))
					throw std::invalid_argument("Curve keys must be constant, linear or cubic and ordered in time");
				curve.keys.push_back(
					{ time, value, mode, mixed ? scalar(values + stride * k + 4) : 0, mixed ? scalar(values + stride * k + 8) : 0 });
			}
		}
		curves_.push_back(std::move(curve));
	}
}
float CompressedLocomotionCurves::Curve::Sample(float time) const
{
	if (keys.empty())
		return constant;
	if (time < keys.front().time)
		return keys.front().value;
	if (time > keys.back().time)
		return keys.back().value;
	const auto next =
		std::upper_bound(keys.begin() + 1, keys.end() - 1, time, [](float t, const CompressedCurveKey& key) { return t < key.time; });
	const auto& left = *(next - 1);
	const auto& right = *next;
	const float dt = right.time - left.time;
	if (!(dt > 0) || left.mode == kConstantKey)
		return left.value + 0.f;
	const float alpha = (time - left.time) / dt;
	if (left.mode == kLinearKey)
		return ((right.value - left.value) * alpha + left.value) + 0.f;
	const float third = dt * (1.f / 3.f), c1 = third * left.leave + left.value, c2 = right.value - third * right.arrive;
	return CubicBezier(left.value, c1, c2, right.value, alpha) + 0.f;
}
NamedCurves CompressedLocomotionCurves::Sample(float time, const std::set<std::string>& names) const
{
	NamedCurves out;
	for (const auto& curve : curves_)
		if (names.count(curve.name))
			out.push_back({ curve.name, curve.Sample(time), 0 });
	return out;
}
NamedCurveEntry& FindOrAddCurve(NamedCurves& curves, const std::string& name)
{
	const auto found = std::find_if(curves.begin(), curves.end(), [&](const NamedCurveEntry& e) { return e.name == name; });
	if (found != curves.end())
		return *found;
	curves.push_back({ name, 0, 0 });
	return curves.back();
}
NamedCurves BlendWeightedCurves(const std::vector<WeightedNamedCurves>& samples)
{
	if (samples.empty() || samples.size() > 4)
		throw std::invalid_argument("Expected one to four weighted curve sets");
	NamedCurves out;
	for (unsigned i = 0; i < samples.size(); ++i)
	{
		const auto& sample = samples[i];
		const float a = sample.weight;
		if (!std::isfinite(a) || a < 0 || a > 1)
			throw std::invalid_argument("Curve weight is outside [0, 1]");
		if (i == 0)
		{
			out = sample.curves;
			if (a != 1.f)
				for (auto& entry : out)
					entry.value = a * entry.value;
		}
		else if (a > kBlendEpsilon)
			for (const auto& entry : sample.curves)
			{
				auto& found = FindOrAddCurve(out, entry.name);
				found.value = a * entry.value + found.value;
				found.flags |= entry.flags;
			}
	}
	return out;
}
}

// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "pertrack_pose.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
namespace BodycamReference
{
namespace
{
// One seed per 2^11 float bit patterns from .25 (0x3e800000) up to 4.
constexpr unsigned kSeedCount = 16384, kSeedShift = 11;
constexpr std::uint32_t kQuarterBits = 0x3e800000;
}
RsqrtSeedTable::RsqrtSeedTable(const std::vector<std::uint8_t>& packed)
{
	if (packed.size() != kSeedCount * 3 / 8)
		throw std::invalid_argument("Rsqrt seed table has " + std::to_string(packed.size()) + " bytes, expected 6144");
	std::uint32_t value = 0x3ffff000; // first seed, for .25
	std::memcpy(&seeds_[0], &value, 4);
	for (unsigned i = 0; i + 1 < kSeedCount; ++i)
	{
		const unsigned bit = i * 3, pos = bit >> 3, shift = bit & 7;
		const unsigned next = pos + 1 < packed.size() ? packed[pos + 1] : 0, drop = ((packed[pos] | (next << 8)) >> shift) & 7;
		value -= drop << kSeedShift;
		std::memcpy(&seeds_[i + 1], &value, 4);
	}
}
float RsqrtSeedTable::Estimate(float length) const
{
	if (!(length >= .25f && length < 4.f))
		throw std::invalid_argument("Rsqrt seed table covers [.25, 4) only");
	std::uint32_t bits;
	std::memcpy(&bits, &length, 4);
	return seeds_[(bits - kQuarterBits) >> kSeedShift];
}
namespace
{
FloatQuaternion NormalizeStored(FloatQuaternion q, const RsqrtSeedTable& table)
{
	const float length = (q[2] * q[2] + q[0] * q[0]) + (q[3] * q[3] + q[1] * q[1]), half = length * .5f, seed = table.Estimate(length);
	float n = (.5f - (seed * seed) * half) * seed + seed;
	n = (.5f - (n * n) * half) * n + n;
	for (float& value : q)
		value *= n;
	return q;
}
FloatQuaternion BlendStored(const FloatQuaternion& a, const FloatQuaternion& b, float t)
{
	const float dot = (a[2] * b[2] + a[0] * b[0]) + (a[3] * b[3] + a[1] * b[1]), weight = (1.f - t) * (dot < 0 ? -1.f : 1.f);
	FloatQuaternion out;
	for (unsigned i = 0; i < 4; ++i)
		out[i] = a[i] * weight + b[i] * t;
	return out;
}
}
PerTrackPose::PerTrackPose(double length, unsigned frames, std::vector<PerTrackBoneKeys> tracks, Policy policy)
	: length_(length), frames_(frames), policy_(policy), tracks_(std::move(tracks))
{
	SequenceFrameContext(0, length_, 30, 1); // throws unless the length rounds to at least one 30 fps frame
	const bool framesValid =
		policy_ == Policy::Locomotion ? (frames == 19 || frames == 21 || frames == 25 || frames == 35) : (frames == 31 && length == 1);
	const bool tracksValid = policy_ == Policy::DetailAdditive ? (tracks_.size() >= 47 && tracks_.size() <= 49) : tracks_.size() == 86;
	if (!framesValid || !tracksValid || tracks_[0].boneIndex != 0)
		throw std::invalid_argument("Per-track pose has an unexpected frame count, track count or root track");
	for (const auto& track : tracks_)
	{
		const auto validateFrames = [&](const std::vector<unsigned>& indices, std::size_t count)
		{
			if (indices.empty())
				return;
			if (indices.size() != count || count < 2 || indices.front() != 0 || indices.back() != frames_ - 1)
				throw std::invalid_argument("Key frame table does not match its keys or the clip frames");
			for (unsigned i = 1; i < indices.size(); ++i)
				if (indices[i] <= indices[i - 1])
					throw std::invalid_argument("Key frame table is not strictly increasing");
		};
		validateFrames(track.translationFrames, track.translations.size());
		validateFrames(track.rotationFrames, track.rotations.size());
		for (const auto& p : track.translations)
			for (float value : p)
				if (!std::isfinite(value))
					throw std::invalid_argument("Translation key is not finite");
		for (const auto& q : track.rotations)
			for (float value : q)
				if (!std::isfinite(value))
					throw std::invalid_argument("Rotation key is not finite");
		const FloatVector scale = policy_ == Policy::DetailAdditive ? FloatVector{ 0, 0, 0 } : FloatVector{ 1, 1, 1 };
		if (track.scale != scale)
			throw std::invalid_argument("Track scale does not match the pose policy");
	}
}
PerTrackPose::Selection PerTrackPose::Select(float r, unsigned count, const std::vector<unsigned>& frames) const
{
	const unsigned last = count - 1;
	if (count == 1 || r <= 0)
		return { 0, 0, 0 };
	if (r >= 1)
		return { last, last, 0 };
	if (frames.empty())
	{
		const float position = r * float(last), floor = std::floor(position);
		const unsigned first = std::min(unsigned(floor), last);
		return { first, std::min(first + 1, last), position - floor };
	}
	const float position = float(frames_ - 1) * r;
	const unsigned frame = std::min(frames_ - 2, unsigned(std::max(0.f, position)));
	unsigned first = std::min(last, unsigned(std::max(0.f, float(last) * r)));
	if (frames[first] > frame)
	{
		while (first > 0 && frames[first] > frame)
			--first;
	}
	else
		while (first < last && frames[first + 1] <= frame)
			++first;
	const unsigned second = std::min(first + 1, last);
	return { first, second, (position - float(frames[first])) / float(std::max(frames[second] - frames[first], 1u)) };
}
std::vector<SourceBoneTransform> PerTrackPose::Sample(double seconds, const std::vector<SourceBoneTransform>& initial,
	const std::vector<int>& mapping, const std::vector<int>& modes, const RsqrtSeedTable& rsqrt) const
{
	if (mapping.empty() || mapping[0] < 0 || unsigned(mapping[0]) >= initial.size())
		throw std::invalid_argument("Skeleton mapping has no root bone");
	auto pose = initial;
	if (policy_ == Policy::DetailAdditive)
		for (auto& t : pose)
			t = { { 0, 0, 0 }, { 0, 0, 0, 1 }, { 0, 0, 0 } };
	else if (policy_ == Policy::Locomotion)
		pose[mapping[0]] = { { 0, 0, 0 }, { 0, 0, 0, 1 }, { 1, 1, 1 } };
	const float relative = SequenceFrameContext(seconds, length_, 30, 1).relative;
	for (unsigned i = 1; i < tracks_.size(); ++i)
	{
		const auto& track = tracks_[i];
		const int compact = track.boneIndex < mapping.size() ? mapping[track.boneIndex] : -1;
		if (compact == -1)
			continue;
		if (compact < 0 || unsigned(compact) >= pose.size())
			throw std::invalid_argument("Skeleton mapping points past the pose");
		const int mode = track.boneIndex < modes.size() ? modes[track.boneIndex] : 0;
		if (mode != 0 && mode != 1)
			throw std::invalid_argument("Translation mode is neither animation (0) nor skeleton (1)");
		auto& out = pose[compact];
		if (mode == 0)
		{
			FloatVector p{};
			if (!track.translations.empty())
			{
				const auto key = Select(relative, unsigned(track.translations.size()), track.translationFrames);
				p = track.translations[key.first];
				if (key.first != key.second)
					for (unsigned c = 0; c < 3; ++c)
						p[c] = p[c] + (track.translations[key.second][c] - p[c]) * key.alpha;
			}
			for (unsigned c = 0; c < 3; ++c)
				out.p[c] = p[c];
		}
		FloatQuaternion q = { 0, 0, 0, 1 };
		if (!track.rotations.empty())
		{
			const auto key = Select(relative, unsigned(track.rotations.size()), track.rotationFrames);
			q = track.rotations[key.first];
			if (key.first != key.second)
				q = BlendStored(q, track.rotations[key.second], key.alpha);
			q = NormalizeStored(q, rsqrt);
		}
		for (unsigned c = 0; c < 4; ++c)
			out.q[c] = q[c];
		for (unsigned c = 0; c < 3; ++c)
			out.s[c] = track.scale[c];
	}
	return pose;
}
}

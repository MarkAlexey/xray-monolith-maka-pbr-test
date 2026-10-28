#include "action_asset.h"
#include "asset_reader.h"
#include <algorithm>
namespace BodycamReference
{
namespace
{
constexpr unsigned kMaxClips = 128, kMaxFramesPerClip = 4096, kMaxTracksPerClip = 512, kMaxCurvesPerClip = 512, kMaxKeysPerCurve = 4096;
}
ActionAssetBank ActionAssetBank::Load(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCRACTN1", 1);
	ActionAssetBank bank;
	const auto count = r.Count(kMaxClips);
	for (unsigned c = 0; c < count; ++c)
	{
		AuthoredActionClip clip;
		clip.rate_ = r.F64();
		const auto samples = r.Count(kMaxFramesPerClip), tracks = r.Count(kMaxTracksPerClip);
		if (clip.rate_ <= 0 || !samples || !tracks)
			throw std::invalid_argument("Action clip has no frames, no tracks or a non-positive rate");
		for (unsigned t = 0; t < tracks; ++t)
		{
			const auto name = LowerAscii(r.String());
			if (!clip.tracks_.emplace(name, r.LocalPoses(samples)).second)
				throw std::invalid_argument("Duplicate action track");
		}
		const auto curves = r.Count(kMaxCurvesPerClip);
		for (unsigned j = 0; j < curves; ++j)
		{
			const auto name = r.String();
			const auto keys = r.Count(kMaxKeysPerCurve);
			std::vector<AuthoredActionClip::Key> values;
			for (unsigned k = 0; k < keys; ++k)
			{
				const double time = r.F64(), value = r.F64();
				const bool linear = r.Bool();
				if (!values.empty() && time < values.back().time)
					throw std::invalid_argument("Action curve keys are out of order");
				values.push_back({ time, value, linear });
			}
			if (!clip.curves_.emplace(name, std::move(values)).second)
				throw std::invalid_argument("Duplicate action curve");
		}
		bank.clips_.push_back(std::move(clip));
	}
	if (!r.Done())
		throw std::invalid_argument("Action package has trailing bytes");
	return bank;
}
LocalPose AuthoredActionClip::Pose(const std::string& name, double time, const LocalPose& fallback) const
{
	const auto it = tracks_.find(LowerAscii(name));
	if (it == tracks_.end())
		return fallback;
	const auto& frames = it->second;
	const double frame = std::clamp(time * rate_, 0., double(frames.size() - 1));
	const unsigned i = unsigned(std::floor(frame)), j = std::min(i + 1, unsigned(frames.size() - 1));
	return BlendAnimationLocal(frames[i], frames[j], float(frame - i));
}
AnimationCurves AuthoredActionClip::Curves(double time) const
{
	AnimationCurves out;
	for (const auto& curve : curves_)
	{
		const auto& keys = curve.second;
		if (keys.empty())
			continue;
		unsigned i = 0;
		while (i + 1 < keys.size() && keys[i + 1].time <= time)
			++i;
		const unsigned next = std::min(i + 1, unsigned(keys.size() - 1));
		const auto& a = keys[i];
		const auto& b = keys[next];
		const double alpha = i == next ? 0 : std::clamp((time - a.time) / (b.time - a.time), 0., 1.);
		out[curve.first] = float(a.value + (a.linear ? (b.value - a.value) * alpha : 0));
	}
	return out;
}
}

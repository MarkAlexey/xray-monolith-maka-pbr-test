#include "pose_asset_bank.h"
#include "asset_reader.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
PoseAssetBank PoseAssetBank::Load(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader r(bytes);
	const auto magic = r.Bytes(8);
	const unsigned version = r.U32();
	if ((version != 1 && version != 2) || std::string(magic.begin(), magic.end()) != "BCRPOSE" + std::to_string(version))
		throw std::invalid_argument("Asset package is not BCRPOSE1 or BCRPOSE2");
	if (r.String() != kAssetSourceRevision)
		throw std::invalid_argument("BCRPOSE package was exported from another source revision");
	PoseAssetBank out;
	out.rsqrt_.emplace(r.Bytes(r.Count(6144))); // see RsqrtSeedTable
	out.initial_ = r.Pose();
	if (out.initial_.size() != kSkeletonBoneCount)
		throw std::invalid_argument("Pose package initial pose does not have 89 bones");
	out.mapping_.resize(r.Count(kSourceBoneCount));
	for (auto& v : out.mapping_)
		v = r.I32();
	out.modes_.resize(r.Count(kTranslationModeCount));
	for (auto& v : out.modes_)
		v = r.I32();
	if (out.mapping_.size() != kSourceBoneCount || out.modes_.size() != kTranslationModeCount)
		throw std::invalid_argument("Pose package skeleton mapping is incomplete");
	const unsigned expected = version == 1 ? 21 : 32;
	const auto count = r.Count(expected);
	if (count != expected)
		throw std::invalid_argument("BCRPOSE" + std::to_string(version) + " package has " + std::to_string(count) + " entries, expected " +
			std::to_string(expected));
	for (unsigned i = 0; i < count; ++i)
	{
		const auto identity = r.String();
		const double length = r.F64();
		r.F64(); // rate scale
		const auto kind = r.U32();
		if (identity.empty() || out.entries_.count(identity) || length < 0)
			throw std::invalid_argument("Pose entry name is empty or repeated, or its length is negative");
		if (kind == 0)
		{
			auto pose = r.Pose();
			if (pose.size() != kSkeletonBoneCount)
				throw std::invalid_argument("Constant pose entry does not have 89 bones");
			out.entries_.emplace(identity, Entry{ length, std::move(pose) });
		}
		else if (kind == 1 || (version == 2 && (kind == 2 || kind == 3)))
		{
			const auto frames = r.U32();
			std::vector<PerTrackBoneKeys> tracks(r.Count(86));
			for (auto& t : tracks)
			{
				t.boneIndex = r.U32();
				t.translations.resize(r.Count(35));
				for (auto& p : t.translations)
					for (auto& v : p)
						v = r.F32();
				t.translationFrames.resize(r.Count(35));
				for (auto& v : t.translationFrames)
					v = r.U32();
				t.rotations.resize(r.Count(35));
				for (auto& q : t.rotations)
					for (auto& v : q)
						v = r.F32();
				t.rotationFrames.resize(r.Count(35));
				for (auto& v : t.rotationFrames)
					v = r.U32();
				for (auto& v : t.scale)
					v = r.F32();
			}
			out.entries_.emplace(
				identity, Entry{ length, PerTrackPose(length, frames, std::move(tracks), PerTrackPose::Policy(kind - 1)) });
		}
		else
			throw std::invalid_argument("Unknown pose asset kind");
	}
	if (!r.Done())
		throw std::invalid_argument("Trailing pose asset data");
	return out;
}
std::vector<SourceBoneTransform> PoseAssetBank::Sample(const std::string& identity, double seconds) const
{
	return Sample(entries_.at(identity), seconds);
}
std::vector<SourceBoneTransform> PoseAssetBank::Sample(const Entry& entry, double seconds) const
{
	if (const auto* moving = std::get_if<PerTrackPose>(&entry.data))
		return moving->Sample(seconds, initial_, mapping_, modes_, *rsqrt_);
	return std::get<std::vector<SourceBoneTransform>>(entry.data);
}
PackedPose PoseAssetBank::Evaluate(const std::vector<BlendMarkerSample>& samples) const
{
	std::vector<WeightedPackedPose> evaluated;
	for (const auto& s : samples)
	{
		const auto& entry = entries_.at(s.sequenceIdentity);
		const float time = std::max(0.f, std::min(s.time, float(entry.length))), weight = std::clamp(float(s.weight), 0.f, 1.f);
		evaluated.push_back({ weight, PackPose(Sample(entry, time)) });
	}
	return BlendSpacePackedPose(evaluated);
}
}

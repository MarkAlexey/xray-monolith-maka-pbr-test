#include "regrip_asset_bank.h"
#include "asset_reader.h"
#include "camera_intensity.h"
namespace BodycamReference
{
namespace
{
constexpr unsigned kClipCount = 4, kMaxSamples = 8192, kMaxValues = 10000000, kMaxCurves = 128;
}
RegripAssetBank RegripAssetBank::Load(const std::vector<std::uint8_t>& bytes, const std::vector<std::string>& physical)
{
	if (physical.size() != kPhysicalBoneCount)
		throw std::invalid_argument("Regrip needs the names of the 68 physical bones");
	// The virtual bones RegripPoseLayer appends after the physical ones, in its order.
	auto names = physical;
	for (const auto* name : { "VB ik_hand_gun", "VB hand_r", "VB hand_l", "VB lowerarm_r", "VB lowerarm_l" })
		names.emplace_back(name);
	for (auto& name : names)
		name = LowerAscii(std::move(name));
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCRGRIP1", 1);
	if (r.U32() != kClipCount)
		throw std::invalid_argument("Regrip package does not hold four clips");
	RegripAssetBank bank;
	for (unsigned clip = 0; clip < kClipCount; ++clip)
	{
		RegripClipData data;
		data.duration = r.F64();
		data.sampleRate = r.F32();
		data.samples = r.Count(kMaxSamples);
		if (r.U32() != kSourceBoneCount)
			throw std::invalid_argument("Regrip clip does not have a track for every source bone");
		std::map<std::string, unsigned> tracks;
		for (unsigned i = 0; i < kSourceBoneCount; ++i)
		{
			auto name = LowerAscii(r.String());
			const auto parent = r.I32();
			if (parent < -1 || parent >= int(i) || !tracks.emplace(name, i).second)
				throw std::invalid_argument("Regrip track precedes its parent or repeats a name");
			data.trackOffsets.push_back(r.I32());
		}
		data.values.resize(r.Count(kMaxValues));
		for (auto& v : data.values)
			v = r.F32();
		const auto curves = r.Count(kMaxCurves);
		for (unsigned i = 0; i < curves; ++i)
		{
			auto name = r.String();
			std::vector<float> values(r.Count(kMaxSamples));
			for (auto& v : values)
				v = r.F32();
			if (!data.curves.emplace(std::move(name), std::move(values)).second)
				throw std::invalid_argument("Duplicate regrip curve");
		}
		std::vector<unsigned> indices;
		indices.reserve(names.size());
		for (const auto& name : names)
		{
			const auto found = tracks.find(name);
			if (found == tracks.end())
				throw std::invalid_argument("Regrip clip has no track for a rig bone");
			indices.push_back(found->second);
		}
		bank.durations_.push_back(data.duration);
		bank.indices_.push_back(std::move(indices));
		bank.clips_.emplace_back(std::move(data));
	}
	if (!r.Done())
		throw std::invalid_argument("Trailing regrip package data");
	return bank;
}
std::vector<RegripSample> RegripAssetBank::SamplePose(const std::vector<RegripMontageSample>& samples) const
{
	std::vector<RegripSample> output;
	SamplePose(samples, output);
	return output;
}
void RegripAssetBank::SamplePose(const std::vector<RegripMontageSample>& samples, std::vector<RegripSample>& output) const
{
	output.resize(samples.size());
	for (unsigned i = 0; i < samples.size(); ++i)
	{
		const auto& sample = samples[i];
		const auto& clip = clips_.at(sample.clip);
		const auto& tracks = indices_.at(sample.clip);
		auto& pose = output[i];
		pose.weight = sample.weight;
		pose.deltas.clear();
		pose.deltas.reserve(tracks.size());
		for (unsigned track : tracks)
			pose.deltas.push_back(ExportRegripDelta(clip.Sample(track, sample.time)));
	}
}
AnimationCurves RegripAssetBank::CameraCurves(const std::vector<RegripMontageSample>& samples, AnimationCurves base) const
{
	for (const auto& sample : samples)
		base = CameraSlotCurves(clips_.at(sample.clip).Curves(sample.time), sample.weight, base);
	return base;
}
}

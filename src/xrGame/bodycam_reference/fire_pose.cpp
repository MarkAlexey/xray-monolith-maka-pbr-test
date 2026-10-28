#include "fire_pose.h"
#include "constants.h"
#include "asset_reader.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
FirePose FirePose::Load(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCRFIRE1", 1);
	FirePose result;
	for (unsigned i = 0; i < kSkeletonBoneCount; ++i)
		result.tracks_.push_back({ r.String(), r.I32() });
	result.physicalTracks_.assign(result.tracks_.begin(), result.tracks_.begin() + kPhysicalBoneCount);
	for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
	{
		auto reference = r.LocalPoses(1).front();
		reference.rotation = NormalizeQuaternion(reference.rotation);
		for (double s : reference.scale)
			if (s == 0)
				throw std::invalid_argument("Fire reference pose has a zero scale");
		const auto channel = [&](unsigned property)
		{
			AuthoredChannelData data;
			data.quaternion = property == 1;
			data.width = property == 1 ? 4 : 3;
			data.step = r.Bool();
			const auto count = r.Count(4096);
			data.times.resize(count);
			data.values.resize(count * data.width);
			for (auto& v : data.times)
				v = r.F32();
			for (auto& v : data.values)
				v = r.F32();
			return AuthoredChannel(std::move(data));
		};
		const int parent = result.tracks_[i].parent;
		if (parent < -1 || parent >= int(i))
			throw std::invalid_argument("Fire skeleton parent does not precede its child");
		const auto component =
			parent < 0 ? reference.rotation : QuaternionProduct(result.physical_[parent].referenceComponent, reference.rotation);
		result.physical_.push_back({ reference, component, { channel(0), channel(1), channel(2) } });
	}
	std::vector<VirtualFireTrack> virtualTracks;
	for (unsigned i = 0; i < kVirtualBoneCount; ++i)
	{
		VirtualFireTrack t;
		t.name = r.String();
		const auto frames = r.LocalPoses(t.frames.size());
		std::copy(frames.begin(), frames.end(), t.frames.begin());
		virtualTracks.push_back(std::move(t));
	}
	result.virtual_.emplace(result.tracks_, std::move(virtualTracks));
	for (auto& curve : result.curves_)
	{
		const auto count = r.Count(4096);
		for (unsigned i = 0; i < count; ++i)
		{
			const double time = r.F64(), value = r.F64();
			if (!curve.empty() && time < curve.back().first)
				throw std::invalid_argument("Fire curve keys are not in time order");
			curve.emplace_back(time, value);
		}
	}
	if (!r.Done())
		throw std::invalid_argument("Trailing fire package bytes");
	return result;
}
std::vector<LocalPose> FirePose::Apply(const std::vector<LocalPose>& base, const std::vector<FireMontageSample>& samples) const
{
	auto output = base;
	for (const auto& sample : samples)
	{
		if (sample.weight <= kBlendEpsilon)
			continue;
		std::vector<LocalPose> physical(output.begin(), output.begin() + kPhysicalBoneCount);
		std::array<Quaternion, kPhysicalBoneCount> fireQ, outQ;
		for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
		{
			const auto& track = physical_[i];
			const auto p = track.channels[0].Sample(sample.time), s = track.channels[2].Sample(sample.time);
			const auto q = track.channels[1].Sample(sample.time);
			const auto local = NormalizeQuaternion({ q[0], q[1], q[2], q[3] });
			const int parent = tracks_[i].parent;
			fireQ[i] = parent < 0 ? local : QuaternionProduct(fireQ[parent], local);
			for (unsigned n = 0; n < 3; ++n)
			{
				physical[i].position[n] += (double(p[n]) - track.reference.position[n]) * sample.weight;
				physical[i].scale[n] *= 1 + (double(s[n]) / track.reference.scale[n] - 1) * sample.weight;
			}
		}
		const auto components = RiflePoseComponents(physical, physicalTracks_);
		for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
		{
			auto delta = NormalizeQuaternion(QuaternionProduct(fireQ[i], Conjugate(physical_[i].referenceComponent)));
			if (delta[3] < 0)
				for (auto& v : delta)
					v = -v;
			const double w = sample.weight;
			const auto weighted = NormalizeQuaternion({ delta[0] * w, delta[1] * w, delta[2] * w, 1 + (delta[3] - 1) * w });
			outQ[i] = NormalizeQuaternion(QuaternionProduct(weighted, Decompose(components[i]).rotation));
		}
		for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
		{
			const int parent = tracks_[i].parent;
			const auto q = parent < 0 ? Quaternion{ 0, 0, 0, 1 } : outQ[parent];
			physical[i].rotation = NormalizeQuaternion(QuaternionProduct(Conjugate(q), outQ[i]));
		}
		output = virtual_->Apply(output, physical, sample.weight, sample.time);
	}
	return output;
}
AnimationCurves FirePose::CameraCurves(const std::vector<FireMontageSample>& samples, AnimationCurves base) const
{
	const std::array<const char*, 3> names = { "Camera_Pitch", "Camera_Yaw", "Camera_Roll" };
	for (const auto& sample : samples)
	{
		AnimationCurves curves;
		for (unsigned i = 0; i < 3; ++i)
		{
			const auto& keys = curves_[i];
			double value = 0;
			if (!keys.empty())
			{
				const auto it =
					std::lower_bound(keys.begin(), keys.end(), sample.time, [](const auto& key, double t) { return key.first < t; });
				if (it == keys.begin())
					value = it->second;
				else if (it == keys.end())
					value = keys.back().second;
				else
				{
					const auto& a = *(it - 1);
					const double t = (sample.time - a.first) / std::max(1e-9, it->first - a.first);
					value = a.second + (it->second - a.second) * t;
				}
			}
			curves[names[i]] = float(value);
		}
		base = CameraSlotCurves(curves, sample.weight, base);
	}
	return base;
}
}

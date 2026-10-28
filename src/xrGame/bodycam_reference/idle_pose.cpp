#include "idle_pose.h"
#include "asset_reader.h"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace BodycamReference
{
namespace
{
// A position, rotation and scale channel per physical bone.
constexpr unsigned kMaxSecondaryTracks = kPhysicalBoneCount * 3;
constexpr unsigned kMaxKeysPerTrack = 4096;
constexpr double kSecondaryMotionWeight = .75;
}
IdlePose::IdlePose(std::vector<int> parents, std::vector<LocalPose> stance, std::vector<LocalPose> rifle,
	std::vector<IdleSecondaryTrack> secondary, double duration, unsigned spine)
	: skeleton_(parents, stance), stance_(std::move(stance)), rifle_(std::move(rifle)), secondary_(std::move(secondary)),
	  duration_(duration), spine_(spine)
{
	if (rifle_.size() != stance_.size() || spine_ >= stance_.size() || !std::isfinite(duration_) || duration_ <= 0)
		throw std::invalid_argument("Stance and rifle poses differ in size, or the spine index or duration is invalid");
	for (auto* poses : { &stance_, &rifle_ })
		for (auto& pose : *poses)
			pose.rotation = NormalizeQuaternion(pose.rotation);
	skeleton_.ResetLocals(rifle_);
	rifleSpine_ = RotationMatrixQuaternion(skeleton_.Component(spine_));
	for (unsigned i = 0; i < parents.size(); ++i)
	{
		for (int bone = int(i); bone >= 0; bone = parents[bone])
			if (unsigned(bone) == spine_)
			{
				rifleBones_.push_back(i);
				break;
			}
	}
	for (const auto& track : secondary_)
		if (track.bone >= stance_.size())
			throw std::invalid_argument("Secondary track names a bone outside the skeleton");
}
const std::vector<LocalPose>& IdlePose::Evaluate(double dt)
{
	time_ = std::fmod(time_ + dt, duration_);
	output_ = stance_;
	for (const auto& track : secondary_)
	{
		const auto value = track.channel.Sample(time_);
		auto& pose = output_[track.bone];
		if (track.rotation)
		{
			auto q = NormalizeQuaternion({ value[0], value[1], value[2], value[3] });
			if (q[3] < 0)
				for (auto& v : q)
					v = -v;
			q = NormalizeQuaternion({ q[0] * kSecondaryMotionWeight, q[1] * kSecondaryMotionWeight, q[2] * kSecondaryMotionWeight,
				1 + (q[3] - 1) * kSecondaryMotionWeight });
			pose.rotation = NormalizeQuaternion(QuaternionProduct(pose.rotation, q));
		}
		else
			for (unsigned i = 0; i < 3; ++i)
				pose.position[i] += double(value[i]) * kSecondaryMotionWeight;
	}
	// The secondary motion has no scale channels; the stance keeps its scale.
	for (unsigned bone : rifleBones_)
		output_[bone] = rifle_[bone];
	skeleton_.ResetLocals(output_);
	const auto parent = Conjugate(Decompose(skeleton_.ParentComponent(spine_)).rotation);
	output_[spine_].rotation = NormalizeQuaternion(QuaternionProduct(parent, rifleSpine_));
	return output_;
}
IdlePose LoadIdlePose(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader reader(bytes);
	reader.ExpectHeader("BCRCOPY1", 1);
	if (reader.U32() != kPhysicalBoneCount)
		throw std::invalid_argument("Idle pose package does not cover the physical skeleton");
	std::vector<int> parents(kPhysicalBoneCount);
	for (auto& p : parents)
		p = reader.I32();
	const auto duration = reader.F64();
	const auto spine = reader.U32();
	auto stance = reader.LocalPoses(kPhysicalBoneCount), rifle = reader.LocalPoses(kPhysicalBoneCount);
	const auto tracks = reader.Count(kMaxSecondaryTracks);
	std::vector<IdleSecondaryTrack> secondary;
	secondary.reserve(tracks);
	for (unsigned i = 0; i < tracks; ++i)
	{
		const auto bone = reader.U32();
		const bool rotation = reader.Bool();
		AuthoredChannelData data;
		data.quaternion = rotation;
		data.width = rotation ? 4 : 3;
		data.step = reader.Bool();
		const auto keys = reader.Count(kMaxKeysPerTrack);
		data.times.resize(keys);
		data.values.resize(keys * data.width);
		for (auto& v : data.times)
			v = reader.F32();
		for (auto& v : data.values)
			v = reader.F32();
		secondary.push_back({ bone, rotation, AuthoredChannel(std::move(data)) });
	}
	if (!reader.Done())
		throw std::invalid_argument("Idle pose package has trailing bytes");
	return IdlePose(std::move(parents), std::move(stance), std::move(rifle), std::move(secondary), duration, spine);
}
}

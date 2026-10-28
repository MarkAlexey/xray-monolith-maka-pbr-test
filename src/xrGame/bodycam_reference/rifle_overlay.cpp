#include "rifle_overlay.h"
#include "asset_reader.h"
#include <stdexcept>
namespace BodycamReference
{
std::vector<Matrix> RiflePoseComponents(const std::vector<LocalPose>& pose, const std::vector<RifleTrack>& tracks)
{
	std::vector<Matrix> out;
	out.reserve(pose.size());
	for (unsigned i = 0; i < pose.size(); ++i)
	{
		auto local = pose[i];
		local.rotation = NormalizeQuaternion(local.rotation);
		auto matrix = Compose(local);
		if (tracks[i].parent >= 0)
			matrix = Multiply(out[tracks[i].parent], matrix);
		out.push_back(matrix);
	}
	return out;
}
namespace
{
int Find(const std::vector<RifleTrack>& tracks, const std::string& name)
{
	for (unsigned i = 0; i < tracks.size(); ++i)
		if (LowerAscii(tracks[i].name) == name)
			return int(i);
	return -1;
}
}
PackedPose GltfToPackedRiflePose(const std::vector<LocalPose>& pose)
{
	PackedPose out;
	out.reserve(pose.size());
	for (const auto& t : pose)
		out.push_back({ -t.rotation[0], -t.rotation[2], -t.rotation[1], t.rotation[3], t.position[0] * kCentimetresPerMetre,
			t.position[2] * kCentimetresPerMetre, t.position[1] * kCentimetresPerMetre, 0, t.scale[0], t.scale[2], t.scale[1], 0 });
	return out;
}
std::vector<LocalPose> PackedToGltfRiflePose(const PackedPose& pose)
{
	std::vector<LocalPose> out;
	out.reserve(pose.size());
	for (const auto& t : pose)
		out.push_back({ { t[4] * kMetresPerCentimetre, t[6] * kMetresPerCentimetre, t[5] * kMetresPerCentimetre },
			{ -t[0], -t[2], -t[1], t[3] }, { t[8], t[10], t[9] } });
	return out;
}
RifleOverlayGun::RifleOverlayGun(std::vector<RifleTrack> tracks, const std::vector<LocalPose>& cclamp) : tracks_(std::move(tracks))
{
	const int hand = Find(tracks_, "hand_r"), gun = Find(tracks_, "ik_hand_gun");
	if (hand < 0 || gun < 0)
		throw std::invalid_argument("Rifle overlay needs hand_r and ik_hand_gun tracks");
	hand_ = unsigned(hand);
	gun_ = unsigned(gun);
	rhs_ = Find(tracks_, "vb rhs_ik_hand_gun");
	lhs_ = Find(tracks_, "vb lhs_ik_hand_gun");
	const auto reference = RiflePoseComponents(cclamp, tracks_);
	gunFromRight_ = Multiply(InverseMatrix(reference[hand_]), reference[gun_]);
	const int spine = Find(tracks_, "spine_01");
	for (unsigned i = 0; i < tracks_.size(); ++i)
	{
		parents_.push_back(tracks_[i].parent);
		float weight = 0;
		for (int p = int(i); p >= 0; p = tracks_[p].parent)
			if (p == spine || p == gun)
			{
				weight = 1;
				break;
			}
		mask_.push_back(weight);
	}
	if (spine < 0 || rhs_ < 0 || lhs_ < 0 || tracks_[rhs_].parent < 0 || tracks_[lhs_].parent < 0)
		throw std::invalid_argument("Rifle overlay needs spine_01 and both VB ik_hand_gun helper tracks with parents");
}
std::vector<LocalPose> RifleOverlayGun::AttachGunToHand(const std::vector<LocalPose>& pose) const
{
	const auto components = RiflePoseComponents(pose, tracks_);
	auto gun = Multiply(components[hand_], gunFromRight_);
	const int parent = tracks_[gun_].parent;
	if (parent >= 0)
		gun = Multiply(InverseMatrix(components[parent]), gun);
	auto out = pose;
	out[gun_] = Decompose(gun);
	out[gun_].scale = pose[gun_].scale;
	return out;
}
void RifleOverlayGun::CaptureHelperOffsets(std::vector<LocalPose>& pose)
{
	const auto components = RiflePoseComponents(pose, tracks_);
	const auto gun = Decompose(components[gun_]);
	for (int index : { rhs_, lhs_ })
	{
		auto helper = Decompose(components[index]);
		helper.position = gun.position;
		helper.rotation = gun.rotation;
		const auto local = Multiply(InverseMatrix(components[tracks_[index].parent]), Compose(helper));
		if (index == rhs_)
			montageGunFromRight_ = local;
		pose[index] = Decompose(local);
	}
}
Matrix RifleOverlayGun::GunFromHand(const Matrix& hand, const Matrix& target, const Matrix* carried) const
{
	const auto scale = Decompose(target).scale;
	auto out = Decompose(carried ? *carried : Multiply(hand, montageGunFromRight_ ? *montageGunFromRight_ : gunFromRight_));
	out.scale = scale;
	return Compose(out);
}
std::vector<LocalPose> RifleOverlayGun::ApplyMontageMask(const std::vector<LocalPose>& base, const std::vector<LocalPose>& child) const
{
	return PackedToGltfRiflePose(BlendStopMeshBinary(GltfToPackedRiflePose(base), GltfToPackedRiflePose(child), parents_, mask_));
}
}

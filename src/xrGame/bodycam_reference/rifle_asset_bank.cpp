#include "rifle_asset_bank.h"
#include "asset_reader.h"
#include <set>
namespace BodycamReference
{
namespace
{
constexpr unsigned kMaxKeys = 4096, kMaxCurves = 256;
// SHA-256 of the rifle step-key export the package was built from.
constexpr std::string_view kRifleKeySource = "a009d98b7603d57054b1237cfda5ca488a8f30fded3ebf2265e71f6c4b76f178";
}
RifleAssetBank RifleAssetBank::Load(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCRRIFL2", 2);
	if (r.String() != kRifleKeySource)
		throw std::invalid_argument("Rifle package was built from other step keys");
	RifleAssetBank out;
	const double duration = r.F64();
	if (r.U32() != kSkeletonBoneCount)
		throw std::invalid_argument("Rifle package does not have 89 tracks");
	std::vector<StepPoseTrack> keys(kSkeletonBoneCount);
	std::set<std::string> names;
	bool spine = false;
	for (unsigned i = 0; i < kSkeletonBoneCount; ++i)
	{
		RifleTrack track{ r.String(), r.I32() };
		if (track.name.empty() || !names.insert(track.name).second || track.parent < -1 || track.parent >= int(i))
			throw std::invalid_argument("Rifle track is unnamed, repeats a name or precedes its parent");
		if (track.name == "spine_01")
		{
			out.spine_ = i;
			spine = true;
		}
		out.tracks_.push_back(std::move(track));
		auto& k = keys[i];
		k.positions.resize(r.Count(kMaxKeys));
		for (auto& v : k.positions)
			for (auto& x : v)
				x = r.F64();
		k.rotations.resize(r.Count(kMaxKeys));
		for (auto& v : k.rotations)
			for (auto& x : v)
				x = r.F64();
		k.scales.resize(r.Count(kMaxKeys));
		for (auto& v : k.scales)
			for (auto& x : v)
				x = r.F64();
	}
	if (!spine)
		throw std::invalid_argument("Rifle package has no spine_01 track");
	const auto reference = r.Pose();
	std::vector<Vector> translations;
	for (const auto& p : reference)
		translations.push_back(p.p);
	std::vector<int> modes(r.Count(kTranslationModeCount));
	for (auto& m : modes)
		m = r.I32();
	out.retarget_.emplace(translations, modes);
	out.step_.emplace(duration, std::move(keys));
	out.curves_.resize(r.Count(kMaxCurves));
	for (auto& c : out.curves_)
	{
		c.name = r.String();
		c.constantBefore = r.Bool();
		c.constantAfter = r.Bool();
		c.keys.resize(r.Count(kMaxKeys));
		for (auto& k : c.keys)
		{
			k.time = r.F32();
			k.value = r.F32();
			k.mode = r.Count(2);
		}
	}
	const auto cclamp = r.Pose();
	if (cclamp.size() != kSkeletonBoneCount)
		throw std::invalid_argument("Rifle C-clamp pose does not have 89 bones");
	for (const auto& p : cclamp)
		out.cclamp_.push_back({ p.p, p.q, p.s });
	const auto aim = r.Pose();
	if (aim.size() != kPhysicalBoneCount)
		throw std::invalid_argument("Rifle aim pose does not have 68 bones");
	for (const auto& p : aim)
		out.aim_.push_back({ p.p, p.q, p.s });
	if (!r.Done())
		throw std::invalid_argument("Trailing rifle asset data");
	// Building the poses checks the curve keys at the sample times and the bone names.
	out.CreateLocal();
	out.CreateReference();
	return out;
}
RifleLocalPose RifleAssetBank::CreateLocal() const
{
	std::vector<int> parents;
	for (const auto& t : tracks_)
		parents.push_back(t.parent);
	return RifleLocalPose(std::move(parents), spine_, cclamp_, *step_, *retarget_, curves_);
}
PrimaryRiflePose RifleAssetBank::CreatePrimary(RegionalManifest manifest, PackedPose base) const
{
	return PrimaryRiflePose(tracks_, CreateLocal(), RetargetedCclamp(), std::move(manifest), std::move(base));
}
RifleReferencePose RifleAssetBank::CreateReference() const
{
	return RifleReferencePose(
		{ tracks_.begin(), tracks_.begin() + kPhysicalBoneCount }, { cclamp_.begin(), cclamp_.begin() + kPhysicalBoneCount }, aim_);
}
ViewmodelPoseGraph RifleAssetBank::CreateViewmodel() const
{
	std::map<std::string, unsigned> bones;
	std::vector<int> parents;
	for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
	{
		bones.emplace(tracks_[i].name, i);
		parents.push_back(tracks_[i].parent);
	}
	ViewmodelBinding binding;
	binding.right = { bones.at("upperarm_r"), bones.at("lowerarm_r"), bones.at("hand_r") };
	binding.left = { bones.at("upperarm_l"), bones.at("lowerarm_l"), bones.at("hand_l") };
	binding.spine = { { bones.at("spine_01"), bones.at("spine_02"), bones.at("spine_03") }, bones.at("neck_01"), bones.at("head") };
	binding.pelvis = bones.at("pelvis");
	return ViewmodelPoseGraph(std::move(parents), { cclamp_.begin(), cclamp_.begin() + kPhysicalBoneCount }, binding);
}
}

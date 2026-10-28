#include "regional_pose.h"
#include "additive_pose.h"
#include "constants.h"
#include "transform.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
// Narrows to float and clamps to [0, 1].
float Saturate(double value)
{
	return std::max(0.f, std::min(float(value), 1.f));
}
Quaternion RotationOf(const PackedTransform& t)
{
	return { t[0], t[1], t[2], t[3] };
}
PackedPose MeshPose(PackedPose pose, const std::vector<int>& parents)
{
	for (unsigned i = 0; i < pose.size(); ++i)
	{
		const int p = parents[i];
		const auto q = p < 0 ? StopMeshRoot(RotationOf(pose[i])) : StopMeshCompose(RotationOf(pose[i]), RotationOf(pose[p]));
		std::copy(q.begin(), q.end(), pose[i].begin());
	}
	return pose;
}
PackedPose MeshToLocalPose(const PackedPose& mesh, const std::vector<int>& parents)
{
	auto pose = mesh;
	for (unsigned i = 0; i < pose.size(); ++i)
		if (parents[i] >= 0)
		{
			const auto q = StopMeshToLocal(RotationOf(mesh[i]), RotationOf(mesh[parents[i]]));
			std::copy(q.begin(), q.end(), pose[i].begin());
		}
	return pose;
}
PackedPose Mix(const PackedPose& a, const PackedPose& b, const std::vector<float>& weights)
{
	PackedPose out;
	out.reserve(a.size());
	for (unsigned i = 0; i < a.size(); ++i)
	{
		const float alpha = weights[i];
		out.push_back(alpha == 0 ? a[i] : alpha == 1 ? b[i] : TransitionPackedTransform(a[i], b[i], { 1.f - alpha, alpha }));
	}
	return NormalizePackedPose(std::move(out));
}
}
RegionalPose::RegionalPose(RegionalManifest manifest, std::vector<int> parents, std::vector<unsigned> mapping)
	: manifest_(std::move(manifest)), parents_(std::move(parents))
{
	const auto count = parents_.size();
	if ((count != kPhysicalBoneCount && count != kSkeletonBoneCount) || mapping.size() != count || parents_[0] != -1 ||
		manifest_.standingAsset != "ALS_N_Pose")
		throw std::invalid_argument("Regional pose needs 68 or 89 bones, a mapping for each and the ALS_N_Pose standing asset");
	for (unsigned i = 0; i < count; ++i)
		if (parents_[i] < -1 || parents_[i] >= int(i) ||
			mapping[i] != (i < kPhysicalBoneCount ? i : i + (kSourceBoneCount - kSkeletonBoneCount)))
			throw std::invalid_argument("Regional bone precedes its parent or does not map to its source bone");
	std::set<std::string> names;
	const auto validateValue = [&](const RegionalValue& v)
	{
		if (!v.property.empty() && !manifest_.propertyInputs.count(v.property) && v.property != "Arm_L_MS" && v.property != "Arm_R_MS")
			throw std::invalid_argument("Regional weight reads an unknown property");
	};
	for (const auto& region : manifest_.regions)
	{
		if (!names.insert(region.name).second)
			throw std::invalid_argument("Duplicate region name");
		validateValue(region.additiveAlpha);
	}
	if (!names.count(manifest_.assemblyBase))
		throw std::invalid_argument("Regional assembly base is not a region");
	for (const auto& layer : manifest_.assembly)
	{
		if (layer.meshScale || layer.weights.empty())
			throw std::invalid_argument("Regional assembly layer blends scale in mesh space or has no weights");
		for (const auto& weight : layer.weights)
			validateValue(weight);
		std::map<unsigned, RegionalMaskEntry> sparse;
		for (const auto& entry : layer.mask)
		{
			if (entry.sourceIndex >= layer.weights.size())
				throw std::invalid_argument("Regional mask entry refers to a missing weight");
			sparse[entry.skeletonIndex] = entry;
		}
		std::vector<RegionalMaskEntry> mask;
		for (auto index : mapping)
		{
			const auto it = sparse.find(index);
			mask.push_back(it == sparse.end() ? RegionalMaskEntry{} : it->second);
		}
		masks_.push_back(std::move(mask));
	}
}
RegionalOutput RegionalPose::Evaluate(const PackedPose& baseLayer, const PackedPose& basePose, const PackedPose& overlay,
	const NamedCurves& curves, const RegionalStage& stage) const
{
	const auto count = parents_.size();
	const auto curve = [&](const std::string& name)
	{
		const auto it = std::find_if(curves.begin(), curves.end(), [&](const auto& e) { return e.name == name; });
		return it == curves.end() ? 0.f : it->value;
	};
	RegionalOutput out;
	for (const auto& entry : manifest_.propertyInputs)
		out.properties[entry.first] = curve(entry.second);
	for (const auto* side : { "L", "R" })
	{
		const std::string prefix = std::string("Arm_") + side;
		out.properties[prefix + "_MS"] = 1 - std::floor(out.properties[prefix + "_LS"]);
	}
	const auto resolve = [&](const RegionalValue& v) { return Saturate(v.property.empty() ? v.constant : out.properties.at(v.property)); };
	std::array<std::optional<PackedPose>, 2> deltas, overlays;
	std::map<std::string, PackedPose> regions;
	for (const auto& region : manifest_.regions)
	{
		const float weight = Saturate(curve(region.alphaCurve)), alpha = resolve(region.additiveAlpha);
		PackedPose result = baseLayer;
		if (weight > kBlendEpsilon)
		{
			PackedPose adjusted = overlay;
			const unsigned space = region.mesh ? 1 : 0;
			if (alpha > kBlendEpsilon)
			{
				if (!deltas[space])
				{
					const auto a = region.mesh ? MeshPose(baseLayer, parents_) : baseLayer,
							   b = region.mesh ? MeshPose(basePose, parents_) : basePose;
					PackedPose delta;
					delta.reserve(count);
					for (unsigned i = 0; i < count; ++i)
					{
						const auto& t = a[i];
						const auto& r = b[i];
						const auto q = StopMeshCompose(Conjugate(RotationOf(r)), RotationOf(t));
						for (unsigned k = 8; k < 11; ++k)
							if (std::abs(r[k]) < 1e-8)
								throw std::invalid_argument("Regional reference pose has a zero scale");
						delta.push_back({ q[0], q[1], q[2], q[3], t[4] - r[4], t[5] - r[5], t[6] - r[6], 0, t[8] / r[8] - 1,
							t[9] / r[9] - 1, t[10] / r[10] - 1, 0 });
					}
					deltas[space] = std::move(delta);
					overlays[space] = region.mesh ? MeshPose(overlay, parents_) : overlay;
				}
				adjusted = EvaluateLocalAdditive(
					alpha, [&] { return PoseCurveBundle{ *overlays[space], {} }; },
					[&]
					{
						return PoseCurveBundle{ *deltas[space], {} };
					}).pose;
				if (region.mesh)
					adjusted = MeshToLocalPose(adjusted, parents_);
			}
			result = 1.f - weight <= kBlendEpsilon ? std::move(adjusted) : Mix(baseLayer, adjusted, std::vector<float>(count, weight));
		}
		if (stage)
			stage("region:" + region.name, result);
		regions.emplace(region.name, std::move(result));
	}
	out.pose = regions.at(manifest_.assemblyBase);
	for (unsigned i = 0; i < manifest_.assembly.size(); ++i)
	{
		const auto& layer = manifest_.assembly[i];
		std::vector<float> weights;
		weights.reserve(count);
		for (const auto& entry : masks_[i])
			weights.push_back(Saturate(float(entry.weight) * resolve(layer.weights[entry.sourceIndex])));
		if (std::none_of(weights.begin(), weights.end(), [](float w) { return w > kBlendEpsilon; }))
			continue;
		const auto& child = layer.input == "Overlay" ? overlay : regions.at(layer.input);
		if (layer.meshRotation)
		{
			if (std::all_of(weights.begin(), weights.end(), [](float w) { return w == 0 || w == 1; }))
				out.pose = BlendStopMeshBinary(out.pose, child, parents_, weights);
			else
				out.pose = MeshToLocalPose(Mix(MeshPose(out.pose, parents_), MeshPose(child, parents_), weights), parents_);
		}
		else
			out.pose = Mix(out.pose, child, weights);
		if (stage)
			stage("assembly:" + std::to_string(layer.node), out.pose);
	}
	return out;
}
}

#include "skeleton_bridge.h"
#include <stdexcept>
namespace BodycamReference
{
SkeletonBridge::Side SkeletonBridge::Describe(const SkeletonPose& pose)
{
	Side side;
	side.parents.resize(pose.Size());
	side.other.assign(pose.Size(), -1);
	side.offsets.resize(pose.Size());
	side.order = pose.Order();
	for (unsigned i = 0; i < pose.Size(); ++i)
		side.parents[i] = pose.Parent(i);
	return side;
}
SkeletonBridge::SkeletonBridge(const SkeletonPose& reference, const SkeletonPose& target, const std::vector<BoneCorrespondence>& pairs)
	: reference_(Describe(reference)), target_(Describe(target))
{
	if (pairs.empty())
		throw std::invalid_argument("Bone correspondences required");
	for (const auto& pair : pairs)
	{
		if (pair.reference >= reference.Size() || pair.target >= target.Size() || reference_.other[pair.reference] >= 0 ||
			target_.other[pair.target] >= 0)
			throw std::invalid_argument("Ambiguous calibrated bone correspondence");
		reference_.other[pair.reference] = int(pair.target);
		target_.other[pair.target] = int(pair.reference);
		reference_.offsets[pair.reference] = Multiply(InverseAffine(target.Component(pair.target)), reference.Component(pair.reference));
		target_.offsets[pair.target] = Multiply(InverseAffine(reference.Component(pair.reference)), target.Component(pair.target));
	}
}
void SkeletonBridge::Validate(const Side& input, const Side& output, const SkeletonPose& from, const SkeletonPose& base)
{
	if (from.Size() != input.parents.size() || base.Size() != output.parents.size())
		throw std::invalid_argument("Skeleton size differs from the calibration");
	for (unsigned i = 0; i < from.Size(); ++i)
		if (from.Parent(i) != input.parents[i])
			throw std::invalid_argument("Source hierarchy differs from the calibration");
	for (unsigned i = 0; i < base.Size(); ++i)
		if (base.Parent(i) != output.parents[i])
			throw std::invalid_argument("Target hierarchy differs from the calibration");
}
std::vector<LocalPose> SkeletonBridge::Transfer(const Side& input, const Side& output, const SkeletonPose& from, const SkeletonPose& base)
{
	Validate(input, output, from, base);
	std::vector<LocalPose> locals(base.Size());
	std::vector<Matrix> components(base.Size());
	for (unsigned i : output.order)
	{
		const int parent = output.parents[i], other = output.other[i];
		locals[i] = base.Local(i);
		if (other >= 0)
		{
			const auto desired = Multiply(from.Component(unsigned(other)), output.offsets[i]);
			locals[i] = Decompose(parent < 0 ? desired : Multiply(InverseAffine(components[parent]), desired));
		}
		const auto local = Compose(locals[i]);
		components[i] = parent < 0 ? local : Multiply(components[parent], local);
	}
	return locals;
}
std::vector<LocalPose> SkeletonBridge::Import(const SkeletonPose& authored, const SkeletonPose& base) const
{
	return Transfer(target_, reference_, authored, base);
}
std::vector<LocalPose> SkeletonBridge::Export(const SkeletonPose& solved, const SkeletonPose& authored) const
{
	return Transfer(reference_, target_, solved, authored);
}
std::vector<Matrix> SkeletonBridge::ExportComponents(const SkeletonPose& solved, const SkeletonPose& authored) const
{
	Validate(reference_, target_, solved, authored);
	std::vector<Matrix> components(authored.Size());
	for (unsigned i : target_.order)
	{
		const int other = target_.other[i], parent = target_.parents[i];
		if (other >= 0)
			components[i] = Multiply(solved.Component(unsigned(other)), target_.offsets[i]);
		else
		{
			const auto local = Compose(authored.Local(i));
			components[i] = parent < 0 ? local : Multiply(components[parent], local);
		}
	}
	return components;
}
}

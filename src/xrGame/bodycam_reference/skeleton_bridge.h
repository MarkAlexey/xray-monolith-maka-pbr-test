#pragma once
#include "skeleton_pose.h"
namespace BodycamReference
{
struct BoneCorrespondence
{
	unsigned reference, target;
};
// Both rigs are already in the same coordinate basis and units. Calibration
// poses must describe the same authored stance, not an arbitrary current frame.
class SkeletonBridge
{
public:
	SkeletonBridge(const SkeletonPose& referenceCalibration, const SkeletonPose& targetCalibration, const std::vector<BoneCorrespondence>&);
	std::vector<LocalPose> Import(const SkeletonPose& targetAuthored, const SkeletonPose& referenceBase) const;
	std::vector<LocalPose> Export(const SkeletonPose& referenceSolved, const SkeletonPose& targetAuthored) const;
	std::vector<Matrix> ExportComponents(const SkeletonPose& referenceSolved, const SkeletonPose& targetAuthored) const;

private:
	struct Side
	{
		std::vector<int> parents, other;
		std::vector<unsigned> order;
		std::vector<Matrix> offsets;
	};
	static Side Describe(const SkeletonPose&);
	static void Validate(const Side&, const Side&, const SkeletonPose&, const SkeletonPose&);
	static std::vector<LocalPose> Transfer(const Side& input, const Side& output, const SkeletonPose& from, const SkeletonPose& base);
	Side reference_, target_;
};
}

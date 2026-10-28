#pragma once
#include "animation_blend.h"
#include "step_pose.h"
#include <array>
namespace BodycamReference
{
struct RiflePoseSample
{
	std::vector<LocalPose> pose;
	AnimationCurves curves;
};
struct RifleFoldedKey
{
	float time, value;
	unsigned mode; // interpolation to the next key: 0 constant, 1 linear, 2 cubic
};
struct RifleFoldedCurve
{
	std::string name;
	bool constantBefore = false, constantAfter = false;
	std::vector<RifleFoldedKey> keys;
};
struct RifleLocalResult
{
	std::vector<LocalPose> pose, branchA;
	AnimationCurves curves, curvesA, curvesB;
	float gait, clf, sprint;
};
// Step-pose times sampled for the two gait poses, the BasePose_CLF pose and the sprint pose.
constexpr std::array<float, 4> kRifleSampleTimes{ .16f, .18f, .28f, .05f };
class RifleLocalPose
{
public:
	// samples holds the retargeted poses at kRifleSampleTimes.
	RifleLocalPose(std::vector<int> parents, unsigned spine, std::vector<LocalPose> cclamp, std::array<RiflePoseSample, 4> samples);
	RifleLocalPose(std::vector<int> parents, unsigned spine, std::vector<LocalPose> cclamp, const MovingRifleStepPose&,
		const RifleTranslationRetarget&, const std::vector<RifleFoldedCurve>&);
	RifleLocalResult Evaluate(double weightGait, double basePoseClf, double sprintWeight) const;

private:
	std::vector<Quaternion> Rotations(const std::vector<LocalPose>&) const;
	std::vector<int> parents_;
	std::vector<bool> mask_;
	std::vector<LocalPose> cclamp_;
	std::vector<Quaternion> rotations_;
	std::array<RiflePoseSample, 4> samples_;
};
}

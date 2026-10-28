#pragma once
#include "pose.h"
#include <vector>
namespace BodycamReference
{
struct SequenceFrameTime
{
	int frame;
	float subframe;
};
struct SequenceFramePosition
{
	SequenceFrameTime time;
	int durationFrames;
	float relative;
};
SequenceFrameTime FrameTime(double seconds, int numerator, int denominator);
SequenceFramePosition SequenceFrameContext(double seconds, double length, int numerator, int denominator);
unsigned StepKeyIndex(double relative, unsigned count);
struct StepPoseTrack
{
	std::vector<Vector> positions, scales;
	std::vector<Quaternion> rotations;
};
class MovingRifleStepPose
{
public:
	MovingRifleStepPose(double duration, std::vector<StepPoseTrack> tracks);
	std::vector<LocalPose> SampleRelative(double relative) const;
	std::vector<LocalPose> SampleTime(double seconds, SequenceFramePosition* context = nullptr) const;

private:
	double duration_;
	std::vector<StepPoseTrack> tracks_;
};
class RifleTranslationRetarget
{
public:
	RifleTranslationRetarget(std::vector<Vector> targetReferenceCm, std::vector<int> translationModes);
	std::vector<LocalPose> Apply(std::vector<LocalPose> pose) const;

private:
	std::vector<unsigned> indices_;
	std::vector<Vector> translations_;
};
}

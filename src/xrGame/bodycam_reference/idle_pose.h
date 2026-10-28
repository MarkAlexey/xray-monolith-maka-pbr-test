#pragma once
#include "authored_channel.h"
#include "skeleton_pose.h"
#include <cstdint>
namespace BodycamReference
{
struct IdleSecondaryTrack
{
	unsigned bone;
	bool rotation;
	AuthoredChannel channel;
};
// The looping idle pose: the stance with its secondary motion, and the rifle pose from the spine up.
class IdlePose
{
public:
	IdlePose(std::vector<int> parents, std::vector<LocalPose> stance, std::vector<LocalPose> rifle, std::vector<IdleSecondaryTrack>,
		double duration, unsigned spine);
	const std::vector<LocalPose>& Evaluate(double worldSeconds);
	double Time() const { return time_; }
	void ResetTime() { time_ = 0; }

private:
	SkeletonPose skeleton_;
	std::vector<LocalPose> stance_, rifle_, output_;
	std::vector<IdleSecondaryTrack> secondary_;
	std::vector<unsigned> rifleBones_;
	Quaternion rifleSpine_;
	double duration_, time_ = 0;
	unsigned spine_;
};
IdlePose LoadIdlePose(const std::vector<std::uint8_t>&);
}

#pragma once
#include "montage_blend.h"
namespace BodycamReference
{
constexpr double kFireBlendSeconds = .15;
struct FireMontageInstance
{
	unsigned id;
	float time = 0;
	MontageBlend blend{ 0, 1, kFireBlendSeconds };
};
struct FireMontageSample
{
	unsigned id;
	float time, weight, rawWeight;
	bool retiring;
};
// Overlapping fire montage instances and their additive slot weights.
class FireMontages
{
public:
	unsigned Play();
	void StopAll(double duration);
	const std::vector<FireMontageSample>& Update(double dt);

private:
	static void Stop(FireMontageInstance&, double duration);
	unsigned nextId_ = 1;
	std::vector<FireMontageInstance> instances_;
	std::vector<FireMontageSample> samples_;
};
}

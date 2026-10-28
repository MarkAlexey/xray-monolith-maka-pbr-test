#pragma once
#include <vector>

namespace BodycamReference
{
enum class MontageBlendShape
{
	Linear,
	Sinusoidal,
	Hermite
};
struct MontageBlendState
{
	float value, begin, end, duration, alpha, remaining, target;
};
float SinusoidalMontageAlpha(float alpha);
float HermiteMontageAlpha(float alpha);
class MontageBlend
{
public:
	MontageBlend(double begin, double end, double duration, MontageBlendShape shape = MontageBlendShape::Linear);
	void Reset(double end, double duration);
	float Update(double deltaTime);
	const MontageBlendState& State() const { return state_; }
	bool Finished() const { return state_.value == state_.target; }

private:
	MontageBlendState state_{};
	MontageBlendShape shape_;
};
// The factor each slot weight is multiplied by when the slot weights add up to totalNodeWeight.
float AdditiveSlotScale(double totalNodeWeight);
std::vector<float> AdditiveSlotWeights(const std::vector<float>& weights, double totalNodeWeight);
}

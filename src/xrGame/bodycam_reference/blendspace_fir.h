#pragma once
#include <vector>
namespace BodycamReference
{
enum class FirType
{
	Average,
	Linear,
	Cubic,
	EaseInOut
};
struct FirSample
{
	float input = 0, time = 0;
};
class BlendSpaceFir
{
public:
	BlendSpaceFir(float windowDuration, FirType type);
	void Reset(float lastOutput = 0);
	float Update(float input, float deltaTime);
	float CurrentTime() const { return currentTime_; }
	unsigned StackIndex() const { return stackIndex_; }
	const std::vector<FirSample>& Samples() const { return data_; }

private:
	float Weight(float age) const;
	float window_, lastOutput_ = 0, currentTime_ = 0;
	FirType type_;
	unsigned stackIndex_ = 0;
	std::vector<FirSample> data_;
};
}

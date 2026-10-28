#pragma once
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace BodycamReference
{
float CubicRangeAlpha(float input);
class CubicRangeBlend
{
public:
	explicit CubicRangeBlend(std::function<float(float)> mapping = {}) : mapping_(std::move(mapping)) {}
	float MapAlpha(float input) const { return mapping_ ? mapping_(input) : CubicRangeAlpha(input); }
	void SetRange(float begin, float end);
	void SetDuration(double duration);
	float Update(double deltaTime);
	bool Finished() const { return value == target; }
	float begin = 0, end = 0, value = 0, target = 0, alpha = 0, remaining = 0, duration = 0;

private:
	float Mapped(float input) const;
	bool rangeDirty_ = true, durationDirty_ = true, endpointDirty_ = true;
	std::function<float(float)> mapping_;
};
struct GaitChildUpdate
{
	unsigned child;
	float weight;
	bool activeBranch;
	bool outgoingZero = false;
};
struct GaitBlendTrace
{
	unsigned index;
	bool changed, normalized;
	float duration, sum;
	std::array<float, 2> weights, internalValues;
	std::vector<GaitChildUpdate> updates;
};
class GaitBlendList
{
public:
	enum class Policy
	{
		Gait,
		LeftHip,
		RightHip
	};
	explicit GaitBlendList(Policy policy = Policy::Gait) : policy_(policy) { Initialize(); }
	void Initialize();
	GaitBlendTrace Update(unsigned gait, double deltaTime, double parentWeight, bool activeBranch = true);
	const std::array<CubicRangeBlend, 2>& Blends() const { return blends_; }
	const std::array<float, 2>& Weights() const { return weights_; }

private:
	Policy policy_;
	int lastIndex_ = -1;
	float duration_ = 0;
	std::array<float, 2> weights_ = { 1, 0 };
	std::array<CubicRangeBlend, 2> blends_;
};
struct SprintMaskContext
{
	bool branchB;
	float weight;
};
std::vector<SprintMaskContext> DispatchSprintMask(double publishedMask, double parentWeight);
struct ForwardGaitContext
{
	float weight;
	bool activeBranch, viaMask;
	std::optional<unsigned> child;
	bool outgoingZero = false;
};
struct ForwardGaitTrace
{
	std::optional<GaitBlendTrace> gait;
	std::array<std::vector<ForwardGaitContext>, 2> queues;
	std::array<std::optional<ForwardGaitContext>, 2> winners;
};
class ForwardGaitDispatch
{
public:
	void InitializeMask(const std::function<void()>& initializeChildren);
	GaitBlendList& Gait() { return gait_; }
	const GaitBlendList& Gait() const { return gait_; }
	ForwardGaitTrace Update(unsigned gait, double dt, double parentWeight, double publishedMask, bool activeBranch = true);
	float MaskAlpha() const { return maskAlpha_; }
	const std::array<bool, 2>& MaskRelevance() const { return maskRelevance_; }

private:
	GaitBlendList gait_;
	float maskAlpha_ = 0;
	std::array<bool, 2> maskRelevance_ = { false, false };
};
}

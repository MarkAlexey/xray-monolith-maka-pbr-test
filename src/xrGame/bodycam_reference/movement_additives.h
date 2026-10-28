#pragma once
#include "additive_pose.h"
#include "animation_blend.h"
#include <map>
namespace BodycamReference
{
struct WalkingCurveKey
{
	float time, value;
};
struct WalkingCurve
{
	std::string name;
	std::vector<WalkingCurveKey> keys;
};
class MovementWalkCurves
{
public:
	explicit MovementWalkCurves(std::vector<WalkingCurve>);
	NamedCurves Sample(float time) const;

private:
	std::vector<WalkingCurve> curves_;
};
struct ProceduralAlphaState
{
	bool initialized = false;
	float cached = 0, raw = 1, actual = 0;
};
struct WalkingAdditiveState
{
	float time = 0, layerWeight = 0;
	std::uint64_t epoch = 0;
	bool layerRelevant = false, playerUpdated = false, machineRelevant = false, inFlight = false;
	std::map<unsigned, ProceduralAlphaState> alphas;
	NamedCurves published, consumed;
};
struct WalkingStressAdapter
{
	std::function<void(unsigned, float)> tick;
	std::function<NamedCurves(unsigned)> sample;
	std::function<std::array<float, 6>(unsigned)> scales;
};
class MovementAdditivesWalk
{
public:
	explicit MovementAdditivesWalk(MovementWalkCurves, WalkingStressAdapter = {});
	void Update(double dt, bool aiming, const AnimationCurves& previousLayerInputs);
	NamedCurves Evaluate(const NamedCurves& primary);
	void SetStressAdapter(WalkingStressAdapter adapter) { stress_ = std::move(adapter); }
	// Changes only walking detail cadence/amplitude, never stress or action time.
	void SetLocomotionSpeed(double speedCm);
	const WalkingAdditiveState& State() const { return state_; }

private:
	void Visit(unsigned id, float weight, float dt, bool aiming, float gait, bool& machineUpdated);
	NamedCurves EvaluateNode(unsigned id, const NamedCurves& input) const;
	MovementWalkCurves curves_;
	WalkingStressAdapter stress_;
	WalkingAdditiveState state_;
	float locomotionRate_ = 1, locomotionWeight_ = 1, locomotionTarget_ = 1;
	bool hasLocomotionSpeed_ = false;
	std::array<TwoWayPoseState, 2> snapshots_{}; // Walking weight by Weight_Gait: [1] fades in over 0-1, [0] out over 1-2.
};
Matrix ApplyMovementAdditivesWalk(const Matrix&, const NamedCurves& consumed);
}

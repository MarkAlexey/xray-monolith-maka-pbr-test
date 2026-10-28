#pragma once
#include <cstdint>
#include <vector>
namespace BodycamReference
{
float InertialDecay(float displacement, float velocity, float time, float duration);
struct InertialRequest
{
	float duration;
	std::uint8_t flag, blendOption;
};
struct InertialEvent
{
	unsigned requestCount;
	bool activated;
	float subtractionDeficit;
	unsigned state;
	float elapsed, duration, accumulatedDelta;
	bool initializeOffsets, applyOffsets;
};
class InertialTiming
{
public:
	void Initialize();
	void Deactivate();
	void Update(double dt);
	bool Request(InertialRequest);
	InertialEvent BeginEvaluation(bool hasRecentHistory);
	void OffsetsInitialized();
	void FinishEvaluation() { accumulatedDelta = 0; }
	unsigned state = 0;
	float elapsed = 0, duration = 0, maximumDuration = 0, deficit = 0, accumulatedDelta = 0;

private:
	std::vector<InertialRequest> queue_;
};
struct InertialCurve
{
	unsigned name;
	float value;
	std::uint8_t flags;
};
struct InertialCurveDelta
{
	unsigned name;
	float current, displacement, velocity;
	std::uint8_t flags;
};
std::vector<InertialCurveDelta> BuildInertialCurves(
	const std::vector<InertialCurve>& current, const std::vector<InertialCurve>& recent, const std::vector<InertialCurve>& older, float dt);
std::vector<InertialCurve> ApplyInertialCurves(
	const std::vector<InertialCurve>& current, const std::vector<InertialCurveDelta>& delta, float time, float duration);
struct InertialCurveResult
{
	std::vector<InertialCurve> curves;
	InertialEvent event;
};
class InertialCurves
{
public:
	void Initialize();
	void Deactivate();
	void Update(double dt) { timing.Update(dt); }
	bool Request(InertialRequest r) { return timing.Request(r); }
	InertialCurveResult Evaluate(const std::vector<InertialCurve>& current, unsigned historyBoneCount);
	InertialTiming timing;

private:
	struct History
	{
		unsigned boneCount = 0;
		float dt = 0;
		std::vector<InertialCurve> curves;
	};
	History recent_, older_;
	std::vector<InertialCurveDelta> deltas_;
};
}

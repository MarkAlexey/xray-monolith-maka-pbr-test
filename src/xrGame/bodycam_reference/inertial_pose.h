#pragma once
#include "inertial.h"
#include "directional_pose_evaluate.h"
#include <map>
namespace BodycamReference
{
struct InertialVectorDelta
{
	float magnitude;
	std::array<float, 3> direction;
	float velocity;
};
struct InertialRotationDelta
{
	std::array<float, 3> axis;
	float angle, velocity;
};
struct InertialBoneDelta
{
	InertialVectorDelta translation, scale;
	InertialRotationDelta rotation;
};
struct InertialPoseDeltas
{
	std::vector<InertialBoneDelta> records;
	unsigned rootMode;
};
InertialVectorDelta InertialVectorDifference(
	std::array<double, 3> current, std::array<double, 3> recent, std::array<double, 3> older, float dt);
InertialRotationDelta InertialRotationDifference(Quaternion current, Quaternion recent, Quaternion older, float dt);
InertialPoseDeltas BuildInertialPose(const PackedPose& current, const PackedPose& recent, const PackedPose& older, float dt,
	Quaternion currentComponent, Quaternion recentComponent, Quaternion olderComponent);
PackedPose ApplyInertialPose(PackedPose, const std::vector<InertialBoneDelta>&, float time, float duration);
struct InertialPoseDiagnostic
{
	InertialEvent event{};
	unsigned rootMode = 0;
	double maximumTranslationOffsetCm = 0;
};
class MovementInertialReceiver
{
public:
	void Update(double dt, const std::vector<InertialRequest>&);
	PoseCurveBundle Evaluate(const PoseCurveBundle&, Quaternion component, bool recordDiagnostics = true);
	const InertialPoseDiagnostic& Diagnostic() const { return last_; }

private:
	struct History
	{
		PackedPose pose;
		Quaternion component;
		float dt;
	};
	std::map<std::string, unsigned> names_;
	std::vector<std::string> namesById_;
	InertialCurves curves_;
	std::optional<History> recent_, older_;
	std::vector<InertialBoneDelta> deltas_;
	unsigned rootMode_ = 0;
	InertialPoseDiagnostic last_;
};
}

#include "locomotion_frame.h"
namespace BodycamReference
{
LocomotionFrame::LocomotionFrame(const CyclePoseAssets& cycle, const UpperFrameAssets& upper, const PoseAssetBank& poses,
	GroundedLocomotionProperties properties, GroundedRotationProperties rotation)
	: upper_(cycle, upper, poses), properties_(std::move(properties)), rotation_(std::move(rotation))
{
}
LocomotionFrameResult LocomotionFrame::Update(
	double delta, const LocomotionEssential& essential, double aiming, std::optional<double> aimYawRate, bool recordDiagnostics)
{
	const float dt = float(delta);
	AnimationCurves prior;
	for (const auto& e : upper_.PublishedCurves())
		prior[e.name] = e.value;
	const auto gate = properties_.Update(dt, true, essential, prior);
	const auto rotations = rotation_.Update(gate.movementValuesUpdate, essential);
	rifle_.Update(essential.engineRifleGait.value_or(essential.gait), CurveValue(prior, "Weight_Gait"), aiming, dt);
	UpperFrameInput in;
	// The epoch counter wraps at 65535, not 65536.
	in.epoch = { unsigned(frame_ % 65535), frame_ };
	++frame_;
	in.dt = dt;
	in.essential = essential;
	in.properties = properties_.State();
	in.rotations = rotations;
	in.aimYawRate = aimYawRate;
	in.recordDiagnostics = recordDiagnostics;
	return { upper_.Update(in), in.properties, rotations, rifle_.GraphAlpha() };
}
void LocomotionFrame::PublishPrimaryCurves(NamedCurves curves)
{
	upper_.PublishPrimaryCurves(std::move(curves));
}
}

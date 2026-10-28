#include "motion_frame.h"
#include "constants.h"
#include "scalar_math.h"
#include <cmath>
#include <stdexcept>

namespace BodycamReference
{
MotionFrame::MotionFrame(RecoilRig& recoil, RegripMontages& regrip, Rotation initialControl, double horizontalFov)
	: recoil_(recoil), regrip_(regrip), horizontalFov_(horizontalFov)
{
	output_.control = initialControl;
	output_.consumedCurves = curves_.ReadInputs();
}
const MotionFrameOutput& MotionFrame::Begin(double application, bool aiming, bool ready, bool reloading)
{
	if (phase_ != Phase::Idle)
		throw std::logic_error("Motion frame began before the previous one completed");
	if (!std::isfinite(application) || application < 0)
		throw std::invalid_argument("Frame delta must be finite and not negative");
	phase_ = Phase::Input;
	aiming_ = aiming;
	ready_ = ready;
	output_.applicationSeconds = application;
	output_.worldSeconds = ClampFrameDelta(application);
	output_.worldTime += output_.worldSeconds;
	const double dt = output_.worldSeconds;
	if (ready)
	{
		regrip_.Events().Update(dt, reloading);
		output_.recoil = recoil_.PreUpdate(true, true);
		output_.consumedCurves = curves_.ReadInputs();
	}
	const auto input = mouse_.Consume();
	output_.mouse = history_.Process(input, application, horizontalFov_);
	look_ = ModifyRawLook(input.raw[0], input.raw[1], aiming);
	output_.modifiedLook = look_;
	const auto mouseControl = ControllerDeltas(look_[0], look_[1]);
	output_.control = ApplyControlDeltas(
		output_.control, { pendingController_.pitch + mouseControl.pitch, pendingController_.yaw + mouseControl.yaw, 0 });
	const auto next = recoil_.UpdateController(dt, output_.mouse.processed[0], output_.mouse.processed[1]);
	pendingController_ = ControllerDeltas(float(next[0]), float(next[1]));
	output_.magnifiedScale = adsSway_.Update(output_.control, dt, ready);
	output_.adsProgress = AdvanceAds(output_.adsProgress, aiming, dt);
	output_.adsAlpha = AdsCurve(output_.adsProgress);
	output_.elbowCm = elbow_.Update(aiming, dt, ready, GripPoseKind::None);
	return output_;
}
void MotionFrame::ObserveSightMode(bool magnified, bool enabled)
{
	if (phase_ != Phase::Idle)
		throw std::logic_error("Sight mode observed during a motion frame");
	// A switch between magnified and unmagnified sights requests the grip montage after 0.1 s.
	// The first observation is not a switch and draws no random numbers.
	if (enabled && sightMode_ && *sightMode_ != magnified)
		regrip_.Events().Request(.1);
	sightMode_ = magnified;
}
void MotionFrame::SetExternalControl(const Rotation& control)
{
	if (phase_ != Phase::Idle)
		throw std::logic_error("External control set during a motion frame");
	if (!std::isfinite(control.pitch) || !std::isfinite(control.yaw) || !std::isfinite(control.roll))
		throw std::invalid_argument("External control is not finite");
	output_.control = control;
}
void MotionFrame::SeedControl(const Rotation& control, bool aiming)
{
	SetExternalControl(control);
	camera_.SeedControl(control);
	adsSway_.SeedControl(control);
	output_.camera = camera_.State();
	output_.adsProgress = aiming ? 1.f : 0.f;
	output_.adsAlpha = AdsCurve(output_.adsProgress);
}
const MotionFrameOutput& MotionFrame::AfterMovement(const MotionMovement& move, bool magnified)
{
	if (phase_ != Phase::Input)
		throw std::logic_error("AfterMovement called before Begin");
	phase_ = Phase::Pose;
	const double forward = -std::sin(move.actorYawRadians) * move.velocityX + std::cos(move.actorYawRadians) * move.velocityZ;
	const double right = -std::cos(move.actorYawRadians) * move.velocityX - std::sin(move.actorYawRadians) * move.velocityZ;
	const double length = std::hypot(forward, right);
	const double strafe = length > 1e-4 ? right / length : 0, advance = length > 1e-4 ? forward / length : 0;
	output_.camera = camera_.Update({ output_.control, output_.worldSeconds, look_[0], look_[1], move.strafeInput,
		std::hypot(move.velocityX, move.velocityZ) * kCentimetresPerMetre, aiming_, move.leaningPressed, aiming_ ? 2 : 1 });
	const auto& camera = output_.camera;
	output_.lowerSwayCm = lower_.Update(camera.yawRate, aiming_, output_.worldSeconds, ready_);
	SpringInput input;
	const double positionStrafe = strafe * (1 - std::abs(camera.leanAlpha)) + (-camera.leanTarget) * std::abs(camera.leanAlpha);
	input.values = { camera.yawRate, camera.yawRate, camera.pitchRate, camera.pitchRate, camera.yawRate, positionStrafe, strafe, strafe,
		advance, advance };
	input.magnifiedScale = output_.magnifiedScale;
	input.dt = output_.worldSeconds;
	input.aiming = aiming_;
	input.magnified = magnified;
	input.adsAlpha = output_.adsAlpha;
	input.hasPublishedAlpha = true;
	input.actualLook = output_.consumedCurves.actualLook;
	input.actualMove = output_.consumedCurves.actualMove;
	output_.springs = springs_.Update(input);
	look_ = {};
	return output_;
}
void MotionFrame::CompletePose(const FpCurves& published, bool bonesValid, bool capture, bool evaluated)
{
	if (phase_ != Phase::Pose)
		throw std::logic_error("CompletePose called before AfterMovement");
	if (ready_)
	{
		if (evaluated)
			curves_.CompleteEvaluation(output_.adsAlpha, output_.adsAlpha, bonesValid, published);
		recoil_.Publish(output_.worldSeconds, aiming_, capture, curves_.ReadRecoilInput());
	}
	phase_ = Phase::Idle;
}
}

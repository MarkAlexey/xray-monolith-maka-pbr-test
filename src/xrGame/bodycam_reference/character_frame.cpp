#include "character_frame.h"
#include "constants.h"
#include <stdexcept>
#include <cmath>
namespace BodycamReference
{
CharacterFrame::CharacterFrame(
	MotionFrame& motion, GroundedMovement& movement, PrimaryFrame& primary, AimPointBlend& aimPoint, CameraEffects& effects)
	: motion_(motion), movement_(movement), primary_(primary), aimPoint_(aimPoint), effects_(effects)
{
}
const MotionFrameOutput& CharacterFrame::Begin(
	double delta, bool aiming, bool magnified, const CharacterMovementInput& input, const std::function<void()>& afterGrounded)
{
	if (phase_ != Phase::Idle)
		throw std::logic_error("Character frame began before the previous one completed");
	motion_.ObserveSightMode(magnified, input.sightRegrip);
	weaponSway_ = input.weaponSway;
	effects_.SetEnabled(input.cameraShake);
	primary_.SetStressEnabled(input.stressMotion);
	phase_ = Phase::Animation;
	magnified_ = magnified;
	const auto& motion = motion_.Begin(delta, aiming, true, input.reloading);
	GroundedInput step;
	step.dt = step.worldDelta = motion.worldSeconds;
	step.engineControl = motion.control;
	step.right = input.right;
	step.forward = input.forward;
	step.aiming = aiming;
	step.sprintRequested = input.sprintRequested;
	step.canAccelerate = input.canAccelerate;
	step.desiredGait = input.desiredGait;
	step.position = input.position;
	step.resolveMove = input.resolveMove;
	step.engineVelocity = input.engineVelocity;
	step.stance = input.stance;
	step.engineGaitPresentation = input.engineGaitPresentation;
	const auto movement = movement_.Step(step);
	if (afterGrounded)
		afterGrounded();
	primary_.Update(motion.worldSeconds, MakeLocomotionEssential(movement, motion.control), motion.adsProgress, aiming);
	MotionMovement camera;
	camera.actorYawRadians = movement.actorYaw;
	camera.velocityX = -movement.velocity.y / kCentimetresPerMetre;
	camera.velocityZ = movement.velocity.x / kCentimetresPerMetre;
	camera.strafeInput = input.right;
	const auto& result = motion_.AfterMovement(camera, magnified);
	// Ticked before the pose is evaluated so the shared random draws keep their order.
	MovementVibrationInput vibration;
	vibration.speed = std::hypot(camera.velocityX, camera.velocityZ) * kCentimetresPerMetre;
	vibration.yawRate = result.camera.yawRate;
	vibration.pitchRate = result.camera.pitchRate;
	effects_.Tick(result.worldSeconds, vibration, result.modifiedLook[0], result.modifiedLook[1]);
	return result;
}
CharacterPoseResult CharacterFrame::Evaluate(const std::vector<LocalPose>& authored, ViewmodelPoseInput input,
	const OverlayMontages& montages, const ActionCurvePublication& actions)
{
	return EvaluateInPlace(authored, input, montages, actions);
}
CharacterPoseResult CharacterFrame::EvaluateInPlace(const std::vector<LocalPose>& authored, ViewmodelPoseInput& input,
	const OverlayMontages& montages, const ActionCurvePublication& actions)
{
	if (phase_ != Phase::Animation)
		throw std::logic_error("Character pose evaluated before Begin");
	const auto& motion = motion_.Output();
	const auto aim = movement_.AimingAngle();
	input.recoil = motion.recoil.pose;
	input.springs = motion.springs;
	input.adsAlpha = motion.adsAlpha;
	input.elbowCm = motion.elbowCm;
	input.lowerSwayCm = motion.lowerSwayCm;
	input.leanAlpha = motion.camera.leanAlpha;
	if (!weaponSway_)
	{
		input.springs = {};
		input.elbowCm = {};
		input.lowerSwayCm = 0;
	}
	input.cachedAimPitch = aim.pitch;
	input.cachedAimYaw = aim.yaw;
	input.magnified = magnified_;
	input.aimPointLocal = aimPoint_.Update(motion.worldSeconds, magnified_);
	auto physical = primary_.Apply(authored, montages, actions).physical;
	auto targets = primary_.EvaluateViewmodelInPlace(physical, input);
	finalSocket_ = Translation(primary_.CameraPoint(physical));
	finalGun_ = targets.gun;
	finalHands_ = primary_.FinalHandPoints();
	phase_ = Phase::Presentation;
	return { std::move(physical), std::move(targets) };
}
CharacterPoseResult CharacterFrame::Evaluate(
	IdlePose& idle, ViewmodelPoseInput input, const OverlayMontages& montages, const ActionCurvePublication& actions)
{
	return Evaluate(idle.Evaluate(motion_.Output().worldSeconds), std::move(input), montages, actions);
}
void CharacterFrame::Complete(bool capture, bool cameraHit, double cameraDistance)
{
	if (phase_ != Phase::Presentation)
		throw std::logic_error("Character frame completed before its pose was evaluated");
	const auto& curves = primary_.PublishedCurves();
	const auto value = [&](const char* name) -> std::optional<double>
	{
		const auto it = curves.find(name);
		return it == curves.end() ? std::optional<double>{} : it->second;
	};
	effects_.PublishIntensity(cameraHit, cameraDistance);
	motion_.CompletePose({ value("Mask_Sway"), value("EnableADS"), value("FP_Action_Weight") }, true, capture);
	phase_ = Phase::Idle;
}
CharacterPresentation CharacterFrame::Present(const CameraActorPosition& actor, const Rotation& fire, double fov)
{
	if (phase_ != Phase::Idle || !finalSocket_)
		throw std::logic_error("Character presented before a frame completed");
	const auto& state = motion_.Output().camera;
	const auto eye = HeadSocketWorldPosition(actor, movement_.State().actorYaw, *finalSocket_);
	LocalPose camera;
	camera.position = eye;
	camera.rotation = WorldCameraQuaternion({ state.pitch * kDegreesPerRadian, state.yaw * kDegreesPerRadian, state.displayRoll }, fire);
	const auto pov = effects_.Present(CameraPovFromWorld(eye, camera.rotation, fov));
	if (effects_.HasPresented())
		camera = WorldCameraPose(pov);
	const auto root = CameraRelativePhysicalRoot(eye, movement_.State().actorYaw, camera);
	const auto arms = Multiply(root, HeadRelativeModelRoot(*finalSocket_));
	return { eye, camera, root, arms, Multiply(arms, finalGun_), Multiply(arms, finalHands_.left), Multiply(arms, finalHands_.right) };
}
}

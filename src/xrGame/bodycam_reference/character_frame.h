#pragma once
#include "motion_frame.h"
#include "primary_frame.h"
#include "camera_effects.h"
#include "camera_presentation.h"
#include "idle_pose.h"
namespace BodycamReference
{
struct CharacterMovementInput
{
	bool reloading = false;
	double right = 0, forward = 0;
	bool sprintRequested = false, canAccelerate = true;
	std::optional<int> desiredGait;
	unsigned stance = 0; // Source standing=0, crouched=1.
	bool engineGaitPresentation = true;
	bool cameraShake = true, weaponSway = true, stressMotion = true, sightRegrip = true;
	std::optional<PlanarVector> position;
	// Velocity measured by the engine's physics. It drives presentation only.
	std::optional<PlanarVector> engineVelocity;
	WalkingResolver resolveMove;
};
struct CharacterPoseResult
{
	std::vector<LocalPose> physical;
	SwayIkResult targets;
};
struct CharacterPresentation
{
	Vector unshakenEye;
	LocalPose camera;
	Matrix physicalRoot;
	Matrix armsCamera, weaponCamera;
	Matrix handLCamera, handRCamera;
};
// One character frame, in call order: Begin, Evaluate, Complete, Present.
// The referenced objects must outlive it.
class CharacterFrame
{
public:
	CharacterFrame(MotionFrame&, GroundedMovement&, PrimaryFrame&, AimPointBlend&, CameraEffects&);
	// afterGrounded runs between the movement step and the locomotion update.
	const MotionFrameOutput& Begin(double applicationDelta, bool aiming, bool magnified, const CharacterMovementInput&,
		const std::function<void()>& afterGrounded = {});
	CharacterPoseResult Evaluate(const std::vector<LocalPose>& authored, ViewmodelPoseInput, const OverlayMontages& montages = {},
		const ActionCurvePublication& actions = {});
	CharacterPoseResult Evaluate(
		IdlePose&, ViewmodelPoseInput, const OverlayMontages& montages = {}, const ActionCurvePublication& actions = {});
	// Mutates pose-derived input fields while retaining its sample buffers.
	CharacterPoseResult EvaluateInPlace(const std::vector<LocalPose>& authored, ViewmodelPoseInput&, const OverlayMontages& montages = {},
		const ActionCurvePublication& actions = {});
	void Complete(bool scopeCaptureActive, bool cameraBlockingHit, double cameraHitDistanceCm);
	CharacterPresentation Present(const CameraActorPosition&, const Rotation& cameraFire, double horizontalFov);

private:
	enum class Phase
	{
		Idle,
		Animation,
		Presentation
	};
	MotionFrame& motion_;
	GroundedMovement& movement_;
	PrimaryFrame& primary_;
	AimPointBlend& aimPoint_;
	CameraEffects& effects_;
	Phase phase_ = Phase::Idle;
	bool weaponSway_ = true;
	bool magnified_ = false;
	std::optional<Vector> finalSocket_;
	Matrix finalGun_{};
	PhysicalHandPoints finalHands_{};
};
}

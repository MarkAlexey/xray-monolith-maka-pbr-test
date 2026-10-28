#pragma once
#include "constants.h"
#include "control.h"

namespace BodycamReference
{
// Look input per second is divided by this to give the yaw and pitch rates.
constexpr double kLookRateScale = 50;
Rotation InterpolateControlRotation(Rotation current, Rotation target, double delta, double speed);
struct CameraInput
{
	Rotation control;
	double worldDelta = 0, lookX = 0, lookY = 0, moveX = 0, speedCm = 0;
	bool aiming = false, leaningPressed = false;
	int rotationMode = 2;
};
// The displayed camera's lag and the control smoothing. Classic follow tunes
// them; Reference keeps these values.
struct CameraMotionTuning
{
	// The camera lag speed rises from cameraLagSlowSpeed to cameraLagFastSpeed with the summed absolute rates.
	double cameraLagSlowSpeed = 2, cameraLagFastSpeed = 16, cameraLagRate = 5;
	double controlSmoothSpeed = 25;
};
struct CameraState
{
	Rotation smoothControl, armsEndpoint, cameraEndpoint;
	double yaw = 0, pitch = 0, displayRoll = 0;
	double finalYaw = 0, finalPitch = 0, finalRoll = 0;
	double currentLeanRoll = 0, roll = 0, leanTarget = 0, leanAlpha = 0;
	double yawRate = 0, pitchRate = 0, armsLagSpeed = 20, lagSpeed = 1;
};
class CameraMotion
{
public:
	const CameraState& Update(const CameraInput& input);
	void SetTuning(const CameraMotionTuning& tuning) { tuning_ = tuning; }
	const CameraState& State() const { return state_; }
	void Reset() { state_ = {}; }
	void SeedControl(Rotation control)
	{
		state_ = {};
		state_.smoothControl = state_.armsEndpoint = state_.cameraEndpoint = control;
		state_.yaw = control.yaw * kPi / 180;
		state_.pitch = control.pitch * kPi / 180;
		state_.finalYaw = state_.yaw;
		state_.finalPitch = state_.pitch;
		state_.roll = state_.displayRoll = state_.finalRoll = control.roll;
	}

private:
	CameraState state_;
	CameraMotionTuning tuning_;
};
}

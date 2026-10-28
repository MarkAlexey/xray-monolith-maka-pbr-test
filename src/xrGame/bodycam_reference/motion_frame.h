#pragma once
#include "ads_sway.h"
#include "camera.h"
#include "curve_frame.h"
#include "mouse_history.h"
#include "recoil_rig.h"
#include "regrip_montages.h"
#include "spring.h"
#include "viewmodel_helpers.h"

namespace BodycamReference
{
struct MotionMovement
{
	// Movement-simulation world basis in metres per second, not skeleton space.
	double actorYawRadians = 0, velocityX = 0, velocityZ = 0, strafeInput = 0;
	bool leaningPressed = false;
};
struct MotionFrameOutput
{
	Rotation control;
	CameraState camera;
	ProcessedMouseFrame mouse;
	std::array<double, 2> modifiedLook{};
	RecoilPoseSnapshot recoil;
	CurveInputs consumedCurves;
	std::array<SwayNode, 10> springs{};
	Vector elbowCm{}, magnifiedScale{};
	double applicationSeconds = 0, worldTime = 0, lowerSwayCm = 0;
	float worldSeconds = 0, adsProgress = 0, adsAlpha = 0;
};
// Begin, AfterMovement and CompletePose bracket one frame's movement and animation update.
class MotionFrame
{
public:
	MotionFrame(RecoilRig& recoil, RegripMontages& regrip, Rotation initialControl, double horizontalFov);
	MotionFrame(const MotionFrame&) = delete;
	MotionFrame& operator=(const MotionFrame&) = delete;
	void AddMouse(double x, double y) { mouse_.Add(x, y); }
	void ObserveSightMode(bool magnified, bool enabled = true);
	void ResetSightModeObservation() { sightMode_.reset(); }
	void SetExternalControl(const Rotation&);
	void SeedControl(const Rotation&, bool aiming = false);
	const MotionFrameOutput& Begin(double applicationSeconds, bool aiming, bool weaponReady, bool reloading = false);
	const MotionFrameOutput& AfterMovement(const MotionMovement&, bool magnified);
	void CompletePose(const FpCurves&, bool bonesValid, bool scopeCaptureActive, bool evaluated = true);
	const MotionFrameOutput& Output() const { return output_; }
	const Rotation& PendingControllerRecoil() const { return pendingController_; }
	const SwayRig& Springs() const { return springs_; }
	const CurveFrame& Curves() const { return curves_; }

private:
	enum class Phase
	{
		Idle,
		Input,
		Pose
	};
	RecoilRig& recoil_;
	RegripMontages& regrip_;
	double horizontalFov_;
	MouseAccumulator mouse_;
	MouseHistory history_;
	CurveFrame curves_;
	AdsSwayProducer adsSway_;
	LeftElbowOffset elbow_;
	LowerSway lower_;
	CameraMotion camera_;
	SwayRig springs_;
	MotionFrameOutput output_{};
	Rotation pendingController_{};
	std::array<double, 2> look_{};
	Phase phase_ = Phase::Idle;
	bool aiming_ = false, ready_ = false;
	std::optional<bool> sightMode_;
};
}

#pragma once
// The Classic camera: the look lag (Camera Decoupling's deadzone, or the
// gun-first follow), the camera shift, the Reference head roll and shake, the
// Footstep Camera, and the view they compose over the effector camera. No
// engine state: the engine side gathers CameraRigInput, answers CameraRigHost
// and applies the composed view; see bodycam_actor_camera.cpp.
#include "bodycam_simulation.h"
#include "bodycam_reference/classic_follow.h"
#include "bodycam_reference/classic_footstep_camera.h"
#include <array>
#include <cstdint>
#include <optional>

namespace Bodycam
{
// The camera position follows at this share of the angle follow speed.
constexpr float kCameraPositionRateShare = 0.8f;

// Camera decoupling.
struct CameraModeSettings
{
	float inner_gain = 0.f;
	float follow_speed = 8.f;
	float follow_damping = 0.85f;
	float deadzone_yaw = 1.5f;
	float deadzone_pitch = 1.f;
	float softzone_yaw = 4.f;
	float softzone_pitch = 3.f;
	float max_yaw = 19.f;
	float max_pitch = 19.f;
	float pos = 0.025f;
};

// Aiming keeps the camera closer to the gun and lets it catch up faster.
inline CameraModeSettings AdsCameraModeDefaults()
{
	CameraModeSettings ads;
	ads.follow_speed = 14.f;
	ads.follow_damping = 1.f;
	ads.deadzone_yaw = 0.8f;
	ads.deadzone_pitch = 0.55f;
	ads.softzone_yaw = 1.8f;
	ads.softzone_pitch = 1.2f;
	ads.max_yaw = 3.f;
	ads.max_pitch = 2.f;
	ads.pos = 0.006f;
	return ads;
}

// The gun-first follow's camera lag (BodycamReference::CameraMotionTuning).
struct FollowLagSettings
{
	float lag_speed_min = 2.f;
	float lag_speed_max = 16.f;
	float lag_response = 5.f;
	float control_smoothing = 25.f;
};

struct CameraDecouplingSettings
{
	bool enable = true;
	bool follow_lag = true; // The gun-first follow takes the place of the deadzone lag.
	CameraModeSettings hip;
	CameraModeSettings ads = AdsCameraModeDefaults();
	float move_pos = 0.020f;
	FollowLagSettings follow;

	// The lag model the rig runs: Follow needs the feature on.
	bool FollowLag() const { return follow_lag && enable; }
};

// Reference head motion, part of Camera Decoupling: roll and shake (shake is
// view-only).
struct HeadMotionSettings
{
	float roll = .43f;
	float shake = 1.f;
};

// Recorded Reference head motion on the native footstep schedule.
struct FootstepCameraSettings
{
	bool enable = true;
	float bob = 1.f;
	float posture = 2.f;
	float sprint_lunge = 2.f;
	float ads = 1.f;
};

// The settings the rig reads, where the runtime config keeps them.
struct CameraRigSettings
{
	const CameraDecouplingSettings& camera;
	const HeadMotionSettings& head;
	const FootstepCameraSettings& footstep;
	bool sprint_sway = false; // Sprint Transition: the sprint camera sway may move the view.

	bool FollowLag() const { return camera.FollowLag(); }
};

struct CameraState
{
	bool initialized = false;
	bool was_enabled = false;
	float yaw = 0.f;
	float pitch = 0.f;
	SVec3 pos;
};

// One frame of the engine's camera and actor state.
struct CameraRigInput
{
	std::uint32_t frame = 0; // Stamps the HUD share.
	bool apply = false; // The view may move; false while the PiP holds the surround still.
	float aim_yaw = 0.f; // The camera's aim, radians.
	float aim_pitch = 0.f;
	float dt = 0.f;
	bool paused = false;
	AdsState ads;
	double strafe = 0.; // -1 left, 1 right.
	bool leaning = false;
	bool sprinting = false;
	float inertia_speed = 0.f; // Movement Inertia's actual speed, m/s.
	Fvector velocity = { 0.f, 0.f, 0.f }; // m/s, world space.
	bool grounded = true;
	// The planar speed on Reference's gait speeds, m/s, so modded walk, run
	// and sprint speeds keep the same presentation.
	double reference_speed = 0.;
	std::optional<double> foot_phase; // The native leg cycle, while it strides.
	Fmatrix effector; // The effector camera: basis and position.
};

// The simulated view relative to the aim.
struct VisualOutput
{
	float yaw = 0.f;
	float pitch = 0.f;
	Fvector pos = { 0.f, 0.f, 0.f }; // Camera space.
	float sprint_pitch = 0.f; // Part of pitch that is sprint sway, not look lag.
};

struct CameraComposition
{
	VisualOutput lag;
	Fmatrix view; // The final camera basis, with its world position.
	bool composes = false; // The rig moves the view this frame; otherwise the native camera stays.
};

// What the rig asks of the engine.
struct CameraRigHost
{
	virtual ~CameraRigHost() = default;
	// Distance to the first static surface along a horizontal ray from origin
	// (world space, metres, the actor excluded), or nothing within max_distance.
	virtual std::optional<float> HorizontalClearance(const Fvector& origin, const Fvector& direction, float max_distance) = 0;
};

class ClassicCameraRig
{
public:
	// Without an aim the lag starts from the first frame's aim.
	void Reset();
	void Reset(float yaw, float pitch);
	// A script turned the camera: the deadzone lag turns with it, the follow
	// lag starts again from the displayed angles.
	void Rebase(float yaw_delta, float pitch_delta);
	const CameraComposition& Compose(const CameraRigSettings& settings, const CameraRigInput& input, const InputSignals& signals,
		const SprintSway& sway, CameraRigHost& host);
	// The head motion the HUD camera shares, from the current or the previous frame only.
	bool HudShare(std::uint32_t frame, Fvector& offset, Fmatrix& roll) const;
	// The view after the regrip camera turns this frame's applied view about
	// its position: the composed view as the camera manager applied it, or
	// the native camera when the rig did not compose.
	Fmatrix RegripView(const Fmatrix& native, const Fmatrix& regrip) const;

	const CameraState& Camera() const { return m_camera; }
	const BodycamReference::ClassicFollowOutput& Follow() const { return m_follow_output; }
	const BodycamReference::FootstepCameraOutput& Footstep() const { return m_footstep.Output(); }
	const BodycamReference::ReferenceHeadOutput& Head() const { return m_head.Output(); }

private:
	void ResetFollow();
	void UpdateLag(const CameraRigSettings& settings, const CameraRigInput& input, const InputSignals& signals, const SprintSway& sway);
	bool UpdateFollowLag(
		const CameraDecouplingSettings& camera, const CameraRigInput& input, bool enabled, std::array<double, 3>& look_shift);
	Fvector UpdateFootstep(const FootstepCameraSettings& settings, const CameraRigInput& input, CameraRigHost& host);
	void UpdateHead(const HeadMotionSettings& settings, const CameraRigInput& input, Fmatrix& view_rotation);
	void ResetHead();
	void ComposeView(const CameraRigInput& input, const Fmatrix& head_rotation);

	CameraState m_camera;
	BodycamReference::ClassicFollow m_follow_lag;
	BodycamReference::ClassicLookTranslation m_look_translation;
	BodycamReference::ClassicFollowOutput m_follow_output;
	BodycamReference::ClassicFootstepCamera m_footstep;
	BodycamReference::ClassicReferenceHead m_head;
	// The head has moved since its last reset. Resetting rebuilds its effects,
	// so a head that is already reset is left alone.
	bool m_head_moved = false;
	// The HUD camera is built from the effector camera, not the composed view,
	// so it shares the footstep offset and the head roll (not the shake), as
	// stamped on the last composed frame.
	Fvector m_footstep_offset = { 0.f, 0.f, 0.f };
	Fmatrix m_head_roll = Fidentity;
	std::uint32_t m_hud_share_frame = std::uint32_t(-1);
	CameraComposition m_composition;
};
} // namespace Bodycam

#if !defined(BODYCAM_STANDALONE)
#include "stdafx.h"
#endif
#include "bodycam_camera_rig.h"
#include "bodycam_math.h"
#include "bodycam_smoothing.h"
#include "bodycam_reference/classic_regrip.h"
#include <algorithm>
#include <cmath>

// Settings arrive range-checked from the registry in bodycam_settings.cpp, so
// nothing here clamps them again.

namespace Bodycam
{
namespace
{
constexpr float kCameraPitchShiftShare = 0.55f; // Vertical look shift relative to horizontal.
constexpr float kCameraForwardShiftShare = 0.65f; // Forward movement shift relative to sideways.
constexpr float kCameraAdsMoveShiftScale = 0.35f; // Movement shift kept while aiming.
// Metres kept between the footstep camera and a wall.
constexpr float kFootstepWallClearance = 0.1f;
// Metres; a smaller horizontal footstep offset is not traced against walls.
constexpr float kFootstepMinimumTrace = 0.001f;

float SoftRamp(float value, float width)
{
	if (width <= kEpsilon)
		return 1.f;

	const float t = std::clamp(value / width, 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

float CalcDesiredAngle(float current, float target, float deadzone, float softzone, float inner_gain)
{
	const float error = AngleDifference(target, current);
	const float abs_error = std::fabs(error);
	const float inside = NormalizeAngle(current + error * inner_gain);
	if (abs_error <= deadzone)
		return inside;

	const float ramp = SoftRamp(abs_error - deadzone, softzone);
	const float full = NormalizeAngle(target - std::clamp(error, -deadzone, deadzone));
	return NormalizeAngle(inside + AngleDifference(full, inside) * ramp);
}

// Camera follow parameters, blended from hip to ADS. Angles in radians.
struct CameraResponse
{
	float deadzone_yaw, deadzone_pitch, softzone_yaw, softzone_pitch, max_yaw, max_pitch;
	float follow_speed, follow_damping, camera_pos, inner_gain;
};

CameraResponse BlendCameraResponse(const CameraDecouplingSettings& settings, float ads_blend)
{
	const CameraModeSettings& hip = settings.hip;
	const CameraModeSettings& ads = settings.ads;
	CameraResponse r;
	r.deadzone_yaw = DegToRad(Lerp(hip.deadzone_yaw, ads.deadzone_yaw, ads_blend));
	r.deadzone_pitch = DegToRad(Lerp(hip.deadzone_pitch, ads.deadzone_pitch, ads_blend));
	r.softzone_yaw = DegToRad(Lerp(hip.softzone_yaw, ads.softzone_yaw, ads_blend));
	r.softzone_pitch = DegToRad(Lerp(hip.softzone_pitch, ads.softzone_pitch, ads_blend));
	r.max_yaw = DegToRad(Lerp(hip.max_yaw, ads.max_yaw, ads_blend));
	r.max_pitch = DegToRad(Lerp(hip.max_pitch, ads.max_pitch, ads_blend));
	r.follow_speed = Lerp(hip.follow_speed, ads.follow_speed, ads_blend);
	r.follow_damping = Lerp(hip.follow_damping, ads.follow_damping, ads_blend);
	r.camera_pos = Lerp(hip.pos, ads.pos, ads_blend);
	r.inner_gain = Lerp(hip.inner_gain, ads.inner_gain, ads_blend);
	return r;
}

// The camera trails the aim inside a deadzone and soft zone, limited to a
// maximum lag. Its position shifts with the lag and with movement, and
// carries the sprint sway. With the feature off only the sway remains.
void UpdateDeadzoneLag(const CameraDecouplingSettings& settings, CameraState& state, const CameraRigInput& input, bool follow_lag,
	const InputSignals& signals, const SprintSway& sway, VisualOutput& output)
{
	if (!state.initialized)
	{
		state.yaw = input.aim_yaw;
		state.pitch = input.aim_pitch;
		state.initialized = true;
	}
	// The look and movement shift ends with the feature; only sway may remain.
	if (state.was_enabled && !settings.enable)
		state.pos.Set(0.f, 0.f, 0.f);
	state.was_enabled = settings.enable;
	const CameraResponse response = BlendCameraResponse(settings, input.ads.blend);

	const float angle_rate = FollowRate(response.follow_speed, response.follow_damping);
	const float desired_yaw = CalcDesiredAngle(state.yaw, input.aim_yaw, response.deadzone_yaw, response.softzone_yaw, response.inner_gain);
	const float desired_pitch =
		CalcDesiredAngle(state.pitch, input.aim_pitch, response.deadzone_pitch, response.softzone_pitch, response.inner_gain);
	state.yaw = SmoothAngle(state.yaw, desired_yaw, angle_rate, input.dt);
	state.pitch = SmoothAngle(state.pitch, desired_pitch, angle_rate, input.dt);

	const float yaw_error = AngleDifference(input.aim_yaw, state.yaw);
	const float pitch_error = AngleDifference(input.aim_pitch, state.pitch);
	if (std::fabs(yaw_error) > response.max_yaw)
		state.yaw = NormalizeAngle(input.aim_yaw - std::clamp(yaw_error, -response.max_yaw, response.max_yaw));
	if (std::fabs(pitch_error) > response.max_pitch)
		state.pitch = NormalizeAngle(input.aim_pitch - std::clamp(pitch_error, -response.max_pitch, response.max_pitch));

	// How far the camera trails the aim, as a share of the allowed lag (-1..1).
	// The limit above leaves the error within a rounding step of the maximum.
	const float limited_yaw_error = AngleDifference(input.aim_yaw, state.yaw);
	const float limited_pitch_error = AngleDifference(input.aim_pitch, state.pitch);
	const float yaw_norm = response.max_yaw > kEpsilon ? std::clamp(limited_yaw_error / response.max_yaw, -1.f, 1.f) : 0.f;
	const float pitch_norm = response.max_pitch > kEpsilon ? std::clamp(limited_pitch_error / response.max_pitch, -1.f, 1.f) : 0.f;

	// Gun-first follow computes the look shift from its own camera lag.
	const float look_pos = settings.enable && !follow_lag ? response.camera_pos : 0.f;
	const float move_pos = settings.enable ? settings.move_pos : 0.f;
	const float move_scale = input.ads.active ? kCameraAdsMoveShiftScale : 1.f;
	SVec3 pos_target;
	pos_target.Set(-yaw_norm * look_pos - signals.move_intent.x * move_pos * move_scale, pitch_norm * look_pos * kCameraPitchShiftShare,
		-signals.move_intent.z * move_pos * kCameraForwardShiftShare * move_scale);
	pos_target.Add(sway.offset);
	SmoothVector(state.pos, pos_target, FollowRate(response.follow_speed * kCameraPositionRateShare, response.follow_damping), input.dt);
	ClampVector(state.pos, look_pos + move_pos + sway.reach);

	output.yaw = (settings.enable ? state.yaw : input.aim_yaw);
	output.pitch = (settings.enable ? state.pitch : input.aim_pitch) + sway.pitch;
	output.sprint_pitch = sway.pitch;
	output.pos.set(state.pos.x, state.pos.y, state.pos.z);
}
} // namespace

void ClassicCameraRig::ResetFollow()
{
	m_follow_lag.Reset();
	m_look_translation.Reset();
	m_follow_output = BodycamReference::ClassicFollowOutput();
}

void ClassicCameraRig::Reset()
{
	m_camera = CameraState();
	ResetFollow();
	m_footstep.Reset();
	ResetHead();
	m_footstep_offset.set(0.f, 0.f, 0.f);
	m_head_roll.identity();
}

void ClassicCameraRig::Reset(float yaw, float pitch)
{
	Reset();
	m_camera.yaw = yaw;
	m_camera.pitch = pitch;
	m_camera.initialized = true;
}

void ClassicCameraRig::Rebase(float yaw_delta, float pitch_delta)
{
	if (m_camera.initialized)
	{
		m_camera.yaw = NormalizeAngle(m_camera.yaw + yaw_delta);
		m_camera.pitch = NormalizeAngle(m_camera.pitch + pitch_delta);
	}
	ResetFollow();
}

bool ClassicCameraRig::HudShare(std::uint32_t frame, Fvector& offset, Fmatrix& roll) const
{
	if (m_hud_share_frame + 1 < frame)
		return false;
	offset.set(m_footstep_offset);
	roll.set(m_head_roll);
	return true;
}

// Applying a view makes its direction and up unit length and rebuilds right
// and up from them (CCameraManager::ApplyDeviceOverride); the regrip turns
// that applied basis, not the composed one.
Fmatrix ClassicCameraRig::RegripView(const Fmatrix& native, const Fmatrix& regrip) const
{
	Fmatrix base = native;
	if (m_composition.composes)
	{
		const Fmatrix& view = m_composition.view;
		Fvector direction = view.k, up = view.j, right;
		direction.normalize();
		up.normalize();
		right.crossproduct(up, direction);
		up.crossproduct(direction, right);
		base.set(right, up, direction, view.c);
	}
	Fmatrix turned;
	turned.mul(base, regrip);
	turned.c = base.c;
	return turned;
}

// Both lag models run every frame, whichever is selected. The deadzone lag
// keeps advancing under Follow: its position spring carries the movement shift
// and the sprint sway, a switch back continues from its state, and Follow's
// angles reach the output as a change on top of the deadzone angles.
void ClassicCameraRig::UpdateLag(
	const CameraRigSettings& settings, const CameraRigInput& input, const InputSignals& signals, const SprintSway& sway)
{
	const bool follow_lag = settings.FollowLag();
	// Follow starts from the angles the deadzone lag displays.
	if (follow_lag && !m_follow_output.ownsFollow)
		m_follow_lag.Seed(input.aim_yaw, input.aim_pitch, m_camera.initialized ? m_camera.yaw : input.aim_yaw,
			m_camera.initialized ? m_camera.pitch : input.aim_pitch);
	VisualOutput& lag = m_composition.lag;
	UpdateDeadzoneLag(settings.camera, m_camera, input, follow_lag, signals, sway, lag);

	std::array<double, 3> look_shift;
	if (!UpdateFollowLag(settings.camera, input, follow_lag, look_shift))
		return;
	// Follow takes over the angles and adds its look shift; the sprint sway the
	// deadzone lag added stays.
	lag.pos.x += float(look_shift[0]);
	lag.pos.y += float(look_shift[1]);
	lag.yaw += float(m_follow_output.cameraYaw) - m_camera.yaw;
	lag.pitch += float(m_follow_output.cameraPitch) - m_camera.pitch;
	m_camera.yaw = float(m_follow_output.cameraYaw);
	m_camera.pitch = float(m_follow_output.cameraPitch);
}

// The gun-first follow and the look shift it derives from its own camera lag.
// Both are held reset while it does not own the frame.
bool ClassicCameraRig::UpdateFollowLag(
	const CameraDecouplingSettings& camera, const CameraRigInput& input, bool enabled, std::array<double, 3>& look_shift)
{
	BodycamReference::ClassicFollowInput follow;
	follow.yaw = input.aim_yaw;
	follow.pitch = input.aim_pitch;
	follow.dt = input.dt;
	follow.enabled = enabled;
	follow.aiming = input.ads.active;
	follow.strafe = input.strafe;
	follow.speedMetres = input.inertia_speed;
	const FollowLagSettings& tuning = camera.follow;
	follow.tuning = { tuning.lag_speed_min, tuning.lag_speed_max, tuning.lag_response, tuning.control_smoothing };
	m_follow_output = m_follow_lag.Update(follow);
	if (!m_follow_output.ownsFollow)
	{
		m_look_translation.Reset();
		return false;
	}
	const float blend = input.ads.blend;
	look_shift = m_look_translation.Update(input.aim_yaw, input.aim_pitch, m_follow_output.cameraYaw, m_follow_output.cameraPitch,
		Lerp(camera.hip.max_yaw, camera.ads.max_yaw, blend), Lerp(camera.hip.max_pitch, camera.ads.max_pitch, blend),
		Lerp(camera.hip.pos, camera.ads.pos, blend), input.dt,
		Lerp(camera.hip.follow_speed, camera.ads.follow_speed, blend) * kCameraPositionRateShare);
	return true;
}

// Recorded Reference head motion on the native footstep schedule, as a world
// offset in the yaw-only frame. Horizontal travel is limited before walls.
Fvector ClassicCameraRig::UpdateFootstep(const FootstepCameraSettings& settings, const CameraRigInput& input, CameraRigHost& host)
{
	Fvector result;
	result.set(0.f, 0.f, 0.f);
	Fvector dir, up, right;
	BuildCameraBasis(input.aim_yaw, 0.f, dir, up, right);
	const Fvector& velocity = input.velocity;
	BodycamReference::FootstepCameraInput footstep;
	footstep.dt = input.dt;
	footstep.paused = input.paused;
	footstep.footPhase = input.foot_phase;
	footstep.forward = velocity.x * dir.x + velocity.z * dir.z;
	footstep.right = velocity.x * right.x + velocity.z * right.z;
	footstep.speed = input.reference_speed;
	footstep.sprinting = input.sprinting;
	footstep.grounded = input.grounded;
	const float ads = 1.f + (settings.ads - 1.f) * input.ads.blend;
	footstep.bobScale = settings.bob * ads;
	footstep.postureScale = settings.posture * ads;
	footstep.lungeScale = settings.sprint_lunge * ads;
	const auto& offset = m_footstep.Update(footstep).offset;
	result.mad(right, float(offset[0]));
	result.y += float(offset[1]);
	result.mad(dir, float(offset[2]));

	Fvector horizontal;
	horizontal.set(result.x, 0.f, result.z);
	const float length = horizontal.magnitude();
	if (length > kFootstepMinimumTrace)
	{
		horizontal.div(length);
		if (const std::optional<float> range = host.HorizontalClearance(input.effector.c, horizontal, length + kFootstepWallClearance))
		{
			const float allowed = std::max(*range - kFootstepWallClearance, 0.f);
			if (allowed < length)
			{
				result.x *= allowed / length;
				result.z *= allowed / length;
			}
		}
	}
	return result;
}

// Reference head roll and shake. The HUD receives the roll only; the shake is
// view-only, as Reference anchors the arms to the body.
void ClassicCameraRig::UpdateHead(const HeadMotionSettings& settings, const CameraRigInput& input, Fmatrix& view_rotation)
{
	BodycamReference::ReferenceHeadInput head_input;
	head_input.yaw = input.aim_yaw;
	head_input.pitch = input.aim_pitch;
	head_input.dt = input.dt;
	head_input.paused = input.paused;
	head_input.strafe = input.strafe;
	head_input.speedMetres = input.reference_speed;
	head_input.aiming = input.ads.active;
	head_input.leaning = input.leaning;
	const auto& head = m_head.Update(head_input);
	m_head_moved = true;
	const double roll = head.roll * settings.roll;
	const double shake = settings.shake;
	m_head_roll = ToEngine(BodycamReference::ClassicRegripCameraDelta({ 0., 0., roll }));
	view_rotation = ToEngine(
		BodycamReference::ClassicRegripCameraDelta({ head.shake.pitch * shake, head.shake.yaw * shake, roll + head.shake.roll * shake }));
}

void ClassicCameraRig::ResetHead()
{
	if (!m_head_moved)
		return;
	m_head.Reset();
	m_head_moved = false;
}

// The simulated view relative to the aim is applied on top of the effector
// camera. With Camera Decoupling off the lag is the aim itself plus the
// sprint sway: native orientation, FOV and effectors stay.
void ClassicCameraRig::ComposeView(const CameraRigInput& input, const Fmatrix& head_rotation)
{
	const VisualOutput& lag = m_composition.lag;
	Fvector visual_dir, visual_up, visual_right;
	BuildCameraBasis(lag.yaw, lag.pitch, visual_dir, visual_up, visual_right);
	Fvector raw_dir, raw_up, raw_right;
	BuildCameraBasis(input.aim_yaw, input.aim_pitch, raw_dir, raw_up, raw_right);

	Fvector zero;
	zero.set(0.f, 0.f, 0.f);
	Fmatrix raw_basis, visual_basis, raw_inv, visual_delta;
	raw_basis.set(raw_right, raw_up, raw_dir, zero);
	visual_basis.set(visual_right, visual_up, visual_dir, zero);
	raw_inv.invert(raw_basis);
	visual_delta.mul(raw_inv, visual_basis);
	Fmatrix& view = m_composition.view;
	view.mul(input.effector, visual_delta);
	// Reference head roll and shake, camera-local after the visual delta.
	view.mulB_43(head_rotation);

	Fvector position = input.effector.c;
	position.mad(view.i, lag.pos.x);
	position.mad(view.j, lag.pos.y);
	position.mad(view.k, lag.pos.z);
	position.add(m_footstep_offset);
	view.c = position;
}

// Each camera feature works alone: Camera Decoupling (look lag, roll and
// shake), Footstep Camera, and the Sprint Transition camera sway.
const CameraComposition& ClassicCameraRig::Compose(const CameraRigSettings& settings, const CameraRigInput& input,
	const InputSignals& signals, const SprintSway& sway, CameraRigHost& host)
{
	UpdateLag(settings, input, signals, sway);
	m_composition.composes = false;

	const bool camera_enabled = settings.camera.enable;
	const bool footstep_enabled = settings.footstep.enable;
	if (!footstep_enabled)
	{
		m_footstep.Reset();
		m_footstep_offset.set(0.f, 0.f, 0.f);
	}
	const VisualOutput& lag = m_composition.lag;
	const bool sprint_sway =
		!camera_enabled && settings.sprint_sway && (_abs(lag.sprint_pitch) > EPS_S || lag.pos.square_magnitude() > EPS_S * EPS_S);
	if (!camera_enabled)
	{
		ResetHead();
		m_head_roll.identity();
	}
	if (!input.apply || (!camera_enabled && !footstep_enabled && !sprint_sway))
		return m_composition;

	if (footstep_enabled)
		m_footstep_offset = UpdateFootstep(settings.footstep, input, host);
	Fmatrix head_rotation;
	head_rotation.identity();
	if (camera_enabled)
		UpdateHead(settings.head, input, head_rotation);
	m_hud_share_frame = input.frame;
	ComposeView(input, head_rotation);
	m_composition.composes = true;
	return m_composition;
}
} // namespace Bodycam

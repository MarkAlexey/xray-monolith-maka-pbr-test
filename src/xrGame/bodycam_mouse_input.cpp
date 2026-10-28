#if !defined(BODYCAM_STANDALONE)
#include "stdafx.h"
#endif
#include "bodycam_mouse_input.h"
#include "bodycam_simulation.h"

#include <cmath>

namespace Bodycam
{
namespace
{
// Radians. A larger difference from the camera's aim means something other than the mouse moved it.
constexpr float kReconcileTolerance = 1.0e-5f;
} // namespace

void ResetMouseAim(MouseAimState& state, float yaw, float pitch)
{
	state = MouseAimState();
	state.yaw = NormalizeAngle(yaw);
	state.pitch = NormalizeAngle(pitch);
	state.initialized = true;
}

void AddMouseAimDelta(MouseAimState& state, float yaw_delta, float pitch_delta)
{
	state.pending_yaw += yaw_delta;
	state.pending_pitch += pitch_delta;
}

void RebaseMouseAim(MouseAimState& state, float yaw_delta, float pitch_delta)
{
	if (!state.initialized)
		return;

	state.yaw = NormalizeAngle(state.yaw + yaw_delta);
	state.pitch = NormalizeAngle(state.pitch + pitch_delta);
}

MouseAim ResolveMouseAim(MouseAimState& state, float target_yaw, float target_pitch)
{
	if (!state.initialized)
		ResetMouseAim(state, target_yaw, target_pitch);

	state.yaw = NormalizeAngle(state.yaw + state.pending_yaw);
	state.pitch = NormalizeAngle(state.pitch + state.pending_pitch);
	state.pending_yaw = 0.f;
	state.pending_pitch = 0.f;

	// Recoil, script cameras and pitch limits move the camera without the mouse.
	if (std::fabs(AngleDifference(target_yaw, state.yaw)) > kReconcileTolerance)
		state.yaw = NormalizeAngle(target_yaw);
	if (std::fabs(AngleDifference(target_pitch, state.pitch)) > kReconcileTolerance)
		state.pitch = NormalizeAngle(target_pitch);

	return { state.yaw, state.pitch };
}
} // namespace Bodycam

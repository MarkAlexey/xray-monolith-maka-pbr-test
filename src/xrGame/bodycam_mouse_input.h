#pragma once
// The aim as the sum of the mouse deltas handed to the camera. Mouse speed is
// the frame-to-frame difference of the aim, and the aim read back from the
// camera direction is too coarse for that at small movements. The camera stays
// authoritative: a difference beyond rounding snaps the aim to it.

namespace Bodycam
{
struct MouseAimState
{
	float yaw = 0.f;
	float pitch = 0.f;
	float pending_yaw = 0.f;
	float pending_pitch = 0.f;
	bool initialized = false;
};

struct MouseAim
{
	float yaw = 0.f;
	float pitch = 0.f;
};

void ResetMouseAim(MouseAimState& state, float yaw, float pitch);
void AddMouseAimDelta(MouseAimState& state, float yaw_delta, float pitch_delta);
void RebaseMouseAim(MouseAimState& state, float yaw_delta, float pitch_delta);
MouseAim ResolveMouseAim(MouseAimState& state, float target_yaw, float target_pitch);
} // namespace Bodycam

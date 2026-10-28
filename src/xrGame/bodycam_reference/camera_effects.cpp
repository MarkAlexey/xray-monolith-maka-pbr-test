#include "camera_effects.h"
namespace BodycamReference
{
void CameraEffects::Tick(double dt, const MovementVibrationInput& movement, double x, double y)
{
	pendingDelta_ = dt;
	vibration_.ComponentTick(dt, movement);
	mouse_.InputFrame(x, y, dt, movement.speed);
}
CameraPov CameraEffects::Present(const CameraPov& base)
{
	if (pendingDelta_ > 0)
	{
		cached_ = mouse_.Update(vibration_.Apply(base, pendingDelta_), pendingDelta_);
		pendingDelta_ = 0;
	}
	return enabled_ && cached_ ? *cached_ : base;
}
}

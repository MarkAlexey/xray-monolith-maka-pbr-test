#pragma once
#include "mouse_shake.h"
#include <optional>
namespace BodycamReference
{
// Tick records one pending delta. The first Present after it applies the effects and caches the
// result; later Present calls in the same frame (extra render passes) return the cached view.
class CameraEffects
{
public:
	CameraEffects(MovementCameraVibration& vibration, MouseShakeLayer& mouse) : vibration_(vibration), mouse_(mouse) {}
	void SetEnabled(bool enabled) { enabled_ = enabled; }
	void Tick(double dt, const MovementVibrationInput& movement, double lookX, double lookY);
	void PublishIntensity(bool hit, double distanceCm) { vibration_.PublishIntensity(hit, distanceCm); }
	CameraPov Present(const CameraPov& base);
	double PendingDelta() const { return pendingDelta_; }
	bool HasPresented() const { return cached_.has_value(); }

private:
	MovementCameraVibration& vibration_;
	MouseShakeLayer& mouse_;
	bool enabled_ = true;
	double pendingDelta_ = 0;
	std::optional<CameraPov> cached_;
};
}

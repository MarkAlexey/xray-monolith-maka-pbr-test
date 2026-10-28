#pragma once
#include "control.h"
#include "pose.h"

namespace BodycamReference
{
// The sway scale curve at a normalised yaw rate in [0, 1]: { 0, y, z }.
Vector AdsSwayScale(double time);
class AdsSwayProducer
{
public:
	const Vector& Update(const Rotation& control, double deltaSeconds, bool weaponValid = true, bool curveValid = true);
	const Rotation& Previous() const { return previous_; }
	const Rotation& Velocity() const { return velocity_; }
	const Vector& Scale() const { return scale_; }
	float Time() const { return time_; }
	void SeedControl(Rotation control)
	{
		previous_ = control;
		velocity_ = {};
		scale_ = {};
		time_ = 0;
	}

private:
	Rotation previous_{}, velocity_{};
	Vector scale_{};
	float time_ = 0;
};
}

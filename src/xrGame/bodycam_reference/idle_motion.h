#pragma once
#include "movement_additives.h"
#include "locomotion_properties.h"
#include "native_idle_curve_data.h"
#include <algorithm>
#include <cmath>
namespace BodycamReference
{
// Ids MovementAdditivesWalk passes to the stress adapter: the subtle idle's sequence player and
// its scale node. Any other id is the strong idle.
constexpr unsigned kSubtleIdleSequence = 28, kSubtleIdleScale = 29;
// Subtle and strong idle stress curves for the walking additive layer. The subtle weight follows
// speed and fades to zero while aiming.
class IdleMotion
{
public:
	IdleMotion() : subtleCurves_(Convert(NativeSubtleIdleData())), strongCurves_(Convert(NativeStrongIdleData())) {}
	IdleMotion(const IdleMotion&) = delete;
	IdleMotion& operator=(const IdleMotion&) = delete;
	void SetEnabled(bool enabled) { enabled_ = enabled; }
	void Update(double dt, double speed, bool aiming)
	{
		const double target = aiming ? 0. : .1 + .9 * std::clamp(speed / 100., 0., 1.);
		const double distance = target - subtle_;
		subtle_ = distance * distance < 1e-8 ? target : subtle_ + distance * std::clamp(double(float(dt)) * 10., 0., 1.);
		strong_ = aiming ? 0. : kHipStrongIdleWeight;
	}
	WalkingStressAdapter Adapter()
	{
		return { [this](unsigned id, float dt)
			{
				auto& time = id == kSubtleIdleSequence ? subtleTime_ : strongTime_;
				const float length = id == kSubtleIdleSequence ? NativeSubtleIdleData().length : NativeStrongIdleData().length;
				time = time + dt;
				if (time > length || time < 0)
				{
					time = std::fmod(time, length);
					if (time < 0)
						time += length;
				}
			},
			[this](unsigned id)
			{
				const auto& curves = id == kSubtleIdleSequence ? subtleCurves_ : strongCurves_;
				const float time = id == kSubtleIdleSequence ? subtleTime_ : strongTime_;
				const char* names[] = { "IK_Pitch", "IK_Yaw", "IK_Roll", "IK_X", "IK_Y", "IK_Z" };
				NamedCurves result;
				for (unsigned i = 0; i < 6; ++i)
					result.push_back({ names[i], float(curves[i].Sample(time)), 0 });
				return result;
			},
			[this](unsigned id)
			{
				std::array<float, 6> result;
				result.fill(enabled_ ? float(id == kSubtleIdleScale ? subtle_ : strong_) : 0.f);
				return result;
			} };
	}

private:
	static std::vector<LocomotionPropertyCurve> Convert(const NativeIdleCurveData& data)
	{
		std::vector<LocomotionPropertyCurve> result;
		for (const auto& curve : data.curves)
		{
			std::vector<LocomotionCurveKey> keys;
			for (const auto& k : curve)
				keys.push_back({ k.time, k.value, k.arrive, k.leave, k.linear });
			result.emplace_back(std::move(keys));
		}
		return result;
	}
	// The source weights the strong idle by 4 * stress at the hip and by (stress - .5) / .5 when
	// aiming, with a fixed stress of .1: .4 and 0.
	static constexpr double kHipStrongIdleWeight = .4;
	std::vector<LocomotionPropertyCurve> subtleCurves_, strongCurves_;
	float subtleTime_ = 0, strongTime_ = 0;
	bool enabled_ = true;
	double subtle_ = 0, strong_ = 0;
};
}

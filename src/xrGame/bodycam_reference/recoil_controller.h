#pragma once
#include <array>

namespace BodycamReference
{
struct ControllerRecoilConfig
{
	std::array<std::array<double, 2>, 2> step;
	std::array<double, 2> smoothing;
	std::array<float, 2> scale{ 1.f, 1.f };
	double damping;
	double compensation = 1;
};

// Axis order is horizontal/yaw, vertical/pitch.
struct ControllerRecoilState
{
	std::array<double, 2> target{}, current{}, inputCompensation{}, delta{};
	bool firingActive = false;
};

class ControllerRecoil
{
public:
	void Configure(const ControllerRecoilConfig& config) { config_ = config; }
	void AccumulateMouse(double x, double y);
	// randomUnits holds one random unit per axis, horizontal first.
	void Fire(const std::array<double, 2>& randomUnits);
	void Stop();
	void Update(double worldDelta);
	const ControllerRecoilState& State() const { return state_; }

private:
	ControllerRecoilConfig config_{};
	ControllerRecoilState state_{};
};
}

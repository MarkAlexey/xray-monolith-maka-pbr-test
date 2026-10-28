#pragma once
#include "control.h"

namespace BodycamReference
{
struct MouseHistoryState
{
	std::array<float, 2> zeroTime{}, previous{};
	std::int32_t sampleCount = 1;
	float samplingTotal = .0083f;
	float timeDilation = 1;
};
struct ProcessedMouseFrame
{
	std::array<double, 2> raw{};
	std::array<float, 2> processed{};
};
class MouseHistory
{
public:
	// Smoothed, FOV-scaled mouse input; the raw look input is processed separately.
	ProcessedMouseFrame Process(
		const MouseFrame& frame, double applicationDelta, double cameraHorizontalFov, double timeDilation = 1, bool xFirst = true);
	const MouseHistoryState& State() const { return state_; }

private:
	void ResetHistory();
	float Smooth(int axis, float value, std::uint8_t samples, float dt, float dilation);
	MouseHistoryState state_;
};
}

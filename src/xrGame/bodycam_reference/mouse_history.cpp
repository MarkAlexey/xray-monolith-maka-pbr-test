// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "mouse_history.h"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
float f(double v)
{
	return static_cast<float>(v);
}
}
void MouseHistory::ResetHistory()
{
	const float dilation = state_.timeDilation;
	state_ = {};
	state_.timeDilation = dilation;
}
float MouseHistory::Smooth(int axis, float value, std::uint8_t samples, float dt, float dilation)
{
	if (dilation != state_.timeDilation)
	{
		state_.timeDilation = dilation;
		ResetHistory();
	}
	if (dt >= .25f)
	{
		ResetHistory();
		return value;
	}
	const float period = f(double(state_.samplingTotal) / f(state_.sampleCount));
	if (value == 0)
	{
		state_.zeroTime[axis] = f(double(state_.zeroTime[axis]) + dt);
		if (state_.zeroTime[axis] < period)
			value = f(double(f(double(dt) * state_.previous[axis])) / period);
		else
			state_.previous[axis] = 0;
	}
	else
	{
		state_.zeroTime[axis] = 0;
		if (state_.previous[axis] != 0)
		{
			if (dt < f(double(f(samples + 1)) * period))
				value = f(double(f(double(dt) * value)) / f(double(f(samples)) * period));
			else
			{
				// The sample count wraps to a byte; a nonfinite quotient counts as zero.
				const double quotient = std::trunc(f(double(dt) / period));
				samples = std::isfinite(quotient) ? static_cast<std::uint8_t>(std::fmod(quotient, 256)) : 0;
			}
		}
		state_.previous[axis] = samples != 0 ? f(double(value) / f(samples)) : 0;
	}
	return value;
}
ProcessedMouseFrame MouseHistory::Process(
	const MouseFrame& frame, double applicationDelta, double cameraHorizontalFov, double timeDilation, bool xFirst)
{
	if (!std::isfinite(cameraHorizontalFov))
		throw std::invalid_argument("Camera horizontal FOV is not finite");
	ProcessedMouseFrame result;
	result.raw = frame.raw;
	const float dt = f(applicationDelta), dilation = f(timeDilation);
	for (int ordinal = 0; ordinal < 2; ++ordinal)
	{
		const int axis = xFirst ? ordinal : 1 - ordinal;
		if (axis == 0 && frame.raw[0] != 0 && state_.previous[0] != 0)
		{
			state_.samplingTotal = f(double(state_.samplingTotal) + dt);
			const std::uint32_t count = static_cast<std::uint32_t>(state_.sampleCount) + frame.samples[0];
			std::memcpy(&state_.sampleCount, &count, sizeof(count));
		}
		const float massaged = f(double(f(frame.raw[axis])) * f(.07));
		const float scaled = f(double(massaged) * f(double(f(cameraHorizontalFov)) * f(.01111)));
		result.processed[axis] = Smooth(axis, scaled, frame.samples[axis], dt, dilation);
	}
	return result;
}
}

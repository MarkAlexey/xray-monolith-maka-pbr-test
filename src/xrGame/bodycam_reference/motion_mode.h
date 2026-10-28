#pragma once
#include <cstdint>

namespace BodycamReference
{
enum class MotionMode : unsigned
{
	Classic,
	Reference
};
enum class ModeTransition
{
	None,
	DeferredAction,
	AwaitingReference,
	Changed
};

class MotionSelection
{
public:
	bool Request(unsigned mode)
	{
		if (mode > static_cast<unsigned>(MotionMode::Reference))
			return false;
		requested_ = static_cast<MotionMode>(mode);
		++requests_;
		return true;
	}
	// The actor adapter calls this once per frame, after the Reference host
	// exists and before the weapon is bound and the frame is solved. The
	// requested mode takes over here unless an authored action is still playing.
	// When the rest of that frame fails, DetachActor hands the camera back to
	// Classic and the request stays.
	ModeTransition CompleteBoundary(bool authoredActionActive, bool referenceReady)
	{
		if (requested_ == active_)
			return ModeTransition::None;
		if (authoredActionActive)
			return ModeTransition::DeferredAction;
		if (requested_ == MotionMode::Reference && !referenceReady)
			return ModeTransition::AwaitingReference;
		active_ = requested_;
		++generation_;
		return ModeTransition::Changed;
	}
	void DetachActor()
	{
		active_ = MotionMode::Classic;
		++generation_;
	}
	MotionMode Requested() const { return requested_; }
	MotionMode Active() const { return active_; }
	std::uint64_t Generation() const { return generation_; }
	// Counts the accepted requests, so that a failed start can wait for the next one.
	std::uint64_t Requests() const { return requests_; }

private:
	MotionMode requested_ = MotionMode::Classic;
	MotionMode active_ = MotionMode::Classic;
	std::uint64_t generation_ = 0, requests_ = 0;
};
}

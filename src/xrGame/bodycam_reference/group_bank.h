#pragma once
#include "group_candidates.h"
namespace BodycamReference
{
struct PreparedGroup
{
	GroupPreparation preparation;
	LocomotionSyncGroup current;
	std::optional<LocomotionSyncGroup> previous;
};
// Double-buffered locomotion sync group: one bank is built this frame while the other holds the previous frame.
class LocomotionGroupBank
{
public:
	void BeginUpdate();
	void Register(SyncRecord record);
	PreparedGroup Prepare(const std::map<std::string, MarkerState>&, std::array<std::string, 2> names);
	void Commit(const CandidatePublication&);
	void FinishTick();
	const std::optional<LocomotionSyncGroup>& Published() const { return banks_[1 - current_]; }

private:
	std::array<std::optional<LocomotionSyncGroup>, 2> banks_;
	unsigned current_ = 0;
};
}

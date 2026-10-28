#include "group_bank.h"
namespace BodycamReference
{
void LocomotionGroupBank::BeginUpdate()
{
	if (auto& group = banks_[current_])
	{
		auto names = std::move(group->preparation.validNames);
		group.emplace();
		group->preparation.validNames = std::move(names);
	}
}
void LocomotionGroupBank::Register(SyncRecord record)
{
	auto& group = banks_[current_];
	if (!group)
		group.emplace();
	group->preparation.records.push_back(std::move(record));
}
PreparedGroup LocomotionGroupBank::Prepare(const std::map<std::string, MarkerState>& nodes, std::array<std::string, 2> names)
{
	auto& group = banks_[current_].value();
	PreparedGroup out;
	out.previous = banks_[1 - current_];
	out.preparation = PrepareLocomotionGroup(group.preparation.records,
		out.previous ? std::optional<PreparedGroupSnapshot>(out.previous->preparation) : std::nullopt, nodes, names);
	group.preparation = { out.preparation.records, out.preparation.validNames };
	group.markerEnabled = out.preparation.markerEnabled;
	out.current = group;
	return out;
}
void LocomotionGroupBank::Commit(const CandidatePublication& publication)
{
	auto& group = banks_[current_].value();
	if (publication.markers)
		group.markerContext = *publication.markers;
	group.leaderIndex = int(publication.leaderIndex);
	group.previousRatio = publication.previousRatio;
	group.currentRatio = publication.currentRatio;
}
void LocomotionGroupBank::FinishTick()
{
	current_ = 1 - current_;
}
}

#include "group_candidates.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
CandidateResult TickLocomotionCandidates(const LocomotionSyncGroup& current, const std::optional<LocomotionSyncGroup>& previous,
	double deltaTime, std::map<std::string, MarkerState>& nodes, const CandidateTick& tick)
{
	const auto& records = current.preparation.records;
	const auto& names = current.preparation.validNames;
	const float dt = float(deltaTime);
	if (previous && !previous->preparation.records.empty() && previous->preparation.validNames.size() != 2)
		throw std::invalid_argument("Unsupported: the previous sync group had no marker names");
	const auto admitted = [&](const std::string& name) { return std::find(names.begin(), names.end(), name) != names.end(); };
	CandidateContext context;
	context.validNames = names;
	context.deltaTime = dt;
	context.previousRatio = context.currentRatio = previous ? previous->currentRatio : 0;
	if (previous && previous->markerContext.end && admitted(previous->markerContext.end->previousName) &&
		admitted(previous->markerContext.end->nextName))
		context.markers.start = previous->markerContext.end;
	const int oldIndex = previous ? previous->leaderIndex : -1;
	const SyncRecord* oldLeader = previous && oldIndex >= 0 && oldIndex < int(previous->preparation.records.size())
		? &previous->preparation.records[oldIndex]
		: nullptr;
	CandidateResult result{};
	bool leaderFound = false;
	unsigned leaderIndex = 0;
	const auto invoke = [&](unsigned index)
	{
		context = tick(records[index], context, nodes);
		result.events.push_back({ context.follower ? "follower" : "leader", index, records[index].nodeMarkerIdentity, context.previousRatio,
			context.currentRatio });
	};
	for (unsigned i = 0; i < records.size(); ++i)
	{
		const auto& r = records[i];
		context.follower = false;
		context.resync = false;
		if (r.kind == SyncPlayerKind::Sequence && oldLeader &&
			(r.nodeMarkerIdentity != oldLeader->nodeMarkerIdentity || r.historyIdentity != oldLeader->historyIdentity ||
				r.assetIdentity != oldLeader->assetIdentity))
		{
			context.markers.start.reset();
			result.events.push_back({ "sequence-start-reset", i, {}, {}, {} });
		}
		context.markers.passed.clear();
		invoke(i);
		leaderIndex = i;
		result.events.push_back({ "candidate-publication", i, {}, context.previousRatio, context.currentRatio });
		if (context.markers.end)
		{
			leaderFound = true;
			break;
		}
	}
	if (!previous || oldIndex != int(leaderIndex))
		for (unsigned i = leaderIndex + 1; i < records.size(); ++i)
		{
			auto& state = nodes.at(records[i].nodeMarkerIdentity);
			state.previous.index = kNoMarker;
			state.next.index = kNoMarker;
			result.events.push_back({ "follower-pair-reset", i, {}, {}, {} });
		}
	result.publication = { leaderIndex, context.previousRatio, context.currentRatio,
		leaderFound ? std::optional<GroupMarkerContext>(context.markers) : std::nullopt };
	if (leaderFound && leaderIndex + 1 < records.size())
	{
		if (!context.markers.start || !admitted(context.markers.start->previousName) || !admitted(context.markers.start->nextName))
			throw std::invalid_argument("Unsupported: followers without a valid start marker position");
		for (unsigned i = leaderIndex + 1; i < records.size(); ++i)
		{
			context.follower = true;
			context.resync = false;
			invoke(i);
		}
	}
	result.context = std::move(context);
	result.allCandidatesFailed = !leaderFound;
	return result;
}
}

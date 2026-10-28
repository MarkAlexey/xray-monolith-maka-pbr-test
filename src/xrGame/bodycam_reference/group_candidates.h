#pragma once
#include "group_preparation.h"
#include <functional>
namespace BodycamReference
{
struct GroupMarkerContext
{
	std::optional<MarkerPosition> start, end;
	std::vector<PassedMarker> passed;
};
struct LocomotionSyncGroup
{
	PreparedGroupSnapshot preparation;
	bool markerEnabled = false;
	int leaderIndex = -1;
	float previousRatio = 0, currentRatio = 0;
	GroupMarkerContext markerContext;
};
struct CandidateContext
{
	bool follower = false, resync = false;
	std::vector<std::string> validNames;
	float deltaTime = 0, movement = 0, previousRatio = 0, currentRatio = 0;
	GroupMarkerContext markers;
};
struct CandidateEvent
{
	std::string kind;
	unsigned index;
	std::string nodeMarkerIdentity;
	std::optional<float> previousRatio, currentRatio;
};
struct CandidatePublication
{
	unsigned leaderIndex;
	float previousRatio, currentRatio;
	std::optional<GroupMarkerContext> markers;
};
struct CandidateResult
{
	CandidatePublication publication;
	CandidateContext context;
	std::vector<CandidateEvent> events;
	bool allCandidatesFailed;
};
using CandidateTick = std::function<CandidateContext(const SyncRecord&, CandidateContext, std::map<std::string, MarkerState>&)>;
CandidateResult TickLocomotionCandidates(const LocomotionSyncGroup& current, const std::optional<LocomotionSyncGroup>& previous,
	double deltaTime, std::map<std::string, MarkerState>& nodes, const CandidateTick& tick);
}

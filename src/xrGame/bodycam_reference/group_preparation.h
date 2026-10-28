#pragma once
#include "marker_clock.h"
#include <map>
#include <optional>
namespace BodycamReference
{
enum class SyncPlayerKind
{
	BlendSpace,
	Sequence
};
struct SyncRecord
{
	std::string assetIdentity, nodeMarkerIdentity, historyIdentity;
	SyncPlayerKind kind = SyncPlayerKind::BlendSpace;
	float weight = 0;
	std::vector<std::string> markerNames;
	unsigned registrationIndex = 0;
	bool markerEligible = false;
};
struct PreparedGroupSnapshot
{
	std::vector<SyncRecord> records;
	std::vector<std::string> validNames;
};
struct GroupPairReset
{
	unsigned registrationIndex;
	std::string nodeMarkerIdentity, reason;
};
struct GroupPreparation
{
	std::vector<SyncRecord> records;
	std::vector<std::string> validNames;
	bool markerEnabled = false;
	std::map<std::string, MarkerState> nodeStates;
	std::vector<GroupPairReset> resets;
};
GroupPreparation PrepareLocomotionGroup(std::vector<SyncRecord> records, const std::optional<PreparedGroupSnapshot>& previous,
	const std::map<std::string, MarkerState>& nodes, std::array<std::string, 2> syncNames);
}

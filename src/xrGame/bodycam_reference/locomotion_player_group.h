#pragma once
#include "locomotion_player.h"
#include "group_bank.h"
#include "sprint_marker_tick.h"
#include "locomotion_properties.h"
namespace BodycamReference
{
struct SprintPlayerState
{
	MarkerState marker{ 0, { kNoMarker, 0 }, { kNoMarker, 0 } };
	SequenceMarkerHistory history;
	bool relevanceFlag = false, rateClampInitialized = false;
	float rateClampCurrent = 0;
	float blendWeight = 0;
	void Initialize(const std::function<void(SprintPlayerState&)>& updateExposedInputs);
};
struct BoundLocomotionRecord
{
	unsigned nodeId;
	SyncRecord sync;
	std::array<double, 2> processedInputs{};
	double recordRate = 0;
	float rootMotionWeight = 1;
};
struct PlayerAssetBinding
{
	unsigned nodeId;
	std::string assetIdentity, nodeMarkerIdentity, historyIdentity;
	std::vector<std::string> markerNames;
	bool valid = true, skeletonValid = true;
};
struct PlayerGraphContext
{
	double weight, rootMotionWeight;
};
// Registration of plain players: no montage, mirroring or inertialization.
bool RegisterBlendPlayer(LocomotionPlayerState&, const PlayerAssetBinding&, PlayerGraphContext, const LocomotionPropertyState&,
	const std::function<void(BoundLocomotionRecord)>&);
bool RegisterSprintPlayer(SprintPlayerState&, const PlayerAssetBinding&, PlayerGraphContext, const LocomotionPropertyState&,
	const std::function<void(BoundLocomotionRecord)>&);
struct PlayerGroupOutput
{
	unsigned nodeId, registrationIndex;
	CandidateContext context;
};
struct PlayerGroupTick
{
	std::vector<PlayerGroupOutput> outputs;
	std::optional<LocomotionSyncGroup> group;
	std::optional<PreparedGroup> preparation;
	std::optional<CandidateResult> candidates;
};
// Bindings refer to persistent owners whose addresses outlive this group.
class LocomotionPlayerGroup
{
public:
	LocomotionPlayerGroup(std::map<unsigned, LocomotionPlayer*> blend, std::map<unsigned, SprintPlayerState*> sprint,
		std::array<std::string, 2> names, std::array<float, 2> times);
	void BeginUpdate();
	void Register(BoundLocomotionRecord);
	PlayerGroupTick Tick(double deltaTime, std::array<std::string, 2> syncNames);

private:
	std::map<unsigned, LocomotionPlayer*> blend_;
	std::map<unsigned, SprintPlayerState*> sprint_;
	std::array<std::string, 2> names_;
	std::array<float, 2> times_;
	LocomotionGroupBank bank_;
	std::vector<BoundLocomotionRecord> records_;
};
}

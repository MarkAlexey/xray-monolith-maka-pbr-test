#pragma once
#include "cycle_graph.h"
#include "locomotion_player_group.h"
#include "directional_pose_evaluate.h"
#include "pose_asset_bank.h"
#include "locomotion_rotation.h"
#include <memory>
namespace BodycamReference
{
struct CycleBlendAsset
{
	PlayerAssetBinding binding;
	float startPosition;
	std::array<CookedGridPoint, 4> grid;
	std::array<GridSequence, 4> sequences;
	std::array<float, 2> filterWindows;
};
struct CyclePoseAssets
{
	std::map<unsigned, CycleBlendAsset> blend;
	std::map<unsigned, PlayerAssetBinding> sprint;
	std::map<std::string, MarkerSequence> sequences;
	std::map<std::string, CompressedLocomotionCurves> curves;
	std::map<std::string, std::set<std::string>> admittedCurves;
	std::map<unsigned, CompressedLocomotionCurves> sprintCurves;
	std::array<DirectionCurveKey, 3> changeDirection;
	std::array<unsigned, 8> cacheOrder;
	std::vector<int> profileIndices;
	std::vector<double> profileScales;
};
// The directional cycle players and their graph. The caller sequences BeginUpdate, Update, Tick,
// Evaluate and Publish. The assets are held by reference and must outlive the owner.
class CyclePoseOwner
{
public:
	CyclePoseOwner(const CyclePoseAssets&, const PoseAssetBank&);
	CyclePoseOwner(const CyclePoseOwner&) = delete;
	CyclePoseOwner& operator=(const CyclePoseOwner&) = delete;
	CyclePoseOwner(CyclePoseOwner&&) = delete;
	CyclePoseOwner& operator=(CyclePoseOwner&&) = delete;
	CycleGraphTrace Initialize(CycleTarget, GraphEpoch, CycleContext = {});
	void BeginUpdate();
	CycleGraphTrace Update(CycleTarget, GraphEpoch, CycleContext, const LocomotionPropertyState&, const LocomotionRotationState&,
		unsigned gait, const AnimationCurves& previousCurves, std::int64_t globalMarker);
	PlayerGroupTick Tick(double deltaTime);
	PoseCurveBundle Evaluate() const;
	void Publish();
	const std::array<float, 6>& YawTargets() const { return yaw_; }
	const DirectionalMachine& Directional() const { return graph_->Directional(); }
	DirectionalMachine& Directional() { return graph_->Directional(); }

private:
	void PlayerEvent(const CyclePlayerEvent&);
	const CyclePoseAssets& assets_;
	const PoseAssetBank& poses_;
	std::map<unsigned, LocomotionPlayer> blend_;
	std::map<unsigned, SprintPlayerState> sprint_;
	std::unique_ptr<LocomotionPlayerGroup> group_;
	std::unique_ptr<CycleDirectionalGraph> graph_;
	LocomotionPropertyState properties_;
	LocomotionRotationState rotations_;
	std::array<float, 6> yaw_{};
};
}

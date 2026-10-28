#pragma once
#include "cycle_pose_owner.h"
#include "detail_players.h"
#include "lean_player.h"
#include "footroot_pose.h"
#include "upper_pose.h"
#include "inertial_pose.h"
namespace BodycamReference
{
struct UpperFrameAssets
{
	NormalMachineDefinition normal;
	ChildMachineDefinition stop, detail;
	std::array<CookedGridPoint, 25> leanGrid;
	std::array<std::string, 5> leanIdentities;
	std::map<unsigned, CompressedLocomotionCurves> detailCurves, rotateCurves;
	std::array<std::string, 6> walkIdentities;
	std::vector<int> parents;
	std::array<std::vector<float>, 2> stopWeights;
};
struct UpperFrameInput
{
	GraphEpoch epoch;
	double dt;
	LocomotionEssential essential;
	LocomotionPropertyState properties;
	LocomotionRotationState rotations;
	std::optional<double> aimYawRate;
	bool recordDiagnostics = true;
};
struct UpperFrameResult
{
	PoseCurveBundle pose;
	UpperGraphTrace graph;
	unsigned playerTicks = 0, direction = 0;
	float amount = 0;
	// Left contact = 0, right contact = .5; absent outside the moving sync group.
	std::optional<double> footPhase;
	InertialPoseDiagnostic inertial;
};
// The upper-body locomotion graph. The host supplies movement and properties; the assets must outlive this object.
class LocomotionUpperFrame
{
public:
	LocomotionUpperFrame(const CyclePoseAssets&, const UpperFrameAssets&, const PoseAssetBank&);
	LocomotionUpperFrame(const LocomotionUpperFrame&) = delete;
	LocomotionUpperFrame& operator=(const LocomotionUpperFrame&) = delete;
	UpperFrameResult Update(const UpperFrameInput&);
	void PublishPrimaryCurves(NamedCurves);
	const NamedCurves& PublishedCurves() const { return published_; }

private:
	void Event(const UpperGraphEvent&);
	bool InitializeCache(int id, GraphEpoch epoch);
	PoseCurveBundle EvaluateCycles() const;
	PoseCurveBundle EvaluateDetailPlayer(unsigned id) const;
	const CyclePoseAssets& cycleAssets_;
	const UpperFrameAssets& assets_;
	const PoseAssetBank& poses_;
	CyclePoseOwner cycle_;
	LeanPlayer lean_;
	LeanAlpha leanAlpha_;
	FootRootAlpha footAlpha_;
	FreshFootRoot footPose_;
	DetailPlayers detail_;
	StopPlantDispatch plants_;
	NormalIdlePoseOwner idle_;
	RotateInPlaceLatch rotate_;
	std::map<unsigned, RotateSequencePlayer> rotatePlayers_;
	std::map<unsigned, float> rotateScales_;
	MachineWeightPublication machineWeights_{ 8 };
	PivotNotifyState pivot_;
	std::map<int, bool> cacheInitialized_;
	std::map<int, GraphEpoch> cacheUpdate_;
	std::unique_ptr<UpperLocomotionGraph> graph_;
	MovementInertialReceiver inertial_;
	std::vector<LeanRegistration> leanRecords_;
	std::vector<RotateSequenceRecord> rotateRecords_;
	NamedCurves published_;
	AnimationCurves prior_;
	// The frame being updated; graph events read it.
	const UpperFrameInput* input_ = nullptr;
};
}

#pragma once
#include "locomotion_children.h"
#include "directional_cache.h"
#include "rotate_player.h"
#include <map>
namespace BodycamReference
{
const std::array<unsigned, 4>& DetailPlayerOrder(unsigned state);
struct DetailPlayerState
{
	RelevantSequenceSnapshot sequence;
	bool relevanceFlag = false;
};
struct DetailSyncRecord
{
	unsigned group, playerId; // group: 0 run start, 1 first pivot, 2 second pivot.
	float weight;
};
struct DetailPlayerDispatch
{
	std::array<float, 4> weights;
	std::vector<LocomotionStateUpdate> cyclesQueue;
	std::vector<DetailSyncRecord> records;
};
struct DetailTickCommit
{
	unsigned registrationIndex, playerId;
	float time, previousTime, delta;
};
struct DetailTickGroup
{
	unsigned group, leaderId;
	float previousRatio, currentRatio;
	bool outside;
	std::vector<DetailTickCommit> commits;
};
class DetailPlayers
{
public:
	DetailPlayers();
	void ResetMachine()
	{
		current_ = 0;
		elapsed_ = 0;
	}
	void InitializeState(unsigned);
	bool ChangeState(unsigned);
	void CompleteMachineUpdate(double dt);
	DetailRuleInputs RuleInputs() const;
	DetailPlayerDispatch Dispatch(const std::vector<LocomotionStateUpdate>&, std::array<float, 4> velocity);
	std::vector<DetailTickGroup> Tick(const std::vector<DetailSyncRecord>&, double dt);
	const std::map<unsigned, DetailPlayerState>& Players() const { return players_; }
	const std::array<float, 4>& Weights(unsigned state) const;

private:
	struct WeightState
	{
		std::array<float, 4> desired{};
		std::optional<std::array<float, 4>> actual;
	};
	std::map<unsigned, DetailPlayerState> players_;
	std::map<unsigned, WeightState> weights_;
	unsigned current_ = 0;
	float elapsed_ = 0;
};
class MachineWeightPublication
{
public:
	explicit MachineWeightPublication(unsigned count) { Initialize(count); }
	void Initialize(unsigned count);
	void Prepare();
	void Write(unsigned index, double weight);
	float Read(unsigned index) const;
	bool Publish();

private:
	std::array<std::vector<float>, 2> banks_;
	unsigned current_ = 0;
	bool pending_ = false;
};
class PivotNotifyState
{
public:
	void Notify(double speed, double limit = 200);
	bool Tick(double dt);
	bool Pivot() const { return pivot_; }
	std::optional<float> Remaining() const { return remaining_; }

private:
	bool pivot_ = false;
	std::optional<float> remaining_;
};
}

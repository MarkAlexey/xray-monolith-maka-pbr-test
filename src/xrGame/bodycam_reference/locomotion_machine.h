#pragma once
#include "montage_blend.h"
#include "gait_blend.h"
#include <variant>
#include <array>
#include <optional>
#include <vector>

namespace BodycamReference
{
float LocomotionTransitionDuration(double duration, double destinationWeight);
using LocomotionBlend = std::variant<MontageBlend, CubicRangeBlend>;
struct LocomotionTransitionChoice
{
	unsigned next;
	double duration;
	bool inertial = false;
};
struct LocomotionTransitionRecord
{
	unsigned id, previous, next;
	float duration, elapsed = 0, alpha = 0;
	bool active = true;
	LocomotionBlend blend;
};
struct LocomotionStateUpdate
{
	unsigned state;
	float weight;
	bool activeBranch = false;
};
struct LocomotionTransitionStart
{
	unsigned id, previous, next;
	float duration;
};
struct LocomotionMachineTrace
{
	std::vector<float> publishedBefore, currentWeights;
	std::vector<LocomotionTransitionStart> started;
	std::vector<unsigned> initializedStates, skippedFirstRecords, completed, removed;
	std::vector<LocomotionStateUpdate> updates, contextUpdates;
	unsigned currentState;
	bool cyclesUpdated = false;
};
// State machine with transition records and double-buffered weight publication. The default
// transition rules are the three-state Idle/Moving/Stop machine.
class LocomotionMachine
{
public:
	explicit LocomotionMachine(unsigned currentState = 0, std::vector<float> publishedWeights = { 0, 0, 0 }, bool firstUpdate = true);
	virtual ~LocomotionMachine() = default;
	virtual void Reinitialize(unsigned initialState = 0);
	unsigned PublicationBank() const { return writeBank_; }
	void PrepareProxyUpdate();
	void PublishProxyUpdate();
	float PublicWeight(unsigned state) const;
	float InternalWeight(unsigned state) const;
	LocomotionMachineTrace Update(double dt, bool shouldMove, double parentWeight = 1, bool activeBranch = true);
	void RecordDiagnostics(bool enabled) { recordDiagnostics_ = enabled; }
	bool Publish();
	unsigned CurrentState() const { return currentState_; }
	const std::vector<LocomotionTransitionRecord>& Records() const { return records_; }

protected:
	LocomotionMachine(unsigned count, unsigned currentState, std::vector<float> publishedWeights, bool firstUpdate, unsigned limit);
	virtual std::optional<LocomotionTransitionChoice> SelectTransition(bool shouldMove);
	virtual float Duration(const LocomotionTransitionChoice& choice, float destinationWeight) const;
	virtual LocomotionBlend NewBlend(const LocomotionTransitionChoice& choice, float duration) const;
	virtual float StackAlpha(const LocomotionTransitionRecord& record) const;
	virtual void AfterStateChange(unsigned, unsigned, bool, float) {}
	unsigned stateCount_, currentState_, transitionLimit_;
	bool firstUpdate_;

private:
	std::array<std::vector<float>, 2> banks_;
	unsigned writeBank_ = 0, nextRecordId_ = 0;
	bool pendingPublication_ = false;
	bool recordDiagnostics_ = true;
	std::vector<LocomotionTransitionRecord> records_;
};
}

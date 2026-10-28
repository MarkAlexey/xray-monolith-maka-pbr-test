#include "upper_frame.h"
#include "scalar_math.h"
#include "recoil_pose.h"
#include "additive_pose.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
constexpr CycleTarget kCycleTarget{ true, kCycleRootNode };
// Normal state 3 rotates left, state 4 right; their players list them as 653 - node id.
constexpr unsigned kRotateLeftNode = 544, kRotateRightNode = 541, kNormalPlayerNodeBase = 653;
unsigned RotateNode(unsigned state)
{
	return state == 3 ? kRotateLeftNode : kRotateRightNode;
}
// Slots of the machine weight publication.
constexpr unsigned kDetailWeightSlot = 5, kGroundedWeightSlot = 7;
LeanPlayerState InitialLean()
{
	LeanPlayerState s;
	s.node.previous.offset = s.node.next.offset = 0.f;
	return s;
}
}
LocomotionUpperFrame::LocomotionUpperFrame(const CyclePoseAssets& cycle, const UpperFrameAssets& assets, const PoseAssetBank& poses)
	: cycleAssets_(cycle), assets_(assets), poses_(poses), cycle_(cycle, poses), lean_(assets.leanGrid, InitialLean()),
	  footPose_(assets.parents, 61, true)
{
	for (unsigned state : { 3, 4 })
	{
		rotatePlayers_.emplace(state, RotateSequencePlayer(RotateNode(state)));
		rotateScales_[state] = 0;
	}
	for (int id : { kDetailCacheNode, kCyclesCacheNode })
	{
		cacheInitialized_[id] = false;
		cacheUpdate_[id] = {};
	}
	UpperGraphHooks hooks;
	hooks.resolveCacheInitialize = [this](int id, const std::string&, GraphEpoch epoch) { return InitializeCache(id, epoch); };
	hooks.initializeStopState = [this](unsigned state, const auto& base)
	{
		plants_.InitializeState(state,
			[&](const auto& e)
			{
				if (e.kind == StopEventKind::InitializeCache)
					base();
			});
	};
	hooks.onEvent = [this](const auto& e) { Event(e); };
	graph_ = std::make_unique<UpperLocomotionGraph>(
		assets.normal, assets.stop, assets.detail, std::move(hooks), std::vector<LocomotionMachine*>{ &cycle_.Directional() });
}
bool LocomotionUpperFrame::InitializeCache(int id, GraphEpoch epoch)
{
	auto& initialized = cacheInitialized_.at(id);
	if (!initialized)
	{
		initialized = true;
		return true;
	}
	const auto update = cacheUpdate_.at(id);
	if (update.counter == kNoGraphCounter || update.frame == -1 || update.counter == epoch.counter)
		return false;
	return epoch.frame == -1 || NextGraphCounter(update.counter) != epoch.counter;
}
void LocomotionUpperFrame::Event(const UpperGraphEvent& e)
{
	const auto& in = *input_;
	if (e.type == UpperGraphEventKind::FlushCache)
		cacheUpdate_.at(e.cacheId) = in.epoch;
	if (e.type == UpperGraphEventKind::InitializeMachine && e.machineIndex == 2)
		detail_.ResetMachine();
	if (e.type == UpperGraphEventKind::InitializeCycles)
		leanAlpha_.Initialize([&] { cycle_.Initialize(kCycleTarget, { 1, 1 }); }, [&] { lean_.Initialize(); });
	if (e.type == UpperGraphEventKind::InitializeDetailState && (e.state == 1 || e.state == 3 || e.state == 4 || e.state == 6))
		detail_.InitializeState(unsigned(e.state));
	if (e.type == UpperGraphEventKind::NormalPlayerReset)
		for (unsigned baked : e.change->playerNodes)
			for (auto& player : rotatePlayers_)
				if (kNormalPlayerNodeBase - baked == RotateNode(player.first))
					player.second.ResetBlendWeight();
	if (e.type == UpperGraphEventKind::InitializeNormalState && rotatePlayers_.count(unsigned(e.state)))
		rotatePlayers_.at(unsigned(e.state)).Initialize(rotate_.NodeInputs(e.state == 3));
	if (e.type == UpperGraphEventKind::NormalStateUpdate)
	{
		const auto& c = *e.context;
		if (c.state == 0)
			idle_.Update([] {}, [] { return 0; });
		if (rotatePlayers_.count(c.state))
		{
			const auto inputs = rotate_.NodeInputs(c.state == 3);
			rotateRecords_.push_back(rotatePlayers_.at(c.state).Update(c.weight, 1, inputs));
			rotateScales_[c.state] = inputs.playRate;
		}
	}
	if (e.type == UpperGraphEventKind::UpdateCycles)
	{
		const auto& c = *e.context;
		leanAlpha_.Update(
			e.deltaTime, c, true, true,
			[&](const auto& context)
			{
				footAlpha_.Update(
					context, true,
					[&](const auto& child)
					{
						cycle_.Update(kCycleTarget, in.epoch, { child.weight, 1, in.dt, child.activeBranch, false }, in.properties,
							in.rotations, unsigned(in.essential.gait), prior_, in.epoch.frame);
					},
					[&] { return in.properties.diagonalScaleAmount; });
			},
			[] {}, [&] { return CurveValue(prior_, "Weight_Gait"); },
			[&](const auto& context)
			{
				lean_.Register(context.weight, 1, in.properties.leanAmount[0], in.properties.leanAmount[1],
					[&](const auto& record) { leanRecords_.push_back(record); });
			});
	}
}
PoseCurveBundle LocomotionUpperFrame::EvaluateCycles() const
{
	return EvaluateLocalAdditive(
		leanAlpha_.actual, [&] { return footPose_.Evaluate(footAlpha_.actual, [&] { return cycle_.Evaluate(); }); },
		[&]
		{
			std::vector<WeightedPackedPose> samples;
			for (const auto& s : lean_.State().samples)
				samples.push_back({ std::clamp(s.weight, 0.f, 1.f),
					PackPose(poses_.Sample(assets_.leanIdentities.at(s.sampleIndex), std::clamp(s.time, 0.f, .033333335f))) });
			return PoseCurveBundle{ LeanPackedPose(samples), {} };
		});
}
PoseCurveBundle LocomotionUpperFrame::EvaluateDetailPlayer(unsigned id) const
{
	unsigned direction = 0;
	for (unsigned state : { 1, 3, 4, 6 })
	{
		const auto& order = DetailPlayerOrder(state);
		const auto it = std::find(order.begin(), order.end(), id);
		if (it != order.end())
		{
			direction = unsigned(it - order.begin());
			break;
		}
	}
	const float time = std::clamp(detail_.Players().at(id).sequence.accumulator, 0.f, 1.f);
	const std::array<const char*, 4> directions = { "F", "B", "L", "R" };
	return { PackPose(poses_.Sample(std::string("detail:") + directions[direction], time)),
		assets_.detailCurves.at(direction).Sample(time, { "Mask_Lean" }) };
}
UpperFrameResult LocomotionUpperFrame::Update(const UpperFrameInput& in)
{
	input_ = &in;
	leanRecords_.clear();
	rotateRecords_.clear();
	pivot_.Tick(in.dt);
	machineWeights_.Prepare();
	machineWeights_.Write(kGroundedWeightSlot, 1);
	const double angle = WrapDegrees(in.essential.aimingRotation.yaw - in.essential.actorRotation.yaw);
	if (!in.properties.shouldMove && std::abs(angle) > kRotateAngle && !in.aimYawRate)
		throw std::invalid_argument("Rotate entry needs the aim yaw rate");
	if (in.properties.shouldMove)
		rotate_.ClearFlags();
	else
		rotate_.UpdateStationary(1, unsigned(in.essential.rotationMode), false, angle, in.aimYawRate.value_or(0));
	prior_.clear();
	for (const auto& e : published_)
		prior_[e.name] = e.value;
	cycle_.BeginUpdate();
	UpperGraphInput input;
	input.recordEvents = false;
	input.epoch = in.epoch;
	input.globalFrameMarker = in.epoch.frame;
	input.dt = in.dt;
	input.shouldMove = in.properties.shouldMove;
	CachedLocomotionContext context;
	context.weight = 1;
	context.activeBranch = true;
	input.normalContext = context;
	input.priorCurves = &prior_;
	input.normalInputs = [&]
	{
		return NormalGraphInputs{ rotate_.Left(), rotate_.Right(), [&](unsigned state, const auto&, const auto& transition)
			{
				return AutomaticSequenceTransition({ rotatePlayers_.at(state).RelevantSnapshot() }, -1, transition.duration, false).canTake;
			} };
	};
	input.detailInputs = [&]
	{
		auto out = detail_.RuleInputs();
		out.gait = unsigned(in.essential.gait);
		out.mainGroundedWeight = machineWeights_.Read(kGroundedWeightSlot);
		out.detailWeight = machineWeights_.Read(kDetailWeightSlot);
		out.pivot = pivot_.Pivot();
		return out;
	};
	UpperFrameResult result;
	result.graph = graph_->Update(input);
	std::vector<LocomotionStateUpdate> stopContexts;
	for (const auto& stop : result.graph.stop)
		stopContexts.insert(stopContexts.end(), stop.machine.contextUpdates.begin(), stop.machine.contextUpdates.end());
	std::array<float, 4> velocity;
	for (unsigned i = 0; i < 4; ++i)
		velocity[i] = float(in.properties.velocityBlend[i]);
	plants_.Update(stopContexts, velocity, 0, in.dt);
	std::vector<DetailSyncRecord> detailRecords;
	if (result.graph.detail)
	{
		machineWeights_.Write(kDetailWeightSlot, result.graph.detailWinner->weight);
		detail_.ChangeState(result.graph.detail->machine.currentState);
		detailRecords = detail_.Dispatch(result.graph.detail->machine.contextUpdates, velocity).records;
		detail_.CompleteMachineUpdate(in.dt);
	}
	const auto cycleTick = cycle_.Tick(in.dt);
	result.playerTicks = unsigned(cycleTick.outputs.size());
	if (in.properties.shouldMove && cycleTick.group && cycleTick.group->markerContext.end)
	{
		const auto& marker = *cycleTick.group->markerContext.end;
		if ((marker.previousName == "Left" && marker.nextName == "Right") || (marker.previousName == "Right" && marker.nextName == "Left"))
			result.footPhase = std::fmod((marker.previousName == "Right" ? .5 : 0.) + .5 * std::clamp(double(marker.fraction), 0., 1.), 1.);
	}
	for (const auto& record : leanRecords_)
		lean_.Tick(in.dt, record);
	detail_.Tick(detailRecords, in.dt);
	for (const auto& record : rotateRecords_)
		rotatePlayers_.at(record.nodeId == kRotateLeftNode ? 3 : 4).Tick(record, in.dt);
	std::map<unsigned, PoseCurveBundle> cache;
	const auto read = [&](unsigned id, const std::function<PoseCurveBundle()>& evaluate)
	{
		auto it = cache.find(id);
		if (it == cache.end())
			it = cache.emplace(id, evaluate()).first;
		return it->second;
	};
	const auto reference = [&] { return PoseCurveBundle{ PackPose(poses_.InitialPose()), {} }; };
	const auto detail = [&]
	{
		return EvaluateStatePoseStack(
			7, graph_->Detail().CurrentState(), graph_->Detail().Records(),
			[&](unsigned state)
			{
				const auto weights = state == 0 || state == 2 ? std::array<float, 4>{} : detail_.Weights(state);
				return EvaluateDetailState(
					state, 1, weights, [&] { return read(kCyclesCacheNode, [&] { return EvaluateCycles(); }); },
					[&](unsigned id) { return EvaluateDetailPlayer(id); }, reference);
			},
			[](const auto&, unsigned, std::size_t count) { return TransitionPoseProfile{ std::vector<int>(count, -1), {} }; });
	};
	result.pose = EvaluateNormalPose(
		graph_->Normal(), graph_->Stop(), plants_, read,
		[&]
		{
			return idle_.Evaluate([](const auto& source) { return source(); },
				[&] { return PoseCurveBundle{ PackPose(poses_.Sample("idle:base", 0)), {} }; });
		},
		[&](unsigned state)
		{
			const float time = rotatePlayers_.at(state).State().accumulator;
			return PoseCurveBundle{ PackPose(poses_.Sample("rotate:" + std::to_string(state), time)),
				ScaleNamedCurve(
					assets_.rotateCurves.at(state).Sample(time, { "RotationAmount", "FootLock_L", "FootLock_R", "Enable_Transition" }),
					"RotationAmount", rotateScales_.at(state)) };
		},
		detail,
		[&](StopAsset asset, float time, unsigned)
		{
			const auto& identity = assets_.walkIdentities.at(unsigned(asset));
			return PoseCurveBundle{ PackPose(poses_.Sample(identity, time)),
				cycleAssets_.curves.at(identity).Sample(time, cycleAssets_.admittedCurves.at(identity)) };
		},
		reference, assets_.parents, assets_.stopWeights);
	for (const auto* name : { "BasePose_N", "Enable_FootIK_L", "Enable_FootIK_R" })
		result.pose.curves = BlendNamedCurve(std::move(result.pose.curves), name, 1);
	std::vector<InertialRequest> requests;
	if (result.graph.normal)
		for (float duration : result.graph.normal->inertialDurations)
			requests.push_back({ duration, 1, 2 });
	if (result.graph.detail)
		for (float duration : result.graph.detail->inertialDurations)
			requests.push_back({ duration, 1, 2 });
	inertial_.Update(in.dt, requests);
	const auto component = RecoilQuaternion({ 0, in.essential.actorRotation.yaw - 90, 0 });
	result.pose = inertial_.Evaluate(result.pose, component, in.recordDiagnostics);
	result.inertial = inertial_.Diagnostic();
	published_ = result.pose.curves;
	result.amount = graph_->Normal().InternalWeight(1) + graph_->Normal().InternalWeight(2);
	result.direction = cycle_.Directional().CurrentState();
	graph_->Publish();
	machineWeights_.Publish();
	return result;
}
void LocomotionUpperFrame::PublishPrimaryCurves(NamedCurves curves)
{
	published_ = std::move(curves);
}
}

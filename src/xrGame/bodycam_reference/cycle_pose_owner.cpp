// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "cycle_pose_owner.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
const std::array<std::string, 2> kFootMarkers{ "Left", "Right" };
constexpr std::array<float, 2> kFootMarkerTimes{ .053792756f, .35397556f };
}
CyclePoseOwner::CyclePoseOwner(const CyclePoseAssets& assets, const PoseAssetBank& poses) : assets_(assets), poses_(poses)
{
	if (assets.blend.size() != kDirectionPlayerNodes.size() || assets.sprint.size() != kSprintPlayerNodes.size() ||
		assets.profileIndices.size() != poses.InitialPose().size())
		throw std::invalid_argument("Cycle assets must bind six blend players, two sprint players and a profile index per bone");
	std::map<unsigned, LocomotionPlayer*> blendBindings;
	std::map<unsigned, SprintPlayerState*> sprintBindings;
	for (unsigned id : kDirectionPlayerNodes)
	{
		const auto& a = assets.blend.at(id);
		LocomotionPlayerState state;
		state.node.previous.offset = state.node.next.offset = 0.f;
		auto it = blend_
					  .emplace(id,
						  LocomotionPlayer(a.binding.assetIdentity, a.startPosition, LocomotionGrid(a.grid, a.sequences), a.filterWindows,
							  assets.sequences, state))
					  .first;
		blendBindings[id] = &it->second;
	}
	for (unsigned id : kSprintPlayerNodes)
	{
		(void)assets.sprint.at(id);
		sprintBindings[id] = &sprint_.emplace(id, SprintPlayerState{}).first->second;
	}
	group_ = std::make_unique<LocomotionPlayerGroup>(blendBindings, sprintBindings, kFootMarkers, kFootMarkerTimes);
	CycleGraphHooks hooks;
	hooks.resolveCacheInitialize = [](unsigned, const std::string&) { return false; };
	hooks.refreshBarrier = [](const CycleTarget&) {};
	hooks.onPlayer = [this](const CyclePlayerEvent& e) { PlayerEvent(e); };
	hooks.initializeStateRoot = [](unsigned, const std::function<void()>& source) { source(); };
	hooks.updateStateRoot = [this](unsigned state, const std::function<void()>& source)
	{
		source();
		const std::array<double, 6> targets = { rotations_.fYaw, rotations_.bYaw, rotations_.rYaw, rotations_.rYaw, rotations_.lYaw,
			rotations_.lYaw };
		yaw_.at(state) = float(targets[state]);
	};
	graph_ = std::make_unique<CycleDirectionalGraph>(ChangeDirectionCurve(assets.changeDirection), assets.cacheOrder, std::move(hooks));
}
void CyclePoseOwner::PlayerEvent(const CyclePlayerEvent& e)
{
	const bool isSprint = sprint_.count(e.nodeId) != 0;
	if (e.initialize)
	{
		if (isSprint)
			sprint_.at(e.nodeId).Initialize([](auto&) {});
		else
			blend_.at(e.nodeId).Initialize([](auto&) {});
		return;
	}
	const auto& c = e.context.value();
	const PlayerGraphContext context{ c.weight, c.rootMotionWeight };
	const auto emit = [this](BoundLocomotionRecord record) { group_->Register(std::move(record)); };
	if (isSprint)
		RegisterSprintPlayer(sprint_.at(e.nodeId), assets_.sprint.at(e.nodeId), context, properties_, emit);
	else
		RegisterBlendPlayer(blend_.at(e.nodeId).State(), assets_.blend.at(e.nodeId).binding, context, properties_, emit);
}
CycleGraphTrace CyclePoseOwner::Initialize(CycleTarget target, GraphEpoch epoch, CycleContext context)
{
	return graph_->Initialize(target, [epoch]() { return epoch; }, context);
}
void CyclePoseOwner::BeginUpdate()
{
	group_->BeginUpdate();
}
CycleGraphTrace CyclePoseOwner::Update(CycleTarget target, GraphEpoch epoch, CycleContext context,
	const LocomotionPropertyState& properties, const LocomotionRotationState& rotation, unsigned gait, const AnimationCurves& curves,
	std::int64_t marker)
{
	properties_ = properties;
	rotations_ = rotation;
	CycleProperties values;
	values.direction = unsigned(rotation.movementDirection);
	values.gait = gait;
	for (unsigned i = 0; i < 4; ++i)
		values.velocity[i] = float(properties.velocityBlend[i]);
	values.relativeAccelerationX = properties.relativeAcceleration[0];
	return graph_->Update([target]() { return target; }, [epoch]() { return epoch; }, context, values, curves, marker);
}
PlayerGroupTick CyclePoseOwner::Tick(double dt)
{
	return group_->Tick(dt, kFootMarkers);
}
PoseCurveBundle CyclePoseOwner::Evaluate() const
{
	std::map<unsigned, PoseCurveBundle> cache, leaves;
	const auto snapshot = graph_->EvaluationSnapshot();
	const TwoWayPoseState sprint{ graph_->Sprint().Alpha(), graph_->Sprint().Relevant() };
	const auto player = [&](unsigned id)
	{
		auto it = leaves.find(id);
		if (it != leaves.end())
			return it->second;
		PoseCurveBundle out;
		if (sprint_.count(id))
		{
			const float time = std::clamp(sprint_.at(id).marker.time, 0.f, kSprintCycleSeconds);
			out.pose = PackPose(poses_.Sample("sprint:" + std::to_string(id), time));
			out.curves = assets_.sprintCurves.at(id).Sample(time, { "Weight_Gait" });
		}
		else
		{
			const auto& source = blend_.at(id);
			std::vector<BlendMarkerSample> samples;
			for (const auto& s : source.State().samples)
				samples.push_back(s.marker);
			out.pose = poses_.Evaluate(samples);
			out.curves = source.Evaluate(assets_.curves, assets_.admittedCurves);
		}
		return leaves.emplace(id, std::move(out)).first->second;
	};
	const auto readCache = [&](unsigned id, const std::function<PoseCurveBundle()>& evaluate)
	{
		const auto found = cache.find(id);
		if (found != cache.end())
			return found->second;
		return cache.emplace(id, evaluate()).first->second;
	};
	const auto& directional = graph_->Directional();
	return EvaluateStatePoseStack(
		6, directional.CurrentState(), directional.Records(),
		[&](unsigned state)
		{
			auto out = EvaluateDirectionalInput(
				state, snapshot, sprint, player, readCache, [&]() { return PoseCurveBundle{ PackPose(poses_.InitialPose()), {} }; });
			auto& yaw = FindOrAddCurve(out.curves, "YawOffset");
			const float old = yaw.value;
			yaw.value = (yaw_[state] - old) * 1.f + old;
			return out;
		},
		[&](const LocomotionTransitionRecord& record, unsigned, std::size_t)
		{
			TransitionPoseProfile profile;
			profile.indices = assets_.profileIndices;
			for (double scale : assets_.profileScales)
				profile.weights.push_back(WeightFactorPair(record.alpha, scale));
			return profile;
		});
}
void CyclePoseOwner::Publish()
{
	graph_->Directional().Publish();
}
}

// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "upper_pose.h"
#include "constants.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
// A full-weight blend; (target - value) + value is not always target in float.
void BlendCurveValue(NamedCurveEntry& entry, float target)
{
	entry.value = (target - entry.value) + entry.value;
}
}
NamedCurves BlendNamedCurve(NamedCurves curves, const std::string& name, float target)
{
	BlendCurveValue(FindOrAddCurve(curves, name), target);
	return curves;
}
NamedCurves ScaleNamedCurve(NamedCurves curves, const std::string& name, float scale)
{
	auto& entry = FindOrAddCurve(curves, name);
	BlendCurveValue(entry, entry.value * scale);
	return curves;
}
void NormalIdlePoseOwner::Update(const std::function<void()>& source, const std::function<double()>& scale)
{
	source();
	rotationScale_ = float(scale());
}
PoseCurveBundle NormalIdlePoseOwner::Evaluate(
	const std::function<PoseCurveBundle(const std::function<PoseCurveBundle()>&)>& slot, const std::function<PoseCurveBundle()>& idle) const
{
	auto out = slot(
		[&]
		{
			auto pose = idle();
			for (const auto* name : { "FootLock_L", "FootLock_R", "Enable_Transition" })
				pose.curves = BlendNamedCurve(std::move(pose.curves), name, 1);
			return pose;
		});
	out.curves = ScaleNamedCurve(std::move(out.curves), "RotationAmount", rotationScale_);
	return out;
}
Quaternion StopMeshRoot(Quaternion q)
{
	const double x = q[0], y = q[1], z = q[2], w = q[3], a = y * 0, b = x * 0, c = w * 0, d = z * 0;
	const double zz = (a + z) - x * 0, ww = (b * -1 + w) - y * 0, xx = x + c, yy = y + d * -1;
	return { a * -1 + (z * 0 + xx), b + (w * 0 + yy), zz + c, ww + d * -1 };
}
Quaternion StopMeshCompose(Quaternion q, Quaternion p)
{
	const double x = q[0], y = q[1], z = q[2], w = q[3], px = p[0], py = p[1], pz = p[2], pw = p[3];
	return { ((w * px + x * pw) + z * py) + (y * pz) * -1, ((z * px) * -1 + y * pw) + w * py + x * pz,
		((y * px + z * pw) - x * py) + w * pz, (((x * px) * -1 + w * pw) - y * py) + (z * pz) * -1 };
}
Quaternion StopMeshToLocal(Quaternion q, Quaternion p)
{
	const double x = q[0], y = q[1], z = q[2], w = q[3], px = p[0], py = p[1], nz = p[2] * -1, pw = p[3];
	const auto t = NormalizePackedTransform({ ((x * pw - w * px) - z * py) + (y * nz) * -1, ((y * pw - (z * px) * -1) - w * py) + x * nz,
		((z * pw - y * px) + x * py) + w * nz, ((w * pw - (x * px) * -1) + y * py) + (z * nz) * -1 });
	return { t[0], t[1], t[2], t[3] };
}
PackedPose BlendStopMeshBinary(
	const PackedPose& base, const PackedPose& child, const std::vector<int>& parents, const std::vector<float>& weights)
{
	const auto count = base.size();
	if (!count || child.size() != count || parents.size() != count || weights.size() != count || parents[0] != -1)
		throw std::invalid_argument("Matching stop compact poses, parents and binary weights required");
	std::vector<Quaternion> baseMesh(count), childMesh(count), outputMesh(count);
	PackedPose out(count);
	const auto rotation = [](const PackedTransform& t) { return Quaternion{ t[0], t[1], t[2], t[3] }; };
	for (unsigned i = 0; i < count; ++i)
	{
		const int p = parents[i];
		if ((weights[i] != 0 && weights[i] != 1) || (i && (p < 0 || p >= int(i))))
			throw std::invalid_argument("Binary mask and root-first parents required");
		baseMesh[i] = p < 0 ? StopMeshRoot(rotation(base[i])) : StopMeshCompose(rotation(base[i]), baseMesh[p]);
		childMesh[i] = p < 0 ? StopMeshRoot(rotation(child[i])) : StopMeshCompose(rotation(child[i]), childMesh[p]);
		outputMesh[i] = weights[i] ? childMesh[i] : baseMesh[i];
		out[i] = weights[i] ? child[i] : base[i];
		if (p >= 0)
		{
			const auto q = StopMeshToLocal(outputMesh[i], outputMesh[p]);
			std::copy(q.begin(), q.end(), out[i].begin());
		}
	}
	return out;
}
PoseCurveBundle EvaluateStopPlant(
	unsigned state, const StopPlantSnapshot& snapshot, const StopWalkEvaluator& walk, const std::function<PoseCurveBundle()>& reference)
{
	const auto& config = StopPlantEvaluators(state);
	std::array<std::optional<PoseCurveBundle>, 4> memo;
	const auto evaluate = [&](unsigned i)
	{
		const auto& e = config[i];
		return walk(e.asset, float(e.time), e.node);
	};
	const auto child = [&](unsigned i) -> const PoseCurveBundle&
	{
		if (!memo[i])
		{
			if (i < 2)
				memo[i] = evaluate(i);
			else
			{
				std::array<std::optional<PoseCurveBundle>, 2> leaves;
				const auto leaf = [&](unsigned j) -> const PoseCurveBundle&
				{
					if (!leaves[j])
						leaves[j] = evaluate(2 + (i - 2) * 2 + j);
					return *leaves[j];
				};
				const auto weights = i == 2 ? snapshot.left : snapshot.right;
				auto pose = GaitPackedPose(weights, [&](unsigned j) { return leaf(j).pose; }, [&] { return reference().pose; });
				std::vector<WeightedNamedCurves> curves;
				for (unsigned j = 0; j < 2; ++j)
					if (weights[j] > kBlendEpsilon)
						curves.push_back({ weights[j], leaf(j).curves });
				auto blended = curves.size() == 1 && curves[0].weight >= kBlendFull ? curves[0].curves : MultiWayCurves(curves);
				memo[i] = PoseCurveBundle{ std::move(pose), std::move(blended) };
			}
		}
		return *memo[i];
	};
	auto pose = MultiWayPackedPose(snapshot.weights, [&](unsigned i) { return child(i).pose; }, [&] { return reference().pose; });
	std::vector<WeightedNamedCurves> curves;
	for (unsigned i = 0; i < 4; ++i)
		curves.push_back({ snapshot.weights[i], memo[i] ? memo[i]->curves : NamedCurves{} });
	return { std::move(pose), MultiWayCurves(curves) };
}
PoseCurveBundle EvaluateStopState(unsigned state, bool relevant, const std::vector<int>& parents, const std::vector<float>& weights,
	const std::function<PoseCurveBundle()>& detail, const std::function<PoseCurveBundle()>& plant)
{
	auto out = detail();
	if ((state == 5 || state == 6) && relevant)
	{
		const auto child = plant();
		out.pose = BlendStopMeshBinary(out.pose, child.pose, parents, weights);
		for (const auto& e : child.curves)
		{
			auto found = std::find_if(out.curves.begin(), out.curves.end(), [&](const auto& old) { return old.name == e.name; });
			if (found == out.curves.end())
				out.curves.push_back(e);
			else
				*found = e;
		}
	}
	if (state != 0)
		out.curves = BlendNamedCurve(std::move(out.curves), state == 3 || state == 5 ? "FootLock_L" : "FootLock_R", 1);
	return out;
}
PoseCurveBundle EvaluateNormalPose(const LocomotionMachine& normal, const LocomotionMachine& stop, const StopPlantDispatch& plant,
	const PoseReadCache& cache, const std::function<PoseCurveBundle()>& idle, const std::function<PoseCurveBundle(unsigned)>& rotate,
	const std::function<PoseCurveBundle()>& detail, const StopWalkEvaluator& walk, const std::function<PoseCurveBundle()>& reference,
	const std::vector<int>& parents, const std::array<std::vector<float>, 2>& stopWeights)
{
	const auto cachedDetail = [&] { return cache(kDetailCacheNode, detail); };
	const auto profile = [](const auto&, unsigned, std::size_t count) { return TransitionPoseProfile{ std::vector<int>(count, -1), {} }; };
	const auto stopRoot = [&](unsigned state)
	{
		const auto snapshot = state >= 5 ? plant.EvaluationSnapshot(state) : StopPlantSnapshot{};
		return EvaluateStopState(state, snapshot.layerRelevant, parents, state >= 5 ? stopWeights[state - 5] : std::vector<float>{},
			cachedDetail, [&] { return EvaluateStopPlant(state, snapshot, walk, reference); });
	};
	return EvaluateStatePoseStack(
		5, normal.CurrentState(), normal.Records(),
		[&](unsigned state)
		{
			if (state == 0)
				return idle();
			if (state == 1)
				return cachedDetail();
			if (state == 3 || state == 4)
				return rotate(state);
			return EvaluateStatePoseStack(7, stop.CurrentState(), stop.Records(), stopRoot, profile);
		},
		profile);
}
}

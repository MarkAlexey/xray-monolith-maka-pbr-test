#pragma once
#include "stop_plant.h"
#include "directional_pose_evaluate.h"
namespace BodycamReference
{
NamedCurves BlendNamedCurve(NamedCurves, const std::string&, float target);
NamedCurves ScaleNamedCurve(NamedCurves, const std::string&, float scale);
class NormalIdlePoseOwner
{
public:
	void Initialize(const std::function<void()>& source) { source(); }
	void Update(const std::function<void()>& source, const std::function<double()>& scale);
	PoseCurveBundle Evaluate(const std::function<PoseCurveBundle(const std::function<PoseCurveBundle()>&)>& slot,
		const std::function<PoseCurveBundle()>& idle) const;
	float RotationScale() const { return rotationScale_; }

private:
	float rotationScale_ = 0;
};
PackedPose BlendStopMeshBinary(
	const PackedPose& base, const PackedPose& child, const std::vector<int>& parents, const std::vector<float>& weights);
Quaternion StopMeshRoot(Quaternion);
Quaternion StopMeshCompose(Quaternion local, Quaternion parent);
Quaternion StopMeshToLocal(Quaternion component, Quaternion parent);
using StopWalkEvaluator = std::function<PoseCurveBundle(StopAsset, float, unsigned)>;
PoseCurveBundle EvaluateStopPlant(
	unsigned state, const StopPlantSnapshot&, const StopWalkEvaluator&, const std::function<PoseCurveBundle()>& reference);
PoseCurveBundle EvaluateStopState(unsigned state, bool plantRelevant, const std::vector<int>& parents, const std::vector<float>& weights,
	const std::function<PoseCurveBundle()>& detail, const std::function<PoseCurveBundle()>& plant);
PoseCurveBundle EvaluateNormalPose(const LocomotionMachine& normal, const LocomotionMachine& stop, const StopPlantDispatch&,
	const PoseReadCache&, const std::function<PoseCurveBundle()>& idle, const std::function<PoseCurveBundle(unsigned)>& rotate,
	const std::function<PoseCurveBundle()>& detail, const StopWalkEvaluator&, const std::function<PoseCurveBundle()>& reference,
	const std::vector<int>& parents, const std::array<std::vector<float>, 2>& stopWeights);
}

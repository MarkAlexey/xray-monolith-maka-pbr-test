#pragma once
#include "directional_pose_evaluate.h"
namespace BodycamReference
{
PackedTransform ComposePackedComponent(const PackedTransform& local, const PackedTransform& parent);
PackedTransform RelativePackedTransform(const PackedTransform& child, const PackedTransform& parent);
PackedTransform BlendSkeletalControl(const PackedTransform& original, const PackedTransform& target, float alpha);
enum class FootRootEventKind
{
	Child,
	FreshFlags,
	Compose,
	ScaleTarget,
	CaptureRelative,
	InstallTarget,
	FractionalRelative,
	BlendLocal,
	ReturnRelative,
	Normalize,
	ReturnLocal
};
struct FootRootEvent
{
	FootRootEventKind kind;
	unsigned bone = 0;
};
class FreshFootRoot
{
public:
	FreshFootRoot(std::vector<int> parents, unsigned target, bool admitted);
	PoseCurveBundle Evaluate(
		float alpha, const std::function<PoseCurveBundle()>& child, const std::function<void(FootRootEvent)>& sink = {}) const;

private:
	std::vector<int> parents_;
	unsigned target_;
	bool admitted_;
};
}

#include "ads_pose.h"
#include "constants.h"
#include <algorithm>

namespace BodycamReference
{
namespace
{
constexpr double kAimPointBlendSeconds = .35;
// How far the canted sight pulls the gun onto the camera anchor; the magnified sight pulls it fully.
constexpr double kCantedAnchorWeight = .3;
}
AimPointBlend::AimPointBlend(const Matrix& magnified, const Matrix& canted, bool useMagnified)
	: magnified_(magnified), canted_(canted), current_(useMagnified ? magnified : canted), magnifiedMode_(useMagnified),
	  magnifiedWeight_(useMagnified ? 1. : 0.), fromMagnifiedWeight_(magnifiedWeight_)
{
}
const Matrix& AimPointBlend::Update(double dt, bool useMagnified)
{
	if (!initialized_)
	{
		current_ = magnifiedMode_ ? magnified_ : canted_;
		initialized_ = true;
	}
	if (useMagnified != magnifiedMode_ || equipmentChanged_)
	{
		equipmentChanged_ = false;
		fromMagnifiedWeight_ = magnifiedWeight_;
		magnifiedMode_ = useMagnified;
		playback_ = 0;
		from_ = current_;
		to_ = useMagnified ? magnified_ : canted_;
	}
	if (playback_ < kAimPointBlendSeconds)
	{
		playback_ = std::min(kAimPointBlendSeconds, playback_ + dt);
		const double u = playback_ / kAimPointBlendSeconds, alpha = u * u * (3 - 2 * u);
		current_ = BlendTransform(from_, to_, alpha);
		magnifiedWeight_ = fromMagnifiedWeight_ + ((magnifiedMode_ ? 1. : 0.) - fromMagnifiedWeight_) * alpha;
	}
	return current_;
}
Matrix BlendAdsTarget(const Matrix& current, const Matrix& target, const Matrix& parent, double alpha)
{
	const float weight = static_cast<float>(std::clamp(alpha, 0., 1.));
	if (weight <= kBlendEpsilon)
		return current;
	if (weight >= kBlendFull)
		return target;
	const auto inverse = InverseAffine(parent);
	const auto blended = BlendSkeletalControl(Decompose(Multiply(inverse, current)), Decompose(Multiply(inverse, target)), weight);
	return Multiply(parent, Compose(blended));
}
Matrix SolveAdsPose(const AdsPoseInput& input)
{
	if (static_cast<float>(input.alpha) <= kBlendEpsilon)
		return input.currentGun;
	const auto current = Decompose(input.currentGun), reference = Decompose(input.referenceAppliedGun);
	const Vector referencePoint = Translation(input.referenceCameraPoint);
	// CameraPoint nodes replace component rotation with identity and scale one.
	LocalPose anchor;
	anchor.position = Translation(input.currentCameraPoint);
	const auto predicted = QuaternionProduct(current.rotation, Conjugate(reference.rotation));
	const double additive = static_cast<float>(input.magnified ? 1. : kCantedAnchorWeight);
	for (unsigned i = 0; i < 3; ++i)
	{
		const double position = referencePoint[i] - reference.position[i] + current.position[i];
		anchor.position[i] += (position - anchor.position[i]) * additive;
	}
	anchor.rotation = NormalizeQuaternion(SlerpQuaternion(anchor.rotation, predicted, additive));
	const auto target = Multiply(Compose(anchor), InverseAffine(input.aimPointLocal));
	return BlendAdsTarget(input.currentGun, target, input.headParent, input.alpha);
}
Matrix BlendTransform(const Matrix& from, const Matrix& to, double alpha)
{
	auto a = Decompose(from);
	const auto b = Decompose(to);
	for (unsigned i = 0; i < 3; ++i)
	{
		a.position[i] += (b.position[i] - a.position[i]) * alpha;
		a.scale[i] += (b.scale[i] - a.scale[i]) * alpha;
	}
	a.rotation = NormalizeQuaternion(SlerpQuaternion(a.rotation, b.rotation, alpha));
	return Compose(a);
}
Matrix ApplyComponentAdditive(const Matrix& current, const Matrix& offset)
{
	auto pose = Decompose(current);
	const auto delta = Decompose(offset);
	for (unsigned i = 0; i < 3; ++i)
	{
		pose.position[i] += delta.position[i];
		pose.scale[i] *= delta.scale[i];
	}
	pose.rotation = NormalizeQuaternion(QuaternionProduct(delta.rotation, pose.rotation));
	return Compose(pose);
}
}

#include "viewmodel_helpers.h"
#include "scalar_math.h"
#include <cmath>

namespace BodycamReference
{
namespace
{
constexpr double kPoleCopyAlpha = .3;
// Lower sway amplitude in centimetres per unit of the look curve, and its follow speed.
constexpr double kAimLowerSwayCm = 25, kHipLowerSwayCm = 8, kLowerSwaySpeed = 5;
}
Matrix CopySwayPole(const Matrix& current, const Matrix& source, const Matrix& handParent)
{
	auto desired = Decompose(source);
	desired.scale = Decompose(current).scale;
	return BlendAdsTarget(current, Compose(desired), handParent, kPoleCopyAlpha);
}
Matrix HandLocalAfterGunCopy(const Matrix& gun, const Matrix& hand, const Matrix& source)
{
	auto copied = Decompose(source);
	copied.scale = Decompose(hand).scale;
	return Multiply(InverseAffine(gun), Compose(copied));
}
Matrix ApplyRecoilPose(const Matrix& gun, const RecoilPose& snapshot)
{
	auto pose = Decompose(gun);
	pose.rotation = NormalizeQuaternion(QuaternionProduct(ProceduralYXZQuaternion(snapshot.rotation), pose.rotation));
	pose.position[0] += snapshot.location[1] * kMetresPerCentimetre;
	pose.position[1] += snapshot.location[2] * kMetresPerCentimetre;
	pose.position[2] += snapshot.location[0] * kMetresPerCentimetre;
	return Compose(pose);
}
Matrix ApplyLowerSway(Matrix gun, double centimeters)
{
	gun[13] += centimeters * kMetresPerCentimetre;
	return gun;
}
const Vector& LeftElbowOffset::Update(bool aiming, double seconds, bool valid, GripPoseKind grip, const Vector& hip, const Vector& aim)
{
	if (!valid)
		return value_;
	const auto target = grip == GripPoseKind::Other ? Vector{} : (aiming ? aim : hip);
	value_ = VectorInterpTo(value_, target, seconds);
	return value_;
}
LowerSway::LowerSway()
	: curve_({ { 0, 3.3369275e-8f, -.018659236f, -.018659236f }, { 2.255956f, -.067754544f, -.095058486f, -.095058486f },
		  { 3.0114605f, -.34971362f, -.57246536f, -.57246536f }, { 4.5643744f, -1.0031608f, -.018659236f, -.018659236f } })
{
}
double LowerSway::Update(double look, bool aiming, double seconds, bool valid)
{
	if (!valid)
		return value_;
	const float sample = curve_.Sample(static_cast<float>(std::abs(look)));
	value_ = FInterpTo(value_, (aiming ? kAimLowerSwayCm : kHipLowerSwayCm) * double(sample), static_cast<float>(seconds), kLowerSwaySpeed);
	return value_;
}
}

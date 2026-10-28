#pragma once

namespace BodycamReference
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kPiF = 3.14159265358979323846f;
constexpr double kDegreesPerRadian = 57.29577951308232;
constexpr double kRadiansPerDegree = .017453292519943295;
// The source's single-precision degree factor; widen it where the source multiplies in double.
constexpr float kRadiansPerDegreeF = .01745329238474369f;
static_assert(180 / kPi == kDegreesPerRadian && kPi / 180 == kRadiansPerDegree);

constexpr double kCentimetresPerMetre = 100;
constexpr double kMetresPerCentimetre = .01;

// A blend weight at or below kBlendEpsilon counts as zero, at or above kBlendFull as one.
constexpr float kBlendEpsilon = 1e-5f;
constexpr float kBlendFull = .99999f;

// The source skeleton: the physical bones come first, the virtual bones follow them.
constexpr unsigned kPhysicalBoneCount = 68;
constexpr unsigned kVirtualBoneCount = 21;
constexpr unsigned kSkeletonBoneCount = kPhysicalBoneCount + kVirtualBoneCount;
// The exporter's full source skeleton. Physical bones keep their index in it; the virtual bones sit
// kSourceBoneCount - kSkeletonBoneCount entries later.
constexpr unsigned kSourceBoneCount = 179;
// The translation retarget modes the packages carry for the source skeleton.
constexpr unsigned kTranslationModeCount = 158;
}

#include "recoil_asset.h"
#include "asset_reader.h"
namespace BodycamReference
{
namespace
{
constexpr unsigned kMaxCurveKeys = 4096;
RecoilCurve ReadCurve(ReferenceAssetReader& r)
{
	std::vector<RecoilCurveKey> keys(r.Count(kMaxCurveKeys));
	for (auto& k : keys)
	{
		k.time = r.F32();
		k.value = r.F32();
		k.arrive = r.F32();
		k.leave = r.F32();
	}
	return RecoilCurve(std::move(keys));
}
RecoilCurves ReadCurves(ReferenceAssetReader& r)
{
	return { ReadCurve(r), ReadCurve(r), ReadCurve(r), ReadCurve(r), ReadCurve(r), ReadCurve(r) };
}
}
RecoilSetup LoadRecoilSetup(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCRRECL1", 1);
	RecoilSetup s{ ReadCurves(r), ReadCurves(r), ReadCurve(r) };
	for (auto* p : { &s.profile.hip, &s.profile.aim })
	{
		for (auto& v : p->pitch)
			v = r.F64();
		for (auto& v : p->yaw)
			v = r.F64();
		for (auto& v : p->roll)
			v = r.F64();
		for (auto& v : p->kick)
			v = r.F64();
		for (auto& v : p->right)
			v = r.F64();
		for (auto& v : p->up)
			v = r.F64();
	}
	s.profile.maxKick = r.F64();
	s.profile.maxUp = r.F64();
	s.profile.smoothRoll = r.Bool();
	for (auto& axis : s.controller.step)
		for (auto& v : axis)
			v = r.F64();
	for (auto& v : s.controller.smoothing)
		v = r.F64();
	for (auto& v : s.controller.scale)
		v = r.F32();
	s.controller.damping = r.F64();
	s.controller.compensation = r.F64();
	for (auto& v : s.translation.acceleration)
		v = r.F64();
	for (auto& v : s.translation.damping)
		v = r.F64();
	for (auto& v : s.translation.noiseAmplitude)
		v = r.F64();
	s.translation.pushback = r.F64();
	s.translation.progressAmount = r.F64();
	s.translation.noiseAimScalar = r.F32();
	s.translation.progressAimAlpha = r.F32();
	for (auto& axis : s.sway.range)
		for (auto& v : axis)
			v = r.F64();
	s.sway.acceleration = r.F64();
	s.sway.damping = r.F64();
	s.sway.rollMultiplier = r.F64();
	s.sway.adsScale = r.F32();
	for (auto& v : s.fireSway.step)
		v = r.F32();
	for (auto& v : s.fireSway.limit)
		v = r.F32();
	s.fireSway.damping = r.F64();
	s.fireSway.interpolation = r.F64();
	for (auto& v : s.swayPivot)
		v = r.F64();
	for (auto* value : { &s.attributes.vertical, &s.attributes.horizontal, &s.attributes.kick, &s.attributes.up,
			 &s.attributes.magnifiedKick, &s.attributes.magnifiedUp, &s.attributes.magnifiedMaxKick, &s.attributes.magnifiedMaxUp })
		*value = r.F32();
	if (!r.Done())
		throw std::invalid_argument("Trailing recoil package data");
	return s;
}
}

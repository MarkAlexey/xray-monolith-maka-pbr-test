// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_profile.h"
namespace BodycamReference
{
RecoilRandomProfile BuildRecoilProfile(const RecoilSourceProfile& source, bool aiming, bool capture, const RecoilAttributes& a)
{
	const bool magnified = aiming && capture;
	const float kick = magnified ? static_cast<float>(double(a.magnifiedKick) * a.kick) : a.kick;
	const float up = magnified ? static_cast<float>(double(a.magnifiedUp) * a.up) : a.up;
	const auto& pose = aiming ? source.aim : source.hip;
	RecoilRandomProfile p;
	p.pitch = pose.pitch;
	p.yaw = pose.yaw;
	p.roll = pose.roll;
	p.smoothRoll = source.smoothRoll;
	if (aiming)
	{
		for (auto& v : p.pitch)
			v *= a.vertical;
		for (auto& v : p.yaw)
			v *= a.horizontal;
	}
	for (unsigned i = 0; i < 2; ++i)
	{
		p.location.x[i] = pose.kick[i] * kick;
		p.location.y[i] = pose.right[i];
		p.location.z[i] = pose.up[i] * up;
	}
	const float maxKick = static_cast<float>(source.maxKick), maxUp = static_cast<float>(source.maxUp);
	p.location.maxX = magnified ? static_cast<float>(double(maxKick) * a.magnifiedMaxKick) : maxKick;
	p.location.maxZ = magnified ? static_cast<float>(double(maxUp) * a.magnifiedMaxUp) : maxUp;
	return p;
}
}

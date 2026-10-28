// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "footroot_pose.h"
#include "constants.h"
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
PackedTransform ComposePackedComponent(const PackedTransform& local, const PackedTransform& parent)
{
	const double a = local[0], b = local[1], c = local[2], d = local[3], x = parent[0], y = parent[1], z = parent[2], w = parent[3];
	PackedTransform out = { ((x * d + w * a) + y * c) - z * b, ((w * b - x * c) + y * d) + z * a, ((x * b + w * c) - y * a) + z * d,
		((w * d - x * a) - y * b) - z * c };
	const double vx = local[4] * parent[8], vy = local[5] * parent[9], vz = local[6] * parent[10];
	const double cx = y * vz - z * vy, cy = z * vx - x * vz, cz = x * vy - y * vx;
	const double tx = cx + cx, ty = cy + cy, tz = cz + cz;
	out[4] = ((tx * w + vx) + (y * tz - z * ty)) + parent[4];
	out[5] = ((ty * w + vy) + (z * tx - x * tz)) + parent[5];
	out[6] = ((tz * w + vz) + (x * ty - y * tx)) + parent[6];
	for (unsigned i = 8; i < 11; ++i)
		out[i] = local[i] * parent[i];
	return NormalizePackedTransform(out);
}
PackedTransform RelativePackedTransform(const PackedTransform& a, const PackedTransform& b)
{
	const double ax = a[0], ay = a[1], az = a[2], aw = a[3], x = -b[0], y = -b[1], z = -b[2], w = b[3];
	const double dx = a[4] - b[4], dy = a[5] - b[5], dz = a[6] - b[6];
	const double tx = (y * dz - dy * z) * 2, ty = (z * dx - x * dz) * 2, tz = (x * dy - dx * y) * 2;
	const double cx = y * tz - ty * z, cy = z * tx - x * tz, cz = x * ty - tx * y;
	std::array<double, 3> reciprocal;
	for (unsigned i = 0; i < 3; ++i)
		reciprocal[i] = std::abs(b[i + 8]) <= double(1e-8f) ? 0 : 1 / b[i + 8];
	return { ((x * aw + ax * w) + y * az) - z * ay, ((ay * w - x * az) + y * aw) + z * ax, ((x * ay + az * w) - y * ax) + z * aw,
		((aw * w - x * ax) - y * ay) - z * az, ((tx * w + dx) + cx) * reciprocal[0], ((ty * w + dy) + cy) * reciprocal[1],
		((tz * w + dz) + cz) * reciprocal[2], 0, reciprocal[0] * a[8], reciprocal[1] * a[9], reciprocal[2] * a[10], 0 * a[11] };
}
PackedTransform BlendSkeletalControl(const PackedTransform& x, const PackedTransform& y, float alpha)
{
	if (alpha <= kBlendEpsilon)
		return x;
	if (alpha >= kBlendFull)
		return y;
	const float wa = 1.f - alpha;
	if (wa >= kBlendFull)
		return x;
	const double wb = 1 - double(wa);
	const double dot = (x[1] * y[1] + x[3] * y[3]) + (x[0] * y[0] + x[2] * y[2]);
	PackedTransform out;
	for (unsigned i = 0; i < 4; ++i)
		out[i] = (dot >= 0 ? y[i] : 0 - y[i]) * wb + x[i] * wa;
	for (unsigned i = 4; i < 12; ++i)
		out[i] = wb * y[i] + wa * x[i];
	return NormalizePackedTransform(out);
}
FreshFootRoot::FreshFootRoot(std::vector<int> parents, unsigned target, bool admitted)
	: parents_(std::move(parents)), target_(target), admitted_(admitted)
{
	if (parents_.empty() || parents_[0] != -1)
		throw std::invalid_argument("Foot-root hierarchy must start with its only root");
	for (unsigned i = 1; i < parents_.size(); ++i)
		if (parents_[i] < 0 || parents_[i] >= int(i))
			throw std::invalid_argument("Foot-root hierarchy parent does not precede its child");
	if (admitted_ && (target_ == 0 || target_ >= parents_.size()))
		throw std::invalid_argument("ik_foot_root is missing or is the root");
}
PoseCurveBundle FreshFootRoot::Evaluate(
	float alpha, const std::function<PoseCurveBundle()>& child, const std::function<void(FootRootEvent)>& sink) const
{
	auto out = child();
	const auto emit = [&](FootRootEventKind kind, unsigned bone = 0)
	{
		if (sink)
			sink({ kind, bone });
	};
	emit(FootRootEventKind::Child);
	std::vector<bool> flags(out.pose.size(), false);
	flags[0] = true;
	emit(FootRootEventKind::FreshFlags);
	const std::function<void(unsigned)> component = [&](unsigned i)
	{
		if (!flags[i])
		{
			component(unsigned(parents_[i]));
			out.pose[i] = ComposePackedComponent(out.pose[i], out.pose[parents_[i]]);
			flags[i] = true;
			emit(FootRootEventKind::Compose, i);
		}
	};
	if (alpha > kBlendEpsilon && admitted_)
	{
		const unsigned i = target_, parent = unsigned(parents_[i]);
		component(i);
		auto target = out.pose[i];
		target[8] *= 1.4;
		target[9] *= 1.4;
		target[11] = 0;
		emit(FootRootEventKind::ScaleTarget, i);
		PackedTransform original;
		if (alpha < kBlendFull)
		{
			original = RelativePackedTransform(out.pose[i], out.pose[parent]);
			original[7] = original[11] = 0;
			emit(FootRootEventKind::CaptureRelative, i);
		}
		out.pose[i] = target;
		flags[i] = true;
		emit(FootRootEventKind::InstallTarget, i);
		if (alpha < kBlendFull)
		{
			out.pose[i] = RelativePackedTransform(out.pose[i], out.pose[parent]);
			flags[i] = false;
			emit(FootRootEventKind::FractionalRelative, i);
			out.pose[i] = BlendSkeletalControl(original, out.pose[i], alpha);
			emit(FootRootEventKind::BlendLocal, i);
		}
	}
	for (unsigned i = unsigned(out.pose.size()) - 1; i > 0; --i)
		if (flags[i])
		{
			out.pose[i] = RelativePackedTransform(out.pose[i], out.pose[parents_[i]]);
			emit(FootRootEventKind::ReturnRelative, i);
			out.pose[i] = NormalizePackedTransform(out.pose[i]);
			flags[i] = false;
			emit(FootRootEventKind::Normalize, i);
		}
	emit(FootRootEventKind::ReturnLocal);
	return out;
}
}

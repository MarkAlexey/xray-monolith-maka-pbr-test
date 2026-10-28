#pragma once
#include "transform.h"
#include "upper_pose.h"
#include <optional>
namespace BodycamReference
{
struct RifleTrack
{
	std::string name;
	int parent = -1;
};
// Converts between the exported basis in metres and the source lanes in centimetres, swapping Y and Z.
PackedPose GltfToPackedRiflePose(const std::vector<LocalPose>&);
std::vector<LocalPose> PackedToGltfRiflePose(const PackedPose&);
std::vector<Matrix> RiflePoseComponents(const std::vector<LocalPose>&, const std::vector<RifleTrack>&);
class RifleOverlayGun
{
public:
	RifleOverlayGun(std::vector<RifleTrack>, const std::vector<LocalPose>& cclamp);
	std::vector<LocalPose> AttachGunToHand(const std::vector<LocalPose>&) const;
	void CaptureHelperOffsets(std::vector<LocalPose>&);
	Matrix GunFromHand(const Matrix& handComponent, const Matrix& target, const Matrix* carriedRhs = nullptr) const;
	std::vector<LocalPose> ApplyMontageMask(const std::vector<LocalPose>& base, const std::vector<LocalPose>& child) const;
	const Matrix& GunFromRight() const { return gunFromRight_; }
	const std::optional<Matrix>& MontageGunFromRight() const { return montageGunFromRight_; }

private:
	std::vector<RifleTrack> tracks_;
	std::vector<int> parents_;
	std::vector<float> mask_;
	unsigned hand_, gun_;
	int rhs_, lhs_;
	Matrix gunFromRight_;
	std::optional<Matrix> montageGunFromRight_;
};
}

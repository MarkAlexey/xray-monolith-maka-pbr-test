#pragma once
#include "bodycam_reference_skeleton.h"
#include "bodycam_reference/skeleton_bridge.h"
#include "bodycam_reference/engine_profile.h"
#include <array>
#include <optional>
#include <string>
namespace BodycamReference
{
// The two HUD arm models of the bound weapon. The engine keeps playing its
// hand animation; Publish moves that pose by the motion the Reference graph
// solved, and solves the arms to the moved weapon.
class EngineArmsAdapter
{
public:
	// Throws when a calibration does not fit its model.
	EngineArmsAdapter(IKinematics& right, IKinematics& left, const EngineBasis&, const SkeletonPose& referenceCalibration,
		const ArmCalibration& rightCalibration, const ArmCalibration& leftCalibration);
	EngineArmsAdapter(const EngineArmsAdapter&) = delete;
	EngineArmsAdapter& operator=(const EngineArmsAdapter&) = delete;
	// anchor is the bone the weapon hangs on. aimMotion is the aimed idle; with
	// none, the hip calibration serves both ends of the aim blend.
	void CalibrateWeapon(const char* aimMotion, const char* anchor);
	// The calibration pose at an aim blend, merged into the Reference skeleton.
	std::vector<LocalPose> CalibrationPose(const SkeletonPose& referenceBase, double aimBlend);
	// before/after are the Reference skeleton around the neutral solve of one
	// endpoint: 0 hip, 1 magnified ADS, 2 canted ADS.
	void SetNeutralFit(unsigned endpoint, const SkeletonPose& before, const SkeletonPose& after, const Matrix& neutralGun,
		const Matrix& itemOffset, double aimBlend);
	// The engine's pose of this frame, merged into the Reference skeleton. The
	// result stays valid until the next Capture.
	const std::vector<LocalPose>& Capture(const SkeletonPose& referenceBase);
	// Writes both models from the pose captured this frame. leftHandFromRight
	// is the engine's hand relation; null takes it from the captured pose.
	void Publish(const SkeletonPose& referenceSolved, double aimBlend, const Matrix& proceduralGun, const Matrix& itemOffset,
		const Matrix* leftHandFromRight, double motionInfluence, const Vector& elbowCm, double magnifiedWeight);
	// Metres each shoulder moved in the last Publish to keep its arm within reach: right, left.
	const std::array<double, 2>& ReachAccommodation() const { return reachAccommodation_; }
	const Matrix& ReferenceFromRightModel() const { return rightModel_; }
	const Matrix& ReferenceFromLeftModel() const { return leftModel_; }

private:
	struct NeutralFit
	{
		Matrix right = kIdentityMatrix, left = kIdentityMatrix;
	};
	void MergePose(const SkeletonPose& referenceBase, const SkeletonPose& right, const SkeletonPose& left, std::vector<LocalPose>& locals);
	Matrix BodyMotion(const SkeletonPose& before, const SkeletonPose& after, bool right) const;

	EngineSkeletonAdapter right_, left_;
	Matrix rightModel_, leftModel_;
	SkeletonPose rightCalibration_, leftCalibration_;
	std::optional<SkeletonPose> rightAimCalibration_, leftAimCalibration_;
	SkeletonBridge rightBridge_, leftBridge_;
	ArmChain rightChain_, leftChain_;
	unsigned anchor_ = 0;
	std::array<NeutralFit, 3> neutralFit_;
	std::array<double, 2> reachAccommodation_{};
	// Working storage of every frame, built once with each skeleton's topology.
	SkeletonPose capturedRight_, capturedLeft_, capturedReference_;
	SkeletonPose importedRight_, importedLeft_;
	SkeletonPose baselineRight_, baselineLeft_;
	SkeletonPose contactRight_, contactLeft_;
	std::vector<LocalPose> capturedLocals_;
	std::vector<Matrix> mergeComponents_, publishedRight_, publishedLeft_;
	std::vector<EngineRowMatrix> rightTransforms_, leftTransforms_;
	std::vector<int> referenceParents_;
	std::vector<unsigned char> referenceOwner_; // 0 neither model, 1 right, 2 left
	std::vector<unsigned> referenceOrder_; // parents before children
};
}

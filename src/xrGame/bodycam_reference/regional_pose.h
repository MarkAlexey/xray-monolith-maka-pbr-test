#pragma once
#include "upper_pose.h"
#include <map>
namespace BodycamReference
{
struct RegionalValue
{
	double constant = 0;
	std::string property;
};
struct RegionalDefinition
{
	std::string name, alphaCurve;
	bool mesh = false;
	RegionalValue additiveAlpha;
};
struct RegionalMaskEntry
{
	unsigned skeletonIndex = 0, sourceIndex = 0;
	double weight = 0;
};
struct RegionalAssembly
{
	unsigned node = 0;
	std::string input;
	bool meshRotation = false, meshScale = false;
	std::vector<RegionalValue> weights;
	std::vector<RegionalMaskEntry> mask;
};
struct RegionalManifest
{
	std::string standingAsset, assemblyBase;
	std::map<std::string, std::string> propertyInputs;
	std::vector<RegionalDefinition> regions;
	std::vector<RegionalAssembly> assembly;
};
struct RegionalOutput
{
	PackedPose pose;
	std::map<std::string, double> properties;
};
using RegionalStage = std::function<void(const std::string&, const PackedPose&)>;
class RegionalPose
{
public:
	RegionalPose(RegionalManifest, std::vector<int> parents, std::vector<unsigned> compactToSkeleton);
	RegionalOutput Evaluate(const PackedPose& baseLayer, const PackedPose& basePose, const PackedPose& overlay,
		const NamedCurves& previousPublishedCurves, const RegionalStage& stage = {}) const;

private:
	RegionalManifest manifest_;
	std::vector<int> parents_;
	std::vector<std::vector<RegionalMaskEntry>> masks_;
};
}

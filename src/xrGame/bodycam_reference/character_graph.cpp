#include "character_graph.h"
#include <utility>

namespace BodycamReference
{
CharacterGraph::CharacterGraph(const RecoilSetup& setup, const std::vector<double>& durations, const MovementCurveSampler& sample,
	const PoseAssetBank& poses, const LocomotionAssetBank& locomotion, const RifleAssetBank& rifle, const Matrix& magnified,
	const Matrix& canted, CharacterRandomSource source, Rotation control, double fov, WalkingStressAdapter stress)
	: randomSource_(std::move(source)), recoil(
											setup, [this] { return randomSource_.draw15(); }, [this] { return randomSource_.unit(); }),
	  random([this] { return randomSource_.draw15(); }), regrip(durations, random), motion(recoil, regrip, control, fov),
	  movement(sample, YawMovementBasis), primary(locomotion, poses, rifle, std::move(stress)), aim(magnified, canted, true),
	  vibration([this] { return randomSource_.draw15(); }), mouse([this] { return randomSource_.draw15(); }), effects(vibration, mouse),
	  character(motion, movement, primary, aim, effects)
{
}
}

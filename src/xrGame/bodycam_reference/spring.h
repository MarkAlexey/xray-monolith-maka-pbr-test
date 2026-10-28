#pragma once
#include "transform.h"
#include <array>

namespace BodycamReference
{
// State is retained when a skeletal-control gate is irrelevant.
struct SpringState
{
	float damped = 0, springValue = 0, output = 0;
	float velocity = 0, previousTarget = 0;
	bool initialized = false;
};

enum class SwayAxis
{
	X,
	Yaw,
	Z,
	Roll,
	Pitch
};
struct SwayNode
{
	SwayAxis axis;
	float value, alpha;
	int targetSpace;
	bool relevant;
};
// Values of SwayNode::targetSpace.
constexpr int kComponentSpace = 1, kBoneSpace = 3;

struct SpringInput
{
	// Look X, yaw, Z, roll and pitch, then movement X, yaw, pitch, Z and roll.
	std::array<double, 10> values{};
	std::array<double, 3> magnifiedScale{};
	double dt = 0, adsAlpha = 0;
	bool aiming = false, magnified = false, hasPublishedAlpha = false;
	double actualLook = 1, actualMove = 1;
};

float ExponentialDamp(float current, float target, double dt, double speed);
float FloatSpring(float current, float target, SpringState& state, double stiffness, double damping, double dt);

class SwayRig
{
public:
	static constexpr std::array<int, 10> kGraphOrder{ { 0, 1, 4, 2, 3, 5, 6, 7, 8, 9 } };
	std::array<SwayNode, 10> Update(const SpringInput& input);
	const std::array<SpringState, 10>& States() const { return states_; }
	void Reset() { states_ = {}; }

private:
	std::array<SpringState, 10> states_{};
};
// Applies the rig's outputs to current in graph order, rotating about pivotCm in the head's space.
Matrix ApplySpringStack(const Matrix& current, const Matrix& headParent, const std::array<SwayNode, 10>& nodes, const Vector& pivotCm);
}

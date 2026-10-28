#pragma once
#include <array>

namespace BodycamReference
{
struct TranslationRecoilConfig
{
	std::array<double, 4> acceleration{}, damping{};
	std::array<double, 2> noiseAmplitude{};
	double pushback = 0, progressAmount = 0;
	float noiseAimScalar = 1, progressAimAlpha = 1;
};
struct TranslationRecoilState
{
	// Noise X, noise Y, automatic pushback, vertical progression.
	std::array<double, 4> target{}, value{}, acceleration{};
};
class TranslationRecoil
{
public:
	void Configure(const TranslationRecoilConfig&);
	void Fire(const std::array<double, 2>& units, bool aiming);
	void SetAutoPushback();
	void Update(double worldDelta, bool aiming);
	const TranslationRecoilState& State() const { return state_; }

private:
	TranslationRecoilConfig config_{};
	TranslationRecoilState state_{};
};

struct SwayRecoilConfig
{
	std::array<std::array<double, 2>, 2> range{};
	double acceleration = 0, damping = 0, rollMultiplier = 0;
	float adsScale = 1;
};
struct SwayRecoilState
{
	std::array<double, 2> target{};
	std::array<double, 3> value{}; // pitch, yaw, roll
};
class SwayRecoil
{
public:
	void Configure(const SwayRecoilConfig& config) { config_ = config; }
	void Fire(const std::array<double, 2>& units, bool aiming);
	void Update(double worldDelta);
	const SwayRecoilState& State() const { return state_; }

private:
	SwayRecoilConfig config_{};
	SwayRecoilState state_{};
};
}

#pragma once
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <locale>
#include <string>
namespace BodycamReference
{
// Longest weapon section name accepted as a settings key, and the largest offset on any axis.
constexpr std::size_t kMaxWeaponSectionLength = 127;
constexpr float kMaxWeaponOffset = 2.f;
inline bool WeaponPositionKey(const char* section)
{
	if (!section || std::strncmp(section, "wpn_", 4))
		return false;
	const auto length = std::strlen(section);
	if (length <= 4 || length > kMaxWeaponSectionLength)
		return false;
	for (const unsigned char c : std::string(section))
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
			return false;
	return true;
}
inline bool ValidWeaponPosition(const std::array<float, 3>& value)
{
	for (const auto v : value)
		if (!std::isfinite(v) || std::abs(v) > kMaxWeaponOffset)
			return false;
	return true;
}
inline bool ParseWeaponPosition(const char* text, std::array<float, 3>& output)
{
	if (!text)
		return false;
	std::string value(text);
	for (auto& c : value)
		if (c == ',')
			c = ' ';
	std::istringstream input(value);
	input.imbue(std::locale::classic());
	std::array<float, 3> candidate;
	for (auto& c : candidate)
		if (!(input >> c))
			return false;
	input >> std::ws;
	if (!input.eof() || !ValidWeaponPosition(candidate))
		return false;
	output = candidate;
	return true;
}
}

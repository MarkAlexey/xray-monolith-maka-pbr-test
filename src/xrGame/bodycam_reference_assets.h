#pragma once
#include "bodycam_reference/character_session.h"
#include "bodycam_reference/engine_profile.h"
#include <memory>
#include <string>

namespace BodycamReference
{
// The configuration section of the character every actor uses.
inline constexpr char kCharacterSection[] = "bodycam_reference_character";

// These read through the game's file system and configuration and throw when
// the asset or the line is missing.
std::vector<std::uint8_t> ReadEngineReferencePacket(const std::string& path);
std::string ReadEngineReferenceSetting(const std::string& section, const std::string& key);
std::unique_ptr<CharacterSession> LoadEngineCharacterSession(
	const EngineCharacterAssetPaths&, CharacterRandomSource, Rotation initialControl, double horizontalFov);
}

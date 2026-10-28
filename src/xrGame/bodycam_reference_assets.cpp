#include "../xrEngine/stdafx.h"
#include "bodycam_reference_assets.h"
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
namespace
{
struct CloseReader
{
	void operator()(IReader* reader) const { FS.r_close(reader); }
};
}
std::vector<std::uint8_t> ReadEngineReferencePacket(const std::string& path)
{
	std::unique_ptr<IReader, CloseReader> reader(FS.r_open("$game_data$", path.c_str()));
	if (!reader)
		throw std::runtime_error("Missing reference asset: " + path);
	if (!reader->length())
		throw std::runtime_error("Empty reference asset: " + path);
	const auto* begin = static_cast<const std::uint8_t*>(reader->pointer());
	return { begin, begin + reader->length() };
}
std::string ReadEngineReferenceSetting(const std::string& section, const std::string& key)
{
	if (!pSettings->section_exist(section.c_str()) || !pSettings->line_exist(section.c_str(), key.c_str()))
		throw std::invalid_argument("Missing reference profile field: " + section + "." + key);
	// An empty value reads as a null string.
	const char* value = pSettings->r_string(section.c_str(), key.c_str());
	return value ? value : "";
}
std::unique_ptr<CharacterSession> LoadEngineCharacterSession(
	const EngineCharacterAssetPaths& paths, CharacterRandomSource random, Rotation control, double fov)
{
	const CharacterAssetPackets packets{ ReadEngineReferencePacket(paths.upper), ReadEngineReferencePacket(paths.locomotion),
		ReadEngineReferencePacket(paths.rifle), ReadEngineReferencePacket(paths.recoil), ReadEngineReferencePacket(paths.movement),
		ReadEngineReferencePacket(paths.equipment), ReadEngineReferencePacket(paths.idle), ReadEngineReferencePacket(paths.regrip) };
	return std::make_unique<CharacterSession>(packets, std::move(random), control, fov);
}
}

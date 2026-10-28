#include "bodycam_reference_assets.h"
#include "pch_script.h"
#include "bodycam_reference_optics.h"
#include "bodycam_reference_host.h"
#include "bodycam_settings.h"
#include "Weapon.h"
#include "player_hud.h"
#include "../xrEngine/svp_reference_optics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <locale>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
constexpr char kEffectsSection[] = "bodycam_reference_effects";

template <unsigned N>
std::array<double, N> Numbers(const std::string& section, const char* key)
{
	auto text = ReadEngineReferenceSetting(section, key);
	std::replace(text.begin(), text.end(), ',', ' ');
	std::istringstream input(text);
	input.imbue(std::locale::classic());
	std::array<double, N> values;
	for (auto& value : values)
		if (!(input >> value) || !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
			throw std::invalid_argument("Invalid reference optics values: " + section + "." + key);
	input >> std::ws;
	if (!input.eof())
		throw std::invalid_argument("Extra reference optics values");
	return values;
}
// The part of the published frame that the effects profile fixes.
SvpReferenceOpticsFrame LoadProfile(const std::string& section)
{
	SvpReferenceOpticsFrame packet;
	const auto text = [&](const char* key, char* target, std::size_t size, const char* error)
	{
		const auto value = ReadEngineReferenceSetting(section, key);
		if (value.empty() || value.size() >= size)
			throw std::invalid_argument(error);
		std::memcpy(target, value.c_str(), value.size() + 1);
	};
	const auto vector = [&](const char* key, Fvector& target)
	{
		const auto v = Numbers<3>(section, key);
		target.set(float(v[0]), float(v[1]), float(v[2]));
	};
	const auto constants = [&](const char* key, void* target, std::size_t size, const char* error)
	{
		const auto bytes = ReadEngineReferencePacket(ReadEngineReferenceSetting(section, key));
		if (bytes.size() != size)
			throw std::invalid_argument(error);
		std::memcpy(target, bytes.data(), size);
	};
	text("source_package", packet.sourcePackage, sizeof(packet.sourcePackage), "Invalid source optics identity");
	text("source_variant", packet.sourceVariant, sizeof(packet.sourceVariant), "Invalid source optics identity");
	packet.sourceRadius = float(Numbers<1>(section, "source_radius_m")[0]);
	vector("source_baseline_eye_m", packet.sourceBaselineEye);
	vector("fallback_eye_m", packet.ocularBaselineEye);
	// The eye sits behind the ocular, which looks along +Z.
	if (packet.sourceBaselineEye.z >= -EPS || packet.ocularBaselineEye.z >= -EPS)
		throw std::invalid_argument("Invalid authored optical eye anchors");
	vector("source_socket_m", packet.sourceSocket);
	vector("source_tangent", packet.sourceTangent);
	vector("source_bitangent", packet.sourceBitangent);
	if (packet.sourceRadius <= EPS || std::abs(packet.sourceTangent.magnitude() - 1.f) > .01f ||
		std::abs(packet.sourceBitangent.magnitude() - 1.f) > .01f ||
		std::abs(packet.sourceTangent.dotproduct(packet.sourceBitangent)) > .01f)
		throw std::invalid_argument("Unqualified source optical geometry");
	constants("glass_constants", packet.glassConstants, sizeof(packet.glassConstants), "Reference glass requires 19 float4 constants");
	for (const auto& value : packet.glassConstants)
		if (!_valid(value))
			throw std::invalid_argument("Nonfinite glass constant");
	text("glass_spots", packet.glassMaterialNames[0], sizeof(packet.glassMaterialNames[0]), "Invalid source optics identity");
	text("glass_surface", packet.glassMaterialNames[1], sizeof(packet.glassMaterialNames[1]), "Invalid source optics identity");
	constants("constants", packet.constants, sizeof(packet.constants), "Reference optics requires 62 float4 constants");
	for (const auto& value : packet.constants)
		if (!_valid(value))
			throw std::invalid_argument("Nonfinite optics constant");
	const char* materials[] = { "translucent", "opaque" };
	for (unsigned i = 0; i < 2; ++i)
		text(materials[i], packet.materialNames[i], sizeof(packet.materialNames[i]), "Invalid optics texture name");
	const auto filter = Numbers<3>(section, "filter");
	packet.threshold = float(filter[0]);
	packet.range = float(filter[1]);
	packet.contrast = float(filter[2]);
	packet.toneMapInputGamma = float(Numbers<1>(section, "tonemap_input_gamma")[0]);
	if (packet.range <= 0 || (packet.toneMapInputGamma != 1 && packet.toneMapInputGamma != 2.2f))
		throw std::invalid_argument("Invalid optics filter range or tonemap input gamma");
	packet.enabled = true;
	packet.allowDeferredCapture = pSettings->r_bool(section.c_str(), "allow_deferred_capture");
	return packet;
}
// Completes the frame for the weapon. False when there is nothing to publish this frame.
bool Describe(CWeapon& weapon, SvpReferenceOpticsFrame& packet)
{
	auto& viewport = Device.m_SecondViewport;
	const auto* weaponHud = weapon.HudItemData();
	if (g_player_hud->attached_item(0) != weaponHud)
		return false;
	// The scope scripts configure the image only while aiming. At the hip the
	// glass still takes the generation of the current, invalidated configuration.
	CSecondVPParams::OpticConfig config;
	const bool configured = viewport.ReadOpticConfig(config);
	const auto scope = weapon.IsScopeAttached() ? weapon.GetScopeName() : shared_str();
	const char* scopeName = scope.c_str() ? scope.c_str() : "";
	const bool contextMatches = config.valid && config.typed_route && config.weapon_id == weapon.ID() &&
		config.zoom_type == weapon.GetZoomType() && config.session == viewport.GetSVPSession() &&
		config.route_epoch == viewport.GetOpticRouteEpoch() && !xr_strcmp(config.weapon, weapon.cNameSect().c_str()) &&
		!xr_strcmp(config.scope, scopeName);
	if (viewport.IsSVPActive() && (!configured || !contextMatches))
		return false;
	// The neutral aimed eye, carried by the weapon: the Reference one when a
	// weapon is bound, else the engine's own aimed camera.
	Fvector eye;
	if (Bodycam::ReferenceActive())
	{
		const auto* host = g_player_hud->ReferenceHost();
		if (!host || !host->HasWeaponBinding())
			return false;
		const auto eyeInWeapon = host->NeutralEyeInWeapon();
		eye.set(eyeInWeapon[12], eyeInWeapon[13], eyeInWeapon[14]);
	}
	else
	{
		Fmatrix cameraFromWeapon, weaponFromCamera;
		if (!g_player_hud->EngineAdsCameraFromWeapon(0, cameraFromWeapon) || !weaponFromCamera.invert_b(cameraFromWeapon))
			return false;
		eye = weaponFromCamera.c;
	}
	weaponHud->m_item_transform.transform_tiny(packet.weaponEyeWorld, eye);
	if (!_valid(packet.weaponEyeWorld))
		return false;
	packet.ocularBaselineFromWeapon = true;
	const ScopeEyeModel eyeModel = SelectScopeEyeModel(Bodycam::ClassicActive(), Bodycam::GetConfig().camera.FollowLag());
	packet.classicPupilTracking = eyeModel.tracked;
	packet.classicFollowEye = eyeModel.current;
	packet.frame = Device.dwFrame;
	packet.session = viewport.GetSVPSession();
	packet.opticEpoch = viewport.svp_optic_epoch;
	packet.routeEpoch = viewport.GetOpticRouteEpoch();
	packet.generation = config.generation;
	packet.active = weapon.GetZoomType() == 0;
	return true;
}
}
void PublishOptics(CWeapon& weapon, std::optional<std::uint64_t>& failedRequest)
{
	static std::unique_ptr<SvpReferenceOpticsFrame> profile;
	const auto request = Bodycam::GetMotionSelection().Requests();
	if (!Bodycam::GetConfig().scope_effects_enable || failedRequest == request || !g_player_hud ||
		!pSettings->section_exist(kEffectsSection))
	{
		svp_reference_optics_clear();
		return;
	}
	try
	{
		if (!profile)
			profile = std::make_unique<SvpReferenceOpticsFrame>(LoadProfile(kEffectsSection));
		auto packet = *profile;
		if (Describe(weapon, packet))
		{
			svp_reference_optics_publish(packet);
			return;
		}
	}
	catch (const std::exception& error)
	{
		profile.reset();
		failedRequest = request;
		Msg("! Reference optics disabled: %s", error.what());
	}
	svp_reference_optics_clear();
}
}

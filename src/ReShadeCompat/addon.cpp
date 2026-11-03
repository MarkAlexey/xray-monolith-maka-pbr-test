#include "stdafx.h"
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#pragma warning(disable: 4047)
HINSTANCE hInstance = (HINSTANCE)&__ImageBase;
#pragma warning(default: 4047)

//Credits for idea SSE ReShade Helper https://www.nexusmods.com/skyrimspecialedition/mods/78961

static reshade::api::effect_runtime* m_runtime = nullptr;
static reshade::api::command_list* m_cmdlist = nullptr;
static reshade::api::resource_view m_rtv;
static reshade::api::resource_view m_rtv_srgb;

// DX12 exe: ReShade runs on the presenting D3D12 device, which never sees the game's depth
// buffer, so the engine hands over a D3D12 copy (D3D12PresentBridge::ExportedDepth).
// Rebinding makes ReShade update its descriptors, so it happens only when the texture
// changes: render size, or depth appearing/disappearing (menus).
static void* m_depth = nullptr; // ID3D12Resource for the frame being presented
static unsigned long long m_depthGeneration = 0;
static uint64_t m_boundDepth = 0;
static unsigned long long m_boundGeneration = 0;
static reshade::api::resource_view m_depthView = {0};

void reshade_set_scene_depth(void* resource, unsigned long long generation)
{
	m_depth = resource;
	m_depthGeneration = generation;
}

static void release_scene_depth(reshade::api::effect_runtime* runtime)
{
	if (m_depthView.handle)
	{
		runtime->update_texture_bindings("DEPTH", {0}, {0});
		runtime->get_device()->destroy_resource_view(m_depthView);
	}
	m_depthView.handle = 0;
	m_boundDepth = 0;
	m_boundGeneration = 0;
}

static void bind_scene_depth(reshade::api::effect_runtime* runtime)
{
	reshade::api::device* device = runtime->get_device();
	if (device->get_api() != reshade::api::device_api::d3d12)
		return;
	const uint64_t depth = reinterpret_cast<uintptr_t>(m_depth);
	if (depth == m_boundDepth && m_depthGeneration == m_boundGeneration)
		return;
	reshade::api::resource_view view = {0};
	if (depth && !device->create_resource_view({depth}, reshade::api::resource_usage::shader_resource,
		reshade::api::resource_view_desc(reshade::api::format::r32_float), &view))
		view.handle = 0;
	runtime->update_texture_bindings("DEPTH", view, view);
	if (m_depthView.handle)
		device->destroy_resource_view(m_depthView);
	m_depthView = view;
	m_boundDepth = depth;
	m_boundGeneration = m_depthGeneration;
}

static void on_reshade_begin_effects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb)
{
	m_runtime = runtime;
	m_cmdlist = cmd_list;
	m_rtv = rtv;
	m_rtv_srgb = rtv_srgb;
	bind_scene_depth(runtime);
}

static void on_reshade_reloaded_effects(reshade::api::effect_runtime* runtime)
{
	if (m_depthView.handle)
		runtime->update_texture_bindings("DEPTH", m_depthView, m_depthView);
}

static void on_destroy_effect_runtime(reshade::api::effect_runtime* runtime)
{
	if (runtime != m_runtime)
		return;
	release_scene_depth(runtime);
	m_runtime = nullptr;
	m_cmdlist = nullptr;
}

void render_reshade_effects()
{
	// A DX12 runtime records into a presentation command list owned by ReShade.
	// Reusing the previous callback's list from our DX11 loading-screen draw is
	// invalid. Let ReShade execute its effects normally during DX12 Present.
	if (m_runtime && m_runtime->get_device()->get_api() == reshade::api::device_api::d3d12)
		return;
	if (m_runtime)
		m_runtime->render_effects(m_cmdlist, m_rtv, m_rtv_srgb);
}

bool init_reshade()
{
	if (!reshade::register_addon(hInstance))
		return false;

	reshade::register_event<reshade::addon_event::reshade_begin_effects>(on_reshade_begin_effects);
	reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(on_reshade_reloaded_effects);
	reshade::register_event<reshade::addon_event::destroy_effect_runtime>(on_destroy_effect_runtime);

	return true;
}

void unregister_reshade()
{
	if (m_runtime)
		release_scene_depth(m_runtime);
	reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(on_destroy_effect_runtime);
	reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(on_reshade_reloaded_effects);
	reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(on_reshade_begin_effects);
	reshade::unregister_addon(hInstance);
	m_runtime = nullptr;
	m_cmdlist = nullptr;
	m_rtv.handle = 0;
	m_rtv_srgb.handle = 0;
}

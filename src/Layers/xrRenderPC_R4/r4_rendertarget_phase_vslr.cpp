#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/IGame_Level.h"

// Monolith: IX-Ray VSLR ("offscreen reflections", r4_vslr_enable 1).
//
// IX-Ray 1.6 (ixray-1.6-stcop: CRender::render_reflections() in r4_rendertarget_phase_sslr.cpp,
// forward_base.ps, combine_vslr.ps, CompureSpecularIrradance(); LVutner and the IX-Ray team)
// renders the scene around the camera into a small cubemap every frame with a cheap forward
// shader, converts it to an octahedral panorama, and lets the image-based reflections read it,
// so metals and glossy surfaces reflect the actual surroundings instead of only the sky.
//
// Port, adapted to Monolith:
//   - Capture: the opaque geometry (priority 0) of each cube face is traversed by its own graph
//     manager (CRender::GMReflect: no HOM, no particles, no lights) and drawn with the
//     shader element E[SE_R2_REFLECTIONS] (= 5), which uber_reflect() builds for the main
//     geometry blenders: level geometry (flat, aref, lightmapped, terrain), trees and models.
//     Shaders: r3\deffer_vslr_*.ps -> vslr_capture.h (linear colour + distance, two targets).
//   - The cube faces are aligned with the world axes (IX-Ray aligns them with the camera), so
//     the panorama is world-space and does not change with the view direction.
//   - Lighting in the capture: sun (no shadow map: the baked sun visibility of static geometry,
//     none for dynamic objects), sky (hemisphere colour x baked hemi), the weather's ambient,
//     and the static lightmap colour of level geometry. Dynamic lights are not in it.
//   - r4_vslr_faces cube faces are re-rendered per frame (6 = all), round robin, so a full
//     update takes 6 / r4_vslr_faces frames. The panorama is rebuilt every frame a face changed.
//   - Panorama: compute shader vslr_panorama.cs, RGBA16F (rgb = gamma colour, a = sky weight:
//     1 where nothing was captured or fog covers it), mips by GenerateMips. Read by
//     pbr_ixray.h as s_env_fwd with USE_OFFSCREEN_REFLECTIONS.
//
// Limits / caveats:
//   - Off with MSAA (o.vslr), not captured for a scope's second viewport (the last capture,
//     centred on the same camera, is reused).
//   - Distant objects are captured only up to r4_vslr_far x the weather's far plane, and objects
//     that the main view would draw as LOD billboards are left out (like IX-Ray).
//   - The capture is centred on the camera: reflections are exact for surfaces near the camera
//     and increasingly off further away (a single probe, no parallax correction).
//   - Only real PBR textures read it (the IX-Ray ambient in pbr_ixray.h); legacy textures keep
//     their cubemap reflections.
//   - Cost: r4_vslr_faces x (traversal + draw of the opaque scene at r4_vslr_size^2).

extern float g_fSCREEN;
extern float r_ssaDISCARD;
extern float r_ssaDONTSORT;
extern float r_ssaLOD_A;
extern float r_ssaLOD_B;
extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

// Distance written where nothing was drawn ("sky"), metres. The panorama pass turns the
// captured distance into a fog/sky weight, so anything at or beyond this reads as sky.
static const float VSLR_SKY_DISTANCE = 10000.f;

void CRenderTarget::create_vslr(u32 size)
{
	destroy_vslr();

	// Two cubemaps: colour and distance
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = size;
	td.Height = size;
	td.MipLevels = 1;
	td.ArraySize = 6;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	const DXGI_FORMAT cube_formats[2] = { DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16_FLOAT };
	ID3DTexture2D** cube_surf[2] = { &t_vslr_color_surf, &t_vslr_dist_surf };
	ID3DRenderTargetView** cube_rtv[2] = { rt_vslr_color_rtv, rt_vslr_dist_rtv };
	for (u32 c = 0; c < 2; ++c)
	{
		td.Format = cube_formats[c];
		if (FAILED(HW.pDevice->CreateTexture2D(&td, nullptr, cube_surf[c])))
		{
			Msg("! IX-Ray VSLR: cannot create the capture cubemaps, VSLR off");
			destroy_vslr();
			return;
		}
		D3D11_RENDER_TARGET_VIEW_DESC rv = {};
		rv.Format = cube_formats[c];
		rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		rv.Texture2DArray.MipSlice = 0;
		rv.Texture2DArray.ArraySize = 1;
		for (u32 f = 0; f < 6; ++f)
		{
			rv.Texture2DArray.FirstArraySlice = f;
			if (FAILED(HW.pDevice->CreateRenderTargetView(*cube_surf[c], &rv, &cube_rtv[c][f])))
			{
				Msg("! IX-Ray VSLR: cannot create a cube face view, VSLR off");
				destroy_vslr();
				return;
			}
		}
	}

	// One depth buffer, shared by the faces (cleared per face)
	D3D11_TEXTURE2D_DESC dd = {};
	dd.Width = size;
	dd.Height = size;
	dd.MipLevels = 1;
	dd.ArraySize = 1;
	dd.Format = DXGI_FORMAT_D16_UNORM;
	dd.SampleDesc.Count = 1;
	dd.Usage = D3D11_USAGE_DEFAULT;
	dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(HW.pDevice->CreateTexture2D(&dd, nullptr, &t_vslr_depth_surf))
		|| FAILED(HW.pDevice->CreateDepthStencilView(t_vslr_depth_surf, nullptr, &rt_vslr_depth_dsv)))
	{
		Msg("! IX-Ray VSLR: cannot create the capture depth buffer, VSLR off");
		destroy_vslr();
		return;
	}

	// Panorama: twice the face size, full mip chain (rough reflections read the small mips)
	const u32 env = size * 2;
	u32 mips = 1;
	while ((env >> mips) > 0)
		++mips;
	D3D11_TEXTURE2D_DESC ed = {};
	ed.Width = env;
	ed.Height = env;
	ed.MipLevels = mips;
	ed.ArraySize = 1;
	ed.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	ed.SampleDesc.Count = 1;
	ed.Usage = D3D11_USAGE_DEFAULT;
	// GenerateMips needs render-target and shader-resource binding plus the misc flag
	ed.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	ed.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
	uv.Format = ed.Format;
	uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uv.Texture2D.MipSlice = 0;
	if (FAILED(HW.pDevice->CreateTexture2D(&ed, nullptr, &t_vslr_env_surf))
		|| FAILED(HW.pDevice->CreateUnorderedAccessView(t_vslr_env_surf, &uv, &rt_vslr_env_uav)))
	{
		Msg("! IX-Ray VSLR: cannot create the panorama, VSLR off");
		destroy_vslr();
		return;
	}

	// LEWD: distance panorama for the localized lookup (r4_vslr_localize). Same layout as the
	// colour panorama, mip 0 only (the lookup is for sharp reflections; rough ones use the plain
	// direction). Without it VSLR still works, unlocalized.
	{
		D3D11_TEXTURE2D_DESC dd2 = ed;
		dd2.MipLevels = 1;
		dd2.Format = DXGI_FORMAT_R16_FLOAT;
		dd2.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		dd2.MiscFlags = 0;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uv2 = uv;
		uv2.Format = dd2.Format;
		if (FAILED(HW.pDevice->CreateTexture2D(&dd2, nullptr, &t_vslr_envdist_surf))
			|| FAILED(HW.pDevice->CreateUnorderedAccessView(t_vslr_envdist_surf, &uv2, &rt_vslr_envdist_uav)))
		{
			Msg("! LEWD VSLR: cannot create the distance panorama, reflections stay unlocalized");
			_RELEASE(rt_vslr_envdist_uav);
			_RELEASE(t_vslr_envdist_surf);
		}
	}

	// CTexture::surface_set creates the views: TextureCube for the cubes, Texture2D (all mips)
	// for the panorama (dx10SH_Texture.cpp)
	t_vslr_color.create(r2_RT_vslr_color);
	t_vslr_color->surface_set(t_vslr_color_surf);
	t_vslr_dist.create(r2_RT_vslr_dist);
	t_vslr_dist->surface_set(t_vslr_dist_surf);
	t_vslr_env.create(r2_RT_vslr_env);
	t_vslr_env->surface_set(t_vslr_env_surf);
	t_vslr_envdist.create(r2_RT_vslr_envdist);
	t_vslr_envdist->surface_set(t_vslr_envdist_surf);
	if (rt_vslr_envdist_uav)
	{
		const FLOAT far_d[4] = { VSLR_SKY_DISTANCE, VSLR_SKY_DISTANCE, VSLR_SKY_DISTANCE, VSLR_SKY_DISTANCE };
		HW.pContext->ClearUnorderedAccessViewFloat(rt_vslr_envdist_uav, far_d);
	}

	// Until every face has been captured the panorama reads as pure sky (a = 1); the consumer
	// also gets intensity 0 until then (binder_vslr_params, r4.cpp)
	const FLOAT sky[4] = { 0.f, 0.f, 0.f, 1.f };
	HW.pContext->ClearUnorderedAccessViewFloat(rt_vslr_env_uav, sky);
	if (t_vslr_env->get_SRView())
		HW.pContext->GenerateMips(t_vslr_env->get_SRView());

	vslr_size = size;
	vslr_env_mips = mips;
	vslr_valid = false;
	vslr_captured_faces = 0;
	Msg("- IX-Ray VSLR: %ux%u cube faces, %ux%u panorama (%u mips)", size, size, env, env, mips);
}

void CRenderTarget::destroy_vslr()
{
	if (t_vslr_color)
		t_vslr_color->surface_set(nullptr);
	if (t_vslr_dist)
		t_vslr_dist->surface_set(nullptr);
	if (t_vslr_env)
		t_vslr_env->surface_set(nullptr);
	if (t_vslr_envdist)
		t_vslr_envdist->surface_set(nullptr);
	t_vslr_color.destroy();
	t_vslr_dist.destroy();
	t_vslr_env.destroy();
	t_vslr_envdist.destroy();
	_RELEASE(rt_vslr_envdist_uav);
	_RELEASE(t_vslr_envdist_surf);
	for (u32 f = 0; f < 6; ++f)
	{
		_RELEASE(rt_vslr_color_rtv[f]);
		_RELEASE(rt_vslr_dist_rtv[f]);
	}
	_RELEASE(rt_vslr_depth_dsv);
	_RELEASE(rt_vslr_env_uav);
	_RELEASE(t_vslr_color_surf);
	_RELEASE(t_vslr_dist_surf);
	_RELEASE(t_vslr_depth_surf);
	_RELEASE(t_vslr_env_surf);
	vslr_size = 0;
	vslr_env_mips = 1;
	vslr_valid = false;
}

// Cubes -> octahedral panorama (mip 0), then the mip chain
void CRenderTarget::phase_vslr_panorama()
{
	if (!s_vslr || !rt_vslr_env_uav)
		return;

	PIX_EVENT(phase_vslr_panorama);

	// The cube faces were render targets a moment ago; unbind before reading them
	ID3D11RenderTargetView* nullrtv[8] = {};
	HW.pContext->OMSetRenderTargets(8, nullrtv, nullptr);

	SPass& P = *(s_vslr->E[0]->passes[0]);
	RCache.set_States(P.state);
	RCache.set_Constants(P.constants);
	RCache.set_Textures(P.T);
	RCache.set_CS(P.cs);

	const u32 env = vslr_size * 2;
	RCache.set_c("vslr_panorama_params", float(env), 1.f / float(env), vslr_far, 0.f);

	// u1 = LEWD distance panorama (null when it could not be created: the shader's writes are dropped)
	ID3D11UnorderedAccessView* uav[2] = { rt_vslr_env_uav, rt_vslr_envdist_uav };
	ID3D11UnorderedAccessView* nulluav[2] = { nullptr, nullptr };
	UINT counts[2] = { 0, 0 };
	HW.pContext->CSSetUnorderedAccessViews(0, 2, uav, counts);
	RCache.Compute((env + 7u) / 8u, (env + 7u) / 8u, 1);
	HW.pContext->CSSetUnorderedAccessViews(0, 2, nulluav, counts);
	RCache.unbind_CS_textures();
	RCache.set_CS(nullptr);

	if (t_vslr_env->get_SRView())
		HW.pContext->GenerateMips(t_vslr_env->get_SRView());
}

// World-aligned cube faces, D3D order (+X, -X, +Y, -Y, +Z, -Z): view direction and up vector.
// X-Ray's camera is left-handed like D3D's cubemap convention, so a face rendered with
// build_camera_dir(position, dir, up) lands in the cube texel layout as is.
static const Fvector vslr_face_dir[6] = {
	{ 1.f, 0.f, 0.f }, { -1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, -1.f, 0.f }, { 0.f, 0.f, 1.f }, { 0.f, 0.f, -1.f } };
static const Fvector vslr_face_up[6] = {
	{ 0.f, 1.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, -1.f }, { 0.f, 0.f, 1.f }, { 0.f, 1.f, 0.f }, { 0.f, 1.f, 0.f } };

void CRender::render_reflections()
{
	if (!o.vslr || !Target->rt_vslr_depth_dsv || !pLastSector || Device.m_SecondViewport.IsSVPFrame())
		return;

	PIX_EVENT(render_reflections);
	PROF_EVENT("render_reflections");

	const u32 size = Target->vslr_size;
	const u32 faces = u32(_max(1, _min(6, ps_r4_vslr_faces)));

	// Capture distance: a fraction of the weather's far plane (fog hides the rest)
	float far_dist = 100.f;
	if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
		far_dist = _max(10.f, g_pGamePersistent->Environment().CurrentEnv->far_plane * ps_r4_vslr_far);
	Target->vslr_far = far_dist;

	// Screen-space-area thresholds for a size x size, 90 degree view (IX-Ray does the same).
	// Nothing else traverses the scene at this point of the frame, so the globals are safe to
	// swap; they are recomputed for the main view below and by CRender::Calculate() next frame.
	const float saved_g_fSCREEN = g_fSCREEN;
	const float saved_DISCARD = r_ssaDISCARD, saved_DONTSORT = r_ssaDONTSORT;
	const float saved_LOD_A = r_ssaLOD_A, saved_LOD_B = r_ssaLOD_B;
	const float saved_GLOD_start = r_ssaGLOD_start, saved_GLOD_end = r_ssaGLOD_end;
	const float saved_HZBvsTEX = r_ssaHZBvsTEX;

	g_fSCREEN = float(size * size) * (EPS_S + ps_r__LOD); // fov 90: fov_factor = 1
	r_ssaDISCARD = _sqr(ps_r__ssaDISCARD) / g_fSCREEN;
	r_ssaDONTSORT = _sqr(ps_r__ssaDONTSORT / 3) / g_fSCREEN;
	r_ssaLOD_A = _sqr(ps_r2_ssaLOD_A / 3) / g_fSCREEN;
	r_ssaLOD_B = _sqr(ps_r2_ssaLOD_B / 3) / g_fSCREEN;
	r_ssaGLOD_start = _sqr(ps_r__GLOD_ssa_start / 3) / g_fSCREEN;
	r_ssaGLOD_end = _sqr(ps_r__GLOD_ssa_end / 3) / g_fSCREEN;
	r_ssaHZBvsTEX = _sqr(ps_r__ssaHZBvsTEX / 3) / g_fSCREEN;

	Fmatrix project;
	project.build_projection(PI_DIV_2, 1.f, VIEWPORT_NEAR, far_dist);

	Fvector position = Device.vCameraPosition;
	CObject* viewer = g_pGameLevel ? g_pGameLevel->CurrentViewEntity() : nullptr;

	const u32 saved_phase = phase;
	phase = PHASE_REFLECT;

	const FLOAT black[4] = { 0.f, 0.f, 0.f, 0.f };
	const FLOAT sky[4] = { VSLR_SKY_DISTANCE, VSLR_SKY_DISTANCE, VSLR_SKY_DISTANCE, VSLR_SKY_DISTANCE };

	for (u32 n = 0; n < faces; ++n)
	{
		const u32 f = (vslr_next_face + n) % 6;

		Fmatrix view, full;
		view.build_camera_dir(position, vslr_face_dir[f], vslr_face_up[f]);
		full.mul(project, view);

		CFrustum frustum;
		frustum.CreateFromMatrix(full, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

		GMReflect.clear();
		GMReflect.traverse(pLastSector, frustum, position, full);
		GMReflect.r_dsgraph_capture(false, true, viewer);

		HW.pContext->ClearRenderTargetView(Target->rt_vslr_color_rtv[f], black);
		HW.pContext->ClearRenderTargetView(Target->rt_vslr_dist_rtv[f], sky);
		HW.pContext->ClearDepthStencilView(Target->rt_vslr_depth_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

		// All bound targets must share one size: drop whatever is still bound beyond slot 2
		// (the G-buffer's motion-vector / Heatvision slots are full screen)
		for (u32 slot = 3; slot < 8; ++slot)
			RCache.set_RT(nullptr, slot);
		Target->u_setrt(size, size, Target->rt_vslr_color_rtv[f], Target->rt_vslr_dist_rtv[f], nullptr, Target->rt_vslr_depth_dsv);
		rmNormal();
		RCache.set_xform_world(Fidentity);
		RCache.set_xform_view(view);
		RCache.set_xform_project(project);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();
		RCache.set_CullMode(CULL_CCW);

		GMReflect.r_dsgraph_render_graph(0);
		GMReflect.clear();
	}
	vslr_next_face = (vslr_next_face + faces) % 6;
	Target->vslr_center = position;	// LEWD: capture point of the localized lookup (vslr_center)

	phase = saved_phase;

	g_fSCREEN = saved_g_fSCREEN;
	r_ssaDISCARD = saved_DISCARD;
	r_ssaDONTSORT = saved_DONTSORT;
	r_ssaLOD_A = saved_LOD_A;
	r_ssaLOD_B = saved_LOD_B;
	r_ssaGLOD_start = saved_GLOD_start;
	r_ssaGLOD_end = saved_GLOD_end;
	r_ssaHZBvsTEX = saved_HZBvsTEX;

	RCache.set_xform_world(Fidentity);
	RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);

	// Every face has content once a full round has passed since the targets were created
	if (!Target->vslr_valid)
	{
		Target->vslr_captured_faces += faces;
		if (Target->vslr_captured_faces >= 6)
			Target->vslr_valid = true;
	}

	Target->phase_vslr_panorama();

	// Back to the main view's size (phase_scene_prepare() binds its own targets next)
	Target->u_setrt(Device.dwWidth, Device.dwHeight, nullptr, nullptr, nullptr, nullptr);
	rmNormal();
}

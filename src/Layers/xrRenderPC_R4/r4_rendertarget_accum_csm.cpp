#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

// Monolith: IX-Ray style cascaded sun shadows (r4_sun_csm 1).
//
// IX-Ray (ixray-1.6-stcop, R_sun.cpp / r4_rendertarget_accum_direct.cpp / accum_sun.ps.hlsl,
// LVutner and the IX-Ray team) renders every sun cascade into one Texture2DArray and lights
// the screen in a single fullscreen pass that picks the cascade per pixel. Monolith renders
// a cascade, lights its light-space box, renders the next cascade into the SAME depth map,
// and so on (three stencil-masked cuboid passes).
//
// This port is a hybrid, on purpose:
//   - Every cascade except the last is rendered into the array (rt_smap_csm, slice = cascade)
//     and lit by ONE fullscreen pass (accum_sun_csm.ps), with the cascade chosen per pixel and
//     a cross-fade between neighbouring cascades (Monolith has hard seams there).
//   - The last (far) cascade is rendered and lit exactly as before, into rt_smap_depth, by the
//     installed accum_sun_far shader (vanilla / ES / SSS). That keeps volumetric sun shafts,
//     which read the far cascade from rt_smap_depth, and SSS/ES's far-shadow tweaks working.
//   - Stencil hand-off: the single pass zeroes the light-marker stencil of every pixel it
//     lights (same mask/op as the legacy near/middle passes); pixels outside the array
//     cascades are discarded, so the legacy far pass lights exactly the rest.
//
// Requirements / limits (o.sun_csm is false otherwise, and the legacy path runs):
//   - no MSAA (the legacy MSAA per-sample passes are not replicated), no -sunfilter;
//   - gamedata\shaders\r3\accum_sun_csm.ps present;
//   - at most CSM_MAX_SLICES array cascades (3 cascades in Monolith -> 2 slices).
//   - Memory: one extra shadow map per array slice (2 x 64 MB at r2_smap_size 4096).

static const u32 CSM_MAX_SLICES = 3;

void CRenderTarget::create_smap_csm(u32 size)
{
	destroy_smap_csm();

	const u32 slices = RImplementation.m_sun_cascades.size() > 1
		? _min(u32(RImplementation.m_sun_cascades.size() - 1), CSM_MAX_SLICES) : 0;
	if (!slices || !rt_smap_depth || !rt_smap_depth->pSurface || !rt_smap_depth->pZRT)
		return;

	// Same typeless format, size and bind flags as the legacy sun map, so sampling and the
	// depth comparison behave identically
	D3D11_TEXTURE2D_DESC td;
	rt_smap_depth->pSurface->GetDesc(&td);
	td.Width = size;
	td.Height = size;
	td.ArraySize = slices;
	td.MipLevels = 1;
	td.SampleDesc.Count = 1;
	td.SampleDesc.Quality = 0;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	CHK_DX(HW.pDevice->CreateTexture2D(&td, nullptr, &t_smap_csm_surf));

	D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
	rt_smap_depth->pZRT->GetDesc(&dsv);
	dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	dsv.Flags = 0;
	dsv.Texture2DArray.MipSlice = 0;
	dsv.Texture2DArray.ArraySize = 1;
	for (u32 i = 0; i < slices; ++i)
	{
		dsv.Texture2DArray.FirstArraySlice = i;
		CHK_DX(HW.pDevice->CreateDepthStencilView(t_smap_csm_surf, &dsv, &rt_smap_csm_dsv[i]));
	}

	// CTexture::surface_set creates a Texture2DArray SRV for array resources (dx10SH_Texture.cpp)
	t_smap_csm.create(r2_RT_smap_csm);
	t_smap_csm->surface_set(t_smap_csm_surf);
	smap_csm_slices = slices;
	smap_csm_size = size;
}

void CRenderTarget::destroy_smap_csm()
{
	if (t_smap_csm)
		t_smap_csm->surface_set(nullptr);
	t_smap_csm.destroy();
	for (auto& dsv : rt_smap_csm_dsv)
		_RELEASE(dsv);
	_RELEASE(t_smap_csm_surf);
	smap_csm_slices = 0;
}

void CRenderTarget::clear_smap_csm()
{
	for (u32 i = 0; i < smap_csm_slices; ++i)
		HW.pContext->ClearDepthStencilView(rt_smap_csm_dsv[i], D3D_CLEAR_DEPTH, 1.0f, 0L);
}

// Bind one array slice as the shadow-map depth target (legacy phase_smap_direct, minus the
// clear: all slices are cleared once per frame in clear_smap_csm, so a cascade with nothing
// to draw does not keep last frame's depth)
void CRenderTarget::phase_smap_csm(light* L, u32 slice)
{
	VERIFY(slice < smap_csm_slices);
	u_setrt(rt_smap_surf, NULL, NULL, rt_smap_csm_dsv[slice]);
	RImplementation.rmNormal();
	RCache.set_Stencil(FALSE);
}

// Cloud-shadow (sunmask) projection, shared by the legacy cascades and the CSM pass so cloud
// shadows line up across cascade boundaries.
// Only the wind scroll is per frame: the legacy code advanced it once per cascade (three times
// a frame), so it advances once per frame here, scaled by the cascade count to keep the old
// speed. The matrix itself is rebuilt on every call because it depends on the current view
// (Device.mInvView), which differs for a second viewport rendered in the same frame.
Fmatrix CRenderTarget::sun_clouds_xform(light* sun)
{
	static float w_shift = 0;
	static u32 shift_frame = u32(-1);

	const float w_dir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
	float w_speed = g_pGamePersistent->Environment().CurrentEnv->wind_velocity * 0.001f;
	clamp(w_speed, 0.1f, 1.0f);

	if (shift_frame != Device.dwFrame)
	{
		shift_frame = Device.dwFrame;
		w_shift -= 0.005f * w_speed * Device.fTimeDelta * float(_max(u32(1), u32(RImplementation.m_sun_cascades.size())));
	}

	Fmatrix m_clouds_shadow;
	Fmatrix m_xform;
	Fvector normal;
	normal.setHP(-w_dir, 0);
	Fvector position;
	position.set(0, 0, 0);
	m_xform.build_camera_dir(position, sun->direction, normal);
	Fvector localnormal;
	m_xform.transform_dir(localnormal, normal);
	localnormal.normalize();
	m_clouds_shadow.mul(m_xform, Device.mInvView);
	m_xform.scale(0.002f, 0.002f, 1.f);
	m_clouds_shadow.mulA_44(m_xform);
	m_xform.translate(localnormal.mul(w_shift));
	m_clouds_shadow.mulA_44(m_xform);
	return m_clouds_shadow;
}

void CRenderTarget::accum_direct_csm()
{
	PIX_EVENT(accum_direct_csm);
	PROF_EVENT("accum_direct_csm");

	if (!smap_csm_slices)
		return;

	phase_accumulator();

	light* fuckingsun = (light*)RImplementation.Lights.sun_adapted._get();

	Fvector L_dir, L_clr;
	float L_spec;
	L_clr.set(fuckingsun->color.r, fuckingsun->color.g, fuckingsun->color.b);
	L_spec = u_diffuse2s(L_clr);
	Device.mView.transform_dir(L_dir, fuckingsun->direction);
	L_dir.normalize();

	u32 Offset;
	const float _w = float(RenderScreenWidth()); // scene resolution (DLSS / FSR3)
	const float _h = float(RenderScreenHeight());
	RCache.set_CullMode(CULL_NONE);

	// 1. Light marker: the same masker pass the legacy near cascade draws (marks pixels that
	//    can receive sun light in the stencil); the legacy far cascade relies on it too.
	{
		u32 C = color_rgba(255, 255, 255, 255);
		Fvector2 p0, p1;
		p0.set(.5f / _w, .5f / _h);
		p1.set((_w + .5f) / _w, (_h + .5f) / _h);
		float d_Z = EPS_S, d_W = 1.f;

		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y); pv++;
		pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y); pv++;
		pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y); pv++;
		pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);
		RCache.set_Geometry(g_combine);

		float intensity = 0.3f * fuckingsun->color.r + 0.48f * fuckingsun->color.g + 0.22f * fuckingsun->color.b;
		Fvector dir = L_dir;
		dir.normalize().mul(-_sqrt(intensity + EPS));
		RCache.set_Element(s_accum_mask->E[SE_MASK_DIRECT]);
		RCache.set_c("Ldynamic_dir", dir.x, dir.y, dir.z, 0);
		RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0x01, 0xff, D3DSTENCILOP_KEEP,
		                   D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	if (RImplementation.o.nvstencil)
		u_stencil_optimize();

	// 2. Per-cascade shadow matrices: view space -> shadow-map texture space, built exactly like
	//    the legacy m_shadow (same depth range scales and biases per cascade)
	Fmatrix m_shadow[CSM_MAX_SLICES];
	FPU::m64r();
	for (u32 i = 0; i < CSM_MAX_SLICES; ++i)
	{
		const u32 ci = _min(i, smap_csm_slices - 1);
		const float fRange = (ci == 0) ? ps_r2_sun_depth_near_scale : ps_r2_sun_depth_far_scale;
		Fmatrix m_TexelAdjust =
		{
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, fRange, 0.0f,
			0.5f, 0.5f, RImplementation.m_sun_cascades[ci].bias, 1.0f
		};
		Fmatrix xf_project;
		xf_project.mul(m_TexelAdjust, RImplementation.m_sun_cascades[ci].xform);
		m_shadow[i].mul(xf_project, Device.mInvView);
	}
	FPU::m24r();

	// 3. Fullscreen lighting pass (clip-space quad, same layout combine_1 uses)
	{
		float scale_X = _w / float(TEX_jitter);
		float scale_Y = _h / float(TEX_jitter);
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(-1, 1, 0, 1, 0, 0, scale_Y); pv++;
		pv->set(-1, -1, 0, 0, 0, 0, 0); pv++;
		pv->set(1, 1, 1, 1, 0, scale_X, scale_Y); pv++;
		pv->set(1, -1, 1, 0, 0, scale_X, 0); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);
		RCache.set_Geometry(g_combine);
	}

	phase_accumulator();
	RCache.set_ColorWriteEnable();
	RCache.set_Element(s_accum_csm->E[0]);
	RCache.set_c("Ldynamic_dir", L_dir.x, L_dir.y, L_dir.z, 0);
	RCache.set_c("Ldynamic_color", L_clr.x, L_clr.y, L_clr.z, L_spec);
	RCache.set_c("m_sunmask", sun_clouds_xform(fuckingsun));
	RCache.set_c("m_shadow_csm0", m_shadow[0]);
	RCache.set_c("m_shadow_csm1", m_shadow[1]);
	RCache.set_c("m_shadow_csm2", m_shadow[2]);
	// x = number of array cascades, y = cross-fade band (fraction of the cascade),
	// z = shadow-map size, w = 1 / size
	RCache.set_c("csm_params", float(smap_csm_slices), ps_r4_sun_csm_blend, float(smap_csm_size), 1.f / float(smap_csm_size));

	// Same stencil test/op as the legacy near/middle passes: only marked pixels, and zero the
	// marker where lit so the legacy far cascade does not light them again
	RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, dwLightMarkerID, 0xff, 0xFE, D3DSTENCILOP_KEEP, D3DSTENCILOP_ZERO,
	                   D3DSTENCILOP_KEEP);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

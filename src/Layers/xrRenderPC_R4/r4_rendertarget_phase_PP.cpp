#include "stdafx.h"
#ifdef XR_DX12_BRIDGE
#include "Dlss/FrameGeneration.h"
#endif
#include "r4_rendertarget.h"
#include "../../xrEngine/CustomHUD.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

void CRenderTarget::u_calc_tc_noise(Fvector2& p0, Fvector2& p1)
{
	CTexture* T = RCache.get_ActiveTexture(2);
	VERIFY2(T, "Texture #3 in noise shader should be setted up");
	u32 tw = iCeil(float(T->get_Width()) * param_noise_scale + EPS_S);
	u32 th = iCeil(float(T->get_Height()) * param_noise_scale + EPS_S);
	VERIFY2(tw && th, "Noise scale can't be zero in any way");

	// calculate shift from FPSes
	im_noise_time -= Device.fTimeDelta;
	if (im_noise_time < 0)
	{
		im_noise_shift_w = ::Random.randI(tw ? tw : 1);
		im_noise_shift_h = ::Random.randI(th ? th : 1);
		float fps_time = 1 / param_noise_fps;
		while (im_noise_time < 0) im_noise_time += fps_time;
	}

	u32 shift_w = im_noise_shift_w;
	u32 shift_h = im_noise_shift_h;
	float start_u = (float(shift_w) + .5f) / (tw);
	float start_v = (float(shift_h) + .5f) / (th);
	u32 _w = Device.dwWidth;
	u32 _h = Device.dwHeight;
	u32 cnt_w = _w / tw;
	u32 cnt_h = _h / th;
	float end_u = start_u + float(cnt_w) + 1;
	float end_v = start_v + float(cnt_h) + 1;

	p0.set(start_u, start_v);
	p1.set(end_u, end_v);
}

void CRenderTarget::u_calc_tc_duality_ss(Fvector2& r0, Fvector2& r1, Fvector2& l0, Fvector2& l1)
{
	// Calculate ordinaty TCs from blur and SS
	float tw = float(dwWidth);
	float th = float(dwHeight);
	if (dwHeight != Device.dwHeight) param_blur = 1.f;
	Fvector2 shift, p0, p1;
	shift.set(.5f / tw, .5f / th);
	shift.mul(param_blur);
	p0.set(.5f / tw, .5f / th).add(shift);
	p1.set((tw + .5f) / tw, (th + .5f) / th).add(shift);

	// Calculate Duality TC
	float shift_u = param_duality_h * .5f;
	float shift_v = param_duality_v * .5f;

	r0.set(p0.x, p0.y);
	r1.set(p1.x - shift_u, p1.y - shift_v);
	l0.set(p0.x + shift_u, p0.y + shift_v);
	l1.set(p1.x, p1.y);
}

BOOL CRenderTarget::u_need_PP()
{
	bool _blur = (param_blur > 0.001f);
	bool _gray = (param_gray > 0.001f);
	bool _noise = (param_noise > 0.001f);
	bool _dual = (param_duality_h > 0.001f) || (param_duality_v > 0.001f);

	// bool	_menu_pp= g_pGamePersistent?g_pGamePersistent->OnRenderPPUI_query():false;

	bool _cbase = false;
	{
		int _r = color_get_R(param_color_base);
		_r = _abs(_r - int(0x7f));
		int _g = color_get_G(param_color_base);
		_g = _abs(_g - int(0x7f));
		int _b = color_get_B(param_color_base);
		_b = _abs(_b - int(0x7f));
		if (_r > 2 || _g > 2 || _b > 2) _cbase = true;
	}
	bool _cadd = false;
	{
		//int		_r	= color_get_R(param_color_add)	;
		//int		_g	= color_get_G(param_color_add)	;
		//int		_b	= color_get_B(param_color_add)	;
		//if (_r>2 || _g>2 || _b>2)	_cadd	= true	;
		int _r = _abs((int)(param_color_add.x * 255));
		int _g = _abs((int)(param_color_add.y * 255));
		int _b = _abs((int)(param_color_add.z * 255));
		if (_r > 2 || _g > 2 || _b > 2) _cadd = true;
	}
	return _blur || _gray || _noise || _dual || _cbase || _cadd || u_need_CM();
}

bool CRenderTarget::u_need_CM()
{
	return (param_color_map_influence > 0.001f);
}

struct TL_2c3uv
{
	Fvector4 p;
	u32 color0;
	u32 color1;
	Fvector2 uv [3];
	IC void set(float x, float y, u32 c0, u32 c1, float u0, float v0, float u1, float v1, float u2, float v2)
	{
		p.set(x, y, EPS_S, 1.f);
		color0 = c0;
		color1 = c1;
		uv[0].set(u0, v0);
		uv[1].set(u1, v1);
		uv[2].set(u2, v2);
	}
};

ref_rt& CRenderTarget::PostprocessColor()
{
	return dlss::Configured() && !dlss::ScopePass() ? rt_dlss_post_color : (RImplementation.o.dx10_msaa ? rt_Generic : rt_Color);
}

ref_rt& CRenderTarget::PostprocessSource()
{
	return dlss::Configured() && !dlss::ScopePass() ? rt_dlss_post_source : rt_Generic_0;
}

bool CRenderTarget::ScopeAfterUpscale() const
{
	return dlss::Configured() && s_dlss_scope_copy._get() != nullptr;
}

void CRenderTarget::phase_dlss_native_depth(float jitterX, float jitterY, bool lens)
{
	// Scene depth is render-sized and jittered. Reproject it into the existing
	// native depth buffer before drawing unjittered HUD screens. Keep their
	// occlusion by hands/casing; never attach a render-sized DSV to a native RT.
	u_setrt(rt_dlss_post_source, nullptr, nullptr, HW.pBaseZB);
	const float w = float(Device.dwWidth), h = float(Device.dwHeight);
	u32 offset;
	FVF::TL* quad = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
	quad[0].set(0, h, EPS_S, 1, 0xffffffff, 0, 1);
	quad[1].set(0, 0, EPS_S, 1, 0xffffffff, 0, 0);
	quad[2].set(w, h, EPS_S, 1, 0xffffffff, 1, 1);
	quad[3].set(w, 0, EPS_S, 1, 0xffffffff, 1, 0);
	RCache.Vertex.Unlock(4, g_combine->vb_stride);
	RCache.set_Geometry(g_combine);
	RCache.set_Element(s_dlss_ui_depth->E[0]);
	RCache.set_c("dlss_ui_depth_params", float(dlss::RenderWidth()) / w,
		float(dlss::RenderHeight()) / h, jitterX, jitterY);
	RCache.set_c("dlss_ui_depth_lens", lens ? 1.f : 0.f, 0.f, 0.f, 0.f);
	RCache.set_ZFunc(D3DCMP_ALWAYS);
	RCache.set_ColorWriteEnable(0);
	RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	RCache.set_ColorWriteEnable();
	RCache.set_CullMode(CULL_CCW);
}

// Redotix99's 3D shader scopes at display resolution, after reconstruction.
// The lens shaders are unchanged: their scene inputs are pointed at display-
// resolution copies for the draw, and screen_res is already the display size.
void CRenderTarget::phase_dlss_scope(float jitterX, float jitterY)
{
	const u32 w = Device.dwWidth, h = Device.dwHeight;
	if (!rt_dlss_scope_color)
	{
		rt_dlss_scope_color.create("$user$dlss_scope_color", w, h, D3DFMT_A16B16G16R16F);
		rt_dlss_scope_position.create("$user$dlss_scope_position", w, h, D3DFMT_A16B16G16R16F);
		rt_dlss_scope_heat.create("$user$dlss_scope_heat", w, h, D3DFMT_A8R8G8B8);
	}
	if (!reported_native_scope)
	{
		Msg("[Upscaler] 3D scope lenses drawn after reconstruction: native=%ux%u, scene=%ux%u",
			w, h, dlss::RenderWidth(), dlss::RenderHeight());
		reported_native_scope = true;
	}

	// Magnify the reconstructed frame, not the jittered render-sized one.
	HW.pContext->CopyResource(rt_dlss_scope_color->pSurface, rt_dlss_post_source->pSurface);

	// Thermal/night modes Load positions and heat at display pixel coordinates.
	const float fw = float(w), fh = float(h);
	u32 offset;
	FVF::TL* quad = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
	quad[0].set(0, fh, EPS_S, 1, 0xffffffff, 0, 1);
	quad[1].set(0, 0, EPS_S, 1, 0xffffffff, 0, 0);
	quad[2].set(fw, fh, EPS_S, 1, 0xffffffff, 1, 1);
	quad[3].set(fw, 0, EPS_S, 1, 0xffffffff, 1, 0);
	RCache.Vertex.Unlock(4, g_combine->vb_stride);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	const ref_rt* copies[] = {&rt_dlss_scope_position, &rt_dlss_scope_heat};
	for (u32 i = 0; i < 2; ++i)
	{
		u_setrt(*copies[i], nullptr, nullptr, nullptr);
		RCache.set_Geometry(g_combine);
		RCache.set_Element(s_dlss_scope_copy->E[i]);
		RCache.set_c("dlss_scope_copy_params", float(dlss::RenderWidth()) / fw,
			float(dlss::RenderHeight()) / fh, jitterX, jitterY);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	}

	phase_dlss_native_depth(jitterX, jitterY, true);
	// Reticles mark stencil 254 and cutouts test it.
	HW.pContext->ClearDepthStencilView(HW.pBaseZB, D3D_CLEAR_STENCIL, 1.f, 0);

	CTexture* inputs[] = {&*rt_Generic_temp->pTexture, &*rt_Generic_2->pTexture, &*rt_Heat->pTexture};
	ID3DBaseTexture* sources[] = {rt_dlss_scope_color->pSurface, rt_dlss_scope_position->pSurface,
		rt_dlss_scope_heat->pSurface};
	ID3DBaseTexture* saved[3];
	for (u32 i = 0; i < 3; ++i)
	{
		saved[i] = inputs[i]->surface_get();
		inputs[i]->surface_set(sources[i]);
	}
	RCache.InvalidateTextures();

	u_setrt(rt_dlss_post_source, nullptr, nullptr, HW.pBaseZB);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	RImplementation.render_Reticle();

	for (u32 i = 0; i < 3; ++i)
	{
		inputs[i]->surface_set(saved[i]);
		_RELEASE(saved[i]);
	}
	RCache.InvalidateTextures();
}

void CRenderTarget::dlss_post_twin(ref_rt& scene, float divisor, int resample)
{
	if (!scene) return;
	string64 name;
	xr_sprintf(name, "$user$dlss_display_%u", u32(dlss_post_twins.size()));
	DlssPostTwin twin{&scene, {}, resample};
	twin.display.create(name, u32(float(Device.dwWidth) / divisor), u32(float(Device.dwHeight) / divisor), scene->fmt);
	dlss_post_twins.push_back(twin);
}

bool CRenderTarget::DlssPostAfterUpscale() const
{
	// Frames that are not reconstructed (scope views) keep the render-size chain.
	return !dlss_post_twins.empty() && dlss::Active();
}

// Reconstruct the scene before sunshafts, fog scattering, motion blur, bloom,
// DOF and the LUT. They then run at display resolution and stay out of the
// upscaler's history, so they are no longer reprojected or smeared by it.
void CRenderTarget::dlss_post_upscale()
{
    // PiP scope viewport: its combine runs mid-frame at its own resolution; the main scene is still
    // open (inScene) and is evaluated/closed by the main combine that follows
    if (dlss::ScopePass()) return;
#ifdef XR_DX12_BRIDGE
	fg::CaptureScene();
#endif
	if (!dlss::Configured())
	{
		dlss::EndScene();
		return;
	}
	PROF_EVENT("Render.Upscaler.ResolveAndOverlays");
	const auto jitter = dlss::RasterJitter();
	dlss_post_jitter.set(jitter.x, jitter.y);
	dlss_post_output = dlss::Evaluate(rt_Generic_0->pSurface, rt_ssfx_motion_vectors->pSurface, reactive_texture());
	dlss::EndScene();
	if (!reported_post_chain)
	{
		Msg("[Upscaler] Post-processing and volumetric lights drawn after reconstruction: native=%ux%u, scene=%ux%u",
			Device.dwWidth, Device.dwHeight, dlss::RenderWidth(), dlss::RenderHeight());
		reported_post_chain = true;
	}

	const float w = float(Device.dwWidth), h = float(Device.dwHeight);
	u32 offset;
	FVF::TL* quad = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
	quad[0].set(0, h, EPS_S, 1, 0xffffffff, 0, 1);
	quad[1].set(0, 0, EPS_S, 1, 0xffffffff, 0, 0);
	quad[2].set(w, h, EPS_S, 1, 0xffffffff, 1, 1);
	quad[3].set(w, 0, EPS_S, 1, 0xffffffff, 1, 0);
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// CAS stays fused into the resolve draw, ahead of bloom, DOF and the LUT.
	const bool sharpen = dlss_post_output && ps_r_dlss_sharpness > 0.0f;
	u_setrt(dlss_post_twins[0].display, nullptr, nullptr, nullptr);
	RImplementation.rmNormal();
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	RCache.set_Element(s_dlss_copy->E[dlss_post_output ? (sharpen ? 2 : 0) : 4]);
	if (sharpen) RCache.set_c("dlss_sharpening", ps_r_dlss_sharpness, 0.f, 0.f, 0.f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);

	for (DlssPostTwin& twin : dlss_post_twins)
	{
		if (twin.resample < 0) continue;
		u_setrt(twin.display, nullptr, nullptr, nullptr);
		RCache.set_Element(s_dlss_scope_copy->E[twin.resample]);
		RCache.set_c("dlss_scope_copy_params", float(dlss::RenderWidth()) / w,
			float(dlss::RenderHeight()) / h, jitter.x, jitter.y);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	}

	for (DlssPostTwin& twin : dlss_post_twins)
		(*twin.scene)->swap_storage(*twin.display);
	dlss_post_swapped = true;
	RCache.InvalidateTextures();
}

void CRenderTarget::dlss_post_restore()
{
	if (!dlss_post_swapped) return;
	for (DlssPostTwin& twin : dlss_post_twins)
		(*twin.scene)->swap_storage(*twin.display);
	dlss_post_swapped = false;
	RCache.InvalidateTextures();
}

// Particles and laser beams held out of the scene, over the reconstructed frame.
// Soft particles and laser dots read the display-size position copy; the depth
// test uses the scene depth reprojected to display resolution.
void CRenderTarget::dlss_post_effects()
{
	const auto& graph = RImplementation.GMBase.RGraph;
	if (graph.mapDlssEffects.empty() && graph.mapDlssHudEffects.empty()) return;
	if (!reported_post_effects)
	{
		Msg("[Upscaler] Particles and laser beams drawn after reconstruction: native=%ux%u, scene=%ux%u",
			Device.dwWidth, Device.dwHeight, dlss::RenderWidth(), dlss::RenderHeight());
		reported_post_effects = true;
	}

	phase_dlss_native_depth(dlss_post_jitter.x, dlss_post_jitter.y);
	u_setrt(rt_Generic_0, nullptr, nullptr, HW.pBaseZB);
	RImplementation.rmNormal();
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	RImplementation.GMBase.r_dsgraph_render_dlss_effects();
}

void CRenderTarget::phase_dlss_postprocess()
{
	// With the post chain after reconstruction, the scene targets hold its display-size result.
	const bool postAfterUpscale = dlss_post_swapped;
	dlss::Offset sceneJitter{dlss_post_jitter.x, dlss_post_jitter.y};
	bool dlssOutput = dlss_post_output;
	if (!postAfterUpscale)
	{
#ifdef XR_DX12_BRIDGE
		fg::CaptureScene();
#endif
		sceneJitter = dlss::RasterJitter();
		dlssOutput = dlss::Evaluate(rt_Color->pSurface, rt_ssfx_motion_vectors->pSurface);
		dlss::EndScene();
	}
	if (!dlss::Configured()) return;

	// Separate display-resolution overlay buffers. Never feed screen-locked
	// distortion, NV noise or fake scopes back into the scene's DLSS history.
	u_setrt(rt_dlss_post_source, nullptr, nullptr, nullptr);
	RImplementation.rmNormal();
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	u32 offset;
	const float w = float(Device.dwWidth), h = float(Device.dwHeight);
	FVF::TL* vertices = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
	vertices[0].set(0, h, EPS_S, 1, 0xffffffff, 0, 1);
	vertices[1].set(0, 0, EPS_S, 1, 0xffffffff, 0, 0);
	vertices[2].set(w, h, EPS_S, 1, 0xffffffff, 1, 1);
	vertices[3].set(w, 0, EPS_S, 1, 0xffffffff, 1, 0);
	RCache.Vertex.Unlock(4, g_combine->vb_stride);
	// Fuse optional CAS into the existing resolve draw: no extra target or copy.
	// Zero retains the original copy shader; fallback/scope frames are unsharpened.
	// After an earlier reconstruction this only copies the post chain's result.
	const bool sharpen = !postAfterUpscale && dlssOutput && ps_r_dlss_sharpness > 0.0f;
	// r_upscaler_debug: 1 shows the upscaler's input, 2 its motion vectors. DLSS
	// still evaluates, so its history stays valid when the view is switched off.
	const bool mv_view = ps_r_upscaler_debug == 2 && s_dlss_copy->E[3];
	const u32 element = mv_view ? 3 : ps_r_upscaler_debug ? (postAfterUpscale ? 4 : 1) :
		postAfterUpscale ? 4 : dlssOutput ? (sharpen ? 2 : 0) : 1;
	// The debug views read the render-size scene targets.
	if (ps_r_upscaler_debug == 1 || ps_r_upscaler_debug == 2) dlss_post_restore();
	RCache.set_Element(s_dlss_copy->E[element]);
	if (element == 2) RCache.set_c("dlss_sharpening", ps_r_dlss_sharpness, 0.f, 0.f, 0.f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	dlss_post_restore();

	// 3D scope lenses magnify the frame around them. In the scene the zoom also
	// magnified the jitter and was reconstructed with the world's motion vectors.
	if (scope_3D_fake_enabled && ScopeAfterUpscale() && !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty())
		phase_dlss_scope(sceneJitter.x, sceneJitter.y);

	// Lens flares are additive, depth-free sprites that cover the HUD. In the scene
	// they would be reprojected with the hands' motion vectors and reconstructed as
	// blocks, so draw them here at display resolution without jitter.
	if (ps_r2_anomaly_flags.test(R2_AN_FLAG_FLARES) && ps_r2_heatvision == 0)
		g_pGamePersistent->Environment().RenderFlares();

	// Only defer direct UI draws when wearable surfaces were found in this view.
	// A normal weapon/device must not trigger an additional native depth pass.
	const bool renderHudUi = RImplementation.GMBase.RGraph.hasNativeHudAttachments &&
		g_hud && g_hud->RenderActiveItemUIQuery();
	if (renderHudUi || !RImplementation.GMBase.RGraph.mapHUDNativeUi.empty())
	{
        PROF_EVENT("Render.Upscaler.NativeHUD");
		if (!reported_native_ui)
		{
			Msg("[Upscaler] HUD UI drawn after reconstruction: native=%ux%u, scene=%ux%u, meshes=%u, item_ui=%u",
				Device.dwWidth, Device.dwHeight, dlss::RenderWidth(), dlss::RenderHeight(),
				unsigned(RImplementation.GMBase.RGraph.mapHUDNativeUi.size()), unsigned(renderHudUi));
			reported_native_ui = true;
		}
		// Not the lenses' widened depth: these screens need their exact edges.
		phase_dlss_native_depth(sceneJitter.x, sceneJitter.y);
		u_setrt(rt_dlss_post_source, nullptr, nullptr, HW.pBaseZB);
		RCache.set_CullMode(CULL_CCW);
		RImplementation.GMBase.r_dsgraph_render_native_ui(renderHudUi);
	}

	if (ps_r2_mask_control.x > 0)
	{
		phase_gasmask_dudv();
		if (ps_r2_drops_control.x > 0) phase_gasmask_drops();
	}
	if (ps_r2_nightvision > 0) phase_nightvision();
	if (ps_r2_heatvision > 0) phase_heatvision();
	// pip the physical scope renders the sight while the true-PiP SVP is live
	if (scope_fake_enabled && !(Device.true_pip_on && Device.m_SecondViewport.IsSVPActive())) phase_fakescope();
}

void CRenderTarget::phase_pp()
{
	phase_dlss_postprocess();
	// combination/postprocess
	u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	RImplementation.rmNormal();
	//	Element 0 for for normal post-process
	//	Element 4 for color map post-process
	bool bCMap = u_need_CM();
	//RCache.set_Element	(s_postprocess->E[bCMap ? 4 : 0]);
	if (dlss::Configured())
	{
		RCache.set_Element(s_dlss_postprocess->E[bCMap ? 4 : 0]);
	}
	else if (!RImplementation.o.dx10_msaa)
	{
		//		RCache.set_Shader	(s_postprocess	);
		RCache.set_Element(s_postprocess->E[bCMap ? 4 : 0]);
	}
	else
	{
		//		RCache.set_Shader( s_postprocess_msaa );
		RCache.set_Element(s_postprocess_msaa->E[bCMap ? 4 : 0]);
	}

	int gblend = clampr(iFloor((1 - param_gray) * 255.f), 0, 255);
	int nblend = clampr(iFloor((1 - param_noise) * 255.f), 0, 255);
	u32 p_color = subst_alpha(param_color_base, nblend);
	u32 p_gray = subst_alpha(param_color_gray, gblend);
	Fvector p_brightness = param_color_add;
	// Msg				("param_gray:%f(%d),param_noise:%f(%d)",param_gray,gblend,param_noise,nblend);
	// Msg				("base: %d,%d,%d",	color_get_R(p_color),		color_get_G(p_color),		color_get_B(p_color));
	// Msg				("gray: %d,%d,%d",	color_get_R(p_gray),		color_get_G(p_gray),		color_get_B(p_gray));
	// Msg				("add:  %d,%d,%d",	color_get_R(p_brightness),	color_get_G(p_brightness),	color_get_B(p_brightness));

	// Draw full-screen quad textured with our scene image
	u32 Offset;
	float _w = float(Device.dwWidth);
	float _h = float(Device.dwHeight);

	Fvector2 n0, n1, r0, r1, l0, l1;
	u_calc_tc_duality_ss(r0, r1, l0, l1);
	u_calc_tc_noise(n0, n1);

	// Fill vertex buffer
	float du = ps_r1_pps_u, dv = ps_r1_pps_v;
	TL_2c3uv* pv = (TL_2c3uv*)RCache.Vertex.Lock(4, g_postprocess.stride(), Offset);
	pv->set(du + 0, dv + float(_h), p_color, p_gray, r0.x, r1.y, l0.x, l1.y, n0.x, n1.y);
	pv++;
	pv->set(du + 0, dv + 0, p_color, p_gray, r0.x, r0.y, l0.x, l0.y, n0.x, n0.y);
	pv++;
	pv->set(du + float(_w), dv + float(_h), p_color, p_gray, r1.x, r1.y, l1.x, l1.y, n1.x, n1.y);
	pv++;
	pv->set(du + float(_w), dv + 0, p_color, p_gray, r1.x, r0.y, l1.x, l0.y, n1.x, n0.y);
	pv++;
	RCache.Vertex.Unlock(4, g_postprocess.stride());

	// Actual rendering
	static shared_str s_brightness = "c_brightness";
	static shared_str s_colormap = "c_colormap";
	RCache.set_c(s_brightness, p_brightness.x, p_brightness.y, p_brightness.z, 0);
	RCache.set_c(s_colormap, param_color_map_influence, param_color_map_interpolate, 0, 0);
	RCache.set_Geometry(g_postprocess);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

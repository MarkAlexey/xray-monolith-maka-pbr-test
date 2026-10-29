#include "stdafx.h"
#include "scope_vslr_renderer.h"
#include "../xrRender/resourcemanager.h"
#include "blender_light_occq.h"
#include "blender_sslr.h"
#include "blender_skin_sss.h"
#include "blender_upscaler_reactive.h"
#include "blender_accum_csm.h"
#include "blender_light_mask.h"
#include "blender_light_direct.h"
#include "blender_light_point.h"
#include "blender_light_spot.h"
#include "blender_light_reflected.h"
#include "blender_combine.h"
#include "blender_bloom_build.h"
#include "blender_luminance.h"
#include "blender_ssao.h"
#include "dx11MinMaxSMBlender.h"
#include "dx11HDAOCSBlender.h"
#include "../xrRenderDX10/msaa/dx10MSAABlender.h"
#include "../xrRenderDX10/DX10 Rain/dx10RainBlender.h"
////////////////////////////lvutner
#include "blender_ss_sunshafts.h"
#include "blender_gasmask_drops.h"
#include "blender_gasmask_dudv.h"
#include "blender_smaa.h"
#include "blender_blur.h"
#include "blender_dof.h"
#include "blender_pp_bloom.h"
#include "blender_nightvision.h"
#include "blender_lut.h"

#include "../xrRender/dxRenderDeviceRender.h"
#include "../xrRender/xrRender_console.h"

#include <D3DX10Tex.h>

D3D_VIEWPORT custom_viewport[1] = { 0, 0, 0, 0, 0.f, 1.f };

class CBlender_dlss_postprocess : public IBlender
{
public:
    CBlender_dlss_postprocess() { description.CLS = 0; }
    LPCSTR getComment() override { return "DLSS display-resolution postprocess"; }
    BOOL canBeDetailed() override { return FALSE; }
    BOOL canBeLMAPped() override { return FALSE; }
    void Compile(CBlender_Compile& C) override
    {
        IBlender::Compile(C);
        if (C.iElement != 0 && C.iElement != 4) return;
        C.r_Pass("stub_notransform_postpr", C.iElement == 4 ? "postprocess_CM" : "postprocess",
            FALSE, FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_base0", "$user$dlss_post_source");
        C.r_dx10Texture("s_base1", "$user$dlss_post_source");
        C.r_dx10Texture("s_noise", "fx\\fx_noise2");
        if (C.iElement == 4)
        {
            C.r_dx10Texture("s_grad0", "$user$cmap0");
            C.r_dx10Texture("s_grad1", "$user$cmap1");
        }
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
    }
};

class CBlender_dlss_menu : public IBlender
{
public:
    CBlender_dlss_menu() { description.CLS = 0; }
    LPCSTR getComment() override { return "Display-resolution menu"; }
    BOOL canBeDetailed() override { return FALSE; }
    BOOL canBeLMAPped() override { return FALSE; }
    void Compile(CBlender_Compile& C) override
    {
        IBlender::Compile(C);
        C.r_Pass("stub_notransform_t", "distort", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_base", "$user$dlss_post_source");
        C.r_dx10Texture("s_distort", "$user$dlss_post_color");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
    }
};

class CBlender_dlss_copy : public IBlender
{
public:
    CBlender_dlss_copy() { description.CLS = 0; }
    LPCSTR getComment() override { return "DLSS resolve / spatial fallback"; }
    BOOL canBeDetailed() override { return FALSE; }
    BOOL canBeLMAPped() override { return FALSE; }
    void Compile(CBlender_Compile& C) override
    {
        IBlender::Compile(C);
        if (C.iElement > 2) return;
        C.r_Pass("stub_screen_space", C.iElement == 2 ? "dlss_sharpen" : "dlss_copy", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", C.iElement == 1 ? r2_RT_albedo : "$user$dlss_output");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
    }
};

class CBlender_dlss_ui_depth : public IBlender
{
public:
    CBlender_dlss_ui_depth() { description.CLS = 0; }
    LPCSTR getComment() override { return "Native PDA screen occlusion depth"; }
    BOOL canBeDetailed() override { return FALSE; }
    BOOL canBeLMAPped() override { return FALSE; }
    void Compile(CBlender_Compile& C) override
    {
        IBlender::Compile(C);
        if (C.iElement != 0) return;
        C.r_Pass("stub_screen_space", "dlss_ui_depth", FALSE, TRUE, TRUE);
        C.r_dx10Texture("s_scene_depth", "$user$dlss_scene_depth");
        C.r_End();
    }
};

void CRenderTarget::set_viewport_size(ID3DDeviceContext * dev, float w, float h)
{
	custom_viewport[0].Width = w;
	custom_viewport[0].Height = h;
	dev->RSSetViewports(1, custom_viewport);
}

void CRenderTarget::u_setrt(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& _4, ID3DDepthStencilView* zb)
{
	if (zb == HW.pBaseZB) zb = dlss::SceneDepth();
	VERIFY(_1 || zb);
	if (_1)
	{
		dwWidth = _1->dwWidth;
		dwHeight = _1->dwHeight;
	}
	else
	{
		D3D_DEPTH_STENCIL_VIEW_DESC desc;
		zb->GetDesc(&desc);

		if (!RImplementation.o.dx10_msaa)
			VERIFY(desc.ViewDimension == D3D_DSV_DIMENSION_TEXTURE2D);

		ID3DResource* pRes;

		zb->GetResource(&pRes);

		ID3DTexture2D* pTex = (ID3DTexture2D*)pRes;

		D3D_TEXTURE2D_DESC TexDesc;

		pTex->GetDesc(&TexDesc);

		dwWidth = TexDesc.Width;
		dwHeight = TexDesc.Height;
		_RELEASE(pRes);
	}

	if (_1) RCache.set_RT(_1->pRT, 0);
	else RCache.set_RT(NULL, 0);
	if (_2) RCache.set_RT(_2->pRT, 1);
	else RCache.set_RT(NULL, 1);
	if (_3) RCache.set_RT(_3->pRT, 2);
	else RCache.set_RT(NULL, 2);
	if (_4) RCache.set_RT(_4->pRT, 3);
	else RCache.set_RT(NULL, 3);
	RCache.set_ZB(zb == HW.pBaseZB ? dlss::SceneDepth() : zb);
	//	RImplementation.rmNormal				();
}

void CRenderTarget::u_setrt(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& _4, const ref_rt& _5, ID3DDepthStencilView* zb)
{
	u_setrt(_1, _2, _3, _4, zb);
	RCache.set_RT(_5 ? _5->pRT : NULL, 4);
}

void CRenderTarget::u_setrt(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, ID3DDepthStencilView* zb)
{
	if (zb == HW.pBaseZB) zb = dlss::SceneDepth();
	VERIFY(_1||zb);
	if (_1)
	{
		dwWidth = _1->dwWidth;
		dwHeight = _1->dwHeight;
	}
	else if (!ps_r__light_lean || !RCache.get_smapsize_memo(zb, dwWidth, dwHeight))
	{
		D3D_DEPTH_STENCIL_VIEW_DESC desc;
		zb->GetDesc(&desc);

		if (!RImplementation.o.dx10_msaa)
			VERIFY(desc.ViewDimension==D3D_DSV_DIMENSION_TEXTURE2D);

		ID3DResource* pRes;

		zb->GetResource(&pRes);

		ID3DTexture2D* pTex = (ID3DTexture2D *)pRes;

		D3D_TEXTURE2D_DESC TexDesc;

		pTex->GetDesc(&TexDesc);

		dwWidth = TexDesc.Width;
		dwHeight = TexDesc.Height;
		_RELEASE(pRes);

		if (ps_r__light_lean)
			RCache.set_smapsize_memo(zb, dwWidth, dwHeight);
	}

	if (_1) RCache.set_RT(_1->pRT, 0);
	else RCache.set_RT(NULL, 0);
	if (_2) RCache.set_RT(_2->pRT, 1);
	else RCache.set_RT(NULL, 1);
	if (_3) RCache.set_RT(_3->pRT, 2);
	else RCache.set_RT(NULL, 2);
	RCache.set_ZB(zb == HW.pBaseZB ? dlss::SceneDepth() : zb);
	//	RImplementation.rmNormal				();
}

void CRenderTarget::u_setrt(const ref_rt& _1, const ref_rt& _2, ID3DDepthStencilView* zb)
{
	if (zb == HW.pBaseZB) zb = dlss::SceneDepth();
	VERIFY(_1||zb);
	if (_1)
	{
		dwWidth = _1->dwWidth;
		dwHeight = _1->dwHeight;
	}
	else
	{
		D3D_DEPTH_STENCIL_VIEW_DESC desc;
		zb->GetDesc(&desc);
		if (! RImplementation.o.dx10_msaa)
			VERIFY(desc.ViewDimension==D3D_DSV_DIMENSION_TEXTURE2D);

		ID3DResource* pRes;

		zb->GetResource(&pRes);

		ID3DTexture2D* pTex = (ID3DTexture2D *)pRes;

		D3D_TEXTURE2D_DESC TexDesc;

		pTex->GetDesc(&TexDesc);

		dwWidth = TexDesc.Width;
		dwHeight = TexDesc.Height;
		_RELEASE(pRes);
	}

	if (_1) RCache.set_RT(_1->pRT, 0);
	else RCache.set_RT(NULL, 0);
	if (_2) RCache.set_RT(_2->pRT, 1);
	else RCache.set_RT(NULL, 1);
	RCache.set_ZB(zb == HW.pBaseZB ? dlss::SceneDepth() : zb);
	//	RImplementation.rmNormal				();
}

void CRenderTarget::u_setrt(u32 W, u32 H, ID3DRenderTargetView* _1, ID3DRenderTargetView* _2, ID3DRenderTargetView* _3,
                            ID3DDepthStencilView* zb)
{
	//VERIFY									(_1);
	dwWidth = W;
	dwHeight = H;
	//VERIFY									(_1);
	RCache.set_RT(_1, 0);
	RCache.set_RT(_2, 1);
	RCache.set_RT(_3, 2);
	RCache.set_ZB(zb == HW.pBaseZB ? dlss::SceneDepth() : zb);
	//	RImplementation.rmNormal				();
}

void CRenderTarget::u_stencil_optimize(eStencilOptimizeMode eSOM)
{
	//	TODO: DX10: remove half pixel offset?
	VERIFY(RImplementation.o.nvstencil);
	//RCache.set_ColorWriteEnable	(FALSE);
	u32 Offset;
	float _w = float(RenderScreenWidth());
	float _h = float(RenderScreenHeight());
	u32 C = color_rgba(255, 255, 255, 255);
	float eps = 0;
	float _dw = 0.5f;
	float _dh = 0.5f;
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(-_dw, _h - _dh, eps, 1.f, C, 0, 0);
	pv++;
	pv->set(-_dw, -_dh, eps, 1.f, C, 0, 0);
	pv++;
	pv->set(_w - _dw, _h - _dh, eps, 1.f, C, 0, 0);
	pv++;
	pv->set(_w - _dw, -_dh, eps, 1.f, C, 0, 0);
	pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);
	RCache.set_Element(s_occq->E[1]);

	switch (eSOM)
	{
	case SO_Light:
		StateManager.SetStencilRef(dwLightMarkerID);
		break;
	case SO_Combine:
		StateManager.SetStencilRef(0x01);
		break;
	default:
		VERIFY(!"CRenderTarget::u_stencil_optimize. switch no default!");
	}

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

// 2D texgen (texture adjustment matrix)
void CRenderTarget::u_compute_texgen_screen(Fmatrix& m_Texgen)
{
	//float	_w						= float(Device.dwWidth);
	//float	_h						= float(Device.dwHeight);
	//float	o_w						= (.5f / _w);
	//float	o_h						= (.5f / _h);
	Fmatrix m_TexelAdjust =
	{
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		//	Removing half pixel offset
		//0.5f + o_w,			0.5f + o_h,			0.0f,			1.0f
		0.5f, 0.5f, 0.0f, 1.0f
	};
	m_Texgen.mul(m_TexelAdjust, RCache.xforms.m_wvp);
}

// 2D texgen for jitter (texture adjustment matrix)
void CRenderTarget::u_compute_texgen_jitter(Fmatrix& m_Texgen_J)
{
	// place into	0..1 space
	Fmatrix m_TexelAdjust =
	{
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f
	};
	m_Texgen_J.mul(m_TexelAdjust, RCache.xforms.m_wvp);

	// rescale - tile it
	float scale_X = float(RenderScreenWidth()) / float(TEX_jitter);
	float scale_Y = float(RenderScreenHeight()) / float(TEX_jitter);
	//float	offset			= (.5f / float(TEX_jitter));
	m_TexelAdjust.scale(scale_X, scale_Y, 1.f);
	//m_TexelAdjust.translate_over(offset,	offset,	0	);
	m_Texgen_J.mulA_44(m_TexelAdjust);
}

u8 fpack(float v)
{
	s32 _v = iFloor(((v + 1) * .5f) * 255.f + .5f);
	clamp(_v, 0, 255);
	return u8(_v);
}

u8 fpackZ(float v)
{
	s32 _v = iFloor(_abs(v) * 255.f + .5f);
	clamp(_v, 0, 255);
	return u8(_v);
}

Fvector vunpack(s32 x, s32 y, s32 z)
{
	Fvector pck;
	pck.x = (float(x) / 255.f - .5f) * 2.f;
	pck.y = (float(y) / 255.f - .5f) * 2.f;
	pck.z = -float(z) / 255.f;
	return pck;
}

Fvector vunpack(Ivector src)
{
	return vunpack(src.x, src.y, src.z);
}

Ivector vpack(Fvector src)
{
	Fvector _v;
	int bx = fpack(src.x);
	int by = fpack(src.y);
	int bz = fpackZ(src.z);
	// dumb test
	float e_best = flt_max;
	int r = bx, g = by, b = bz;
#ifdef DEBUG
	int		d=0;
#else
	int d = 3;
#endif
	for (int x = _max(bx - d, 0); x <= _min(bx + d, 255); x++)
		for (int y = _max(by - d, 0); y <= _min(by + d, 255); y++)
			for (int z = _max(bz - d, 0); z <= _min(bz + d, 255); z++)
			{
				_v = vunpack(x, y, z);
				float m = _v.magnitude();
				float me = _abs(m - 1.f);
				if (me > 0.03f) continue;
				_v.div(m);
				float e = _abs(src.dotproduct(_v) - 1.f);
				if (e < e_best)
				{
					e_best = e;
					r = x, g = y, b = z;
				}
			}
	Ivector ipck;
	ipck.set(r, g, b);
	return ipck;
}

void generate_jitter(DWORD* dest, u32 elem_count)
{
	const int cmax = 8;
	svector<Ivector2, cmax> samples;
	while (samples.size() < elem_count * 2)
	{
		Ivector2 test;
		test.set(::Random.randI(0, 256), ::Random.randI(0, 256));
		BOOL valid = TRUE;
		for (u32 t = 0; t < samples.size(); t++)
		{
			int dist = _abs(test.x - samples[t].x) + _abs(test.y - samples[t].y);
			if (dist < 32)
			{
				valid = FALSE;
				break;
			}
		}
		if (valid) samples.push_back(test);
	}
	for (u32 it = 0; it < elem_count; it++, dest++)
		*dest = color_rgba(samples[2 * it].x, samples[2 * it].y, samples[2 * it + 1].y, samples[2 * it + 1].x);
}

#if USE_DX11
void CRenderTarget::SetActive(bool force)
{
	const bool isMain = this == RImplementation.TargetMain;
	if (isMain)
	{
		// main restores from the persistent swapchain RTV/DSV, not HW.pBaseRT, which SetActive
		// clobbers per target so re-reading it would capture the SVP RT and freeze the screen
		baseRT = HW.secret_pBaseRT;
		baseZB = HW.secret_pBaseZB;
	}
	HW.pBaseRT = baseRT;
	HW.pBaseZB = baseZB;

	if (!force && RImplementation.Target == this)
	{
		for (auto& r : RenderTargetRemaps)
			r.first->surface_set(r.second->pSurface);
		return;
	}

	auto m = Device.matrices[isMain ? 0 : 1];
	if (g_pGamePersistent)
	{
		g_pGamePersistent->m_pGShaderConstants->hud_params.w = !isMain;
		Device.m_SecondViewport.m_render_pass_is_svp = !isMain;
		RImplementation.SetMatrices(m.mView, m.mProject, m.mProjectHud);
	}
	RImplementation.Target = this;

	RImplementation.ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

	// main: the display size (Width/Height is the scene size under DLSS / FSR 3)
	const u32 deviceWidth = isMain ? DisplayWidth : Width;
	const u32 deviceHeight = isMain ? DisplayHeight : Height;
	Device.dwWidth = deviceWidth;
	Device.dwHeight = deviceHeight;
	Device.fASPECT = (float)deviceHeight / (float)deviceWidth;
	Device.fWidth_2 = deviceWidth >> 1;
	Device.fHeight_2 = deviceHeight >> 1;

	// the bind cache keys on STextureList/CTexture identity (R_Backend set_Textures), so the
	// surface_set remap below is invisible to it, a full invalidate forces every RT/texture/
	// matrix/constant to re-bind for this viewport (non-MT calls RCache.Invalidate here too)
	RCache.Invalidate();

	// point the stock-named textures at this viewport's RTs (restore for the main)
	for (auto& r : RenderTargetRemaps)
		r.first->surface_set(r.second->pSurface);

	// main under DLSS / FSR3 keeps the render-size scene viewport (RenderScreenWidth is the
	// display size outside the scene and for the SVP target)
	set_viewport_size(HW.pContext, (float)RenderScreenWidth(), (float)RenderScreenHeight());
	RCache.set_RT(baseRT, 0);
	RCache.set_RT(nullptr, 1);
	RCache.set_RT(nullptr, 2);
	RCache.set_RT(nullptr, 3);
	RCache.set_ZB(baseZB);
}
#endif

CRenderTarget::CRenderTarget()
	: CRenderTarget(nullptr, Device.dwWidth, Device.dwHeight)
{
}

CRenderTarget::CRenderTarget(LPCSTR name, u32 width, u32 height)
	: Width(width), Height(height), DisplayWidth(width), DisplayHeight(height)
{
	// The upscaler belongs to the main target. The PiP SVP target is created mid-frame (ADS-in,
	// adaptive resize): re-initializing DLSS there released and rebuilt the feature and closed
	// the open scene (scene depth, viewport and jitter fell back to display size for that frame).
	if (!name)
	{
		dlss::InitializeTargets();
		Width = dlss::RenderWidth();
		Height = dlss::RenderHeight();
	}
	u32 SampleCount = 1;

	if (ps_r_ssao_mode != 2/*hdao*/)
		ps_r_ssao = _min(ps_r_ssao, 3);

	RImplementation.o.ssao_ultra = ps_r_ssao > 3;
	if (RImplementation.o.dx10_msaa)
		SampleCount = RImplementation.o.dx10_msaa_samples;

#ifdef DEBUG
	Msg			("MSAA samples = %d", SampleCount );
	if( RImplementation.o.dx10_msaa_opt )
		Msg		("dx10_MSAA_opt = on" );
#endif // DEBUG
	param_blur = 0.f;
	param_gray = 0.f;
	param_noise = 0.f;
	param_duality_h = 0.f;
	param_duality_v = 0.f;
	param_noise_fps = 25.f;
	param_noise_scale = 1.f;

	im_noise_time = 1.0f / 100.0f; //Alundaio should be float?
	im_noise_shift_w = 0;
	im_noise_shift_h = 0;

	param_color_base = color_rgba(127, 127, 127, 0);
	param_color_gray = color_rgba(85, 85, 85, 0);
	//param_color_add		= color_rgba(0,0,0,			0);
	param_color_add.set(0.0f, 0.0f, 0.0f);

	dwAccumulatorClearMark = 0;
	dxRenderDeviceRender::Instance().Resources->Evict();

	// Blenders
	b_occq = xr_new<CBlender_light_occq>();
	b_accum_mask = xr_new<CBlender_accum_direct_mask>();
	b_accum_direct = xr_new<CBlender_accum_direct>();
	b_accum_csm = xr_new<CBlender_accum_csm>();
	b_accum_point = xr_new<CBlender_accum_point>();
	b_accum_spot = xr_new<CBlender_accum_spot>();
	b_accum_reflected = xr_new<CBlender_accum_reflected>();
	b_bloom = xr_new<CBlender_bloom_build>();
	if (RImplementation.o.dx10_msaa)
	{
		b_bloom_msaa = xr_new<CBlender_bloom_build_msaa>();
		b_postprocess_msaa = xr_new<CBlender_postprocess_msaa>();
	}
	else
	{
		b_bloom_msaa = nullptr;
		b_postprocess_msaa = nullptr;
	}
	b_luminance = xr_new<CBlender_luminance>();
	b_combine = xr_new<CBlender_combine>();
	b_ssao = xr_new<CBlender_SSAO_noMSAA>();
	///////////////////////////////////lvutner
	b_sunshafts = xr_new<CBlender_sunshafts>();
	b_blur = xr_new<CBlender_blur>();
	b_pp_bloom = xr_new<CBlender_pp_bloom>();
	b_dof = xr_new<CBlender_dof>();
	b_gasmask_drops = xr_new<CBlender_gasmask_drops>();
	b_gasmask_dudv = xr_new<CBlender_gasmask_dudv>();
	b_nightvision = xr_new<CBlender_nightvision>();
	b_fakescope = xr_new<CBlender_fakescope>(); //crookr
	b_heatvision = xr_new<CBlender_heatvision>(); //--DSR-- HeatVision
	b_lut = xr_new<CBlender_lut>();
	b_smaa = xr_new<CBlender_smaa>();

	// Screen Space Shaders Stuff
	b_ssfx_fog_scattering = xr_new<CBlender_ssfx_fog_scattering>();
	b_ssfx_motion_blur = xr_new<CBlender_ssfx_motion_blur>();
	b_ssfx_taa = xr_new<CBlender_ssfx_taa>();
	b_ssfx_rain = xr_new<CBlender_ssfx_rain>();
	b_ssfx_water_blur = xr_new<CBlender_ssfx_water_blur>();
	b_ssfx_bloom = xr_new<CBlender_ssfx_bloom_build>();
	b_ssfx_bloom_lens = xr_new<CBlender_ssfx_bloom_lens>();
	b_ssfx_bloom_downsample = xr_new<CBlender_ssfx_bloom_downsample>();
	b_ssfx_bloom_upsample = xr_new<CBlender_ssfx_bloom_upsample>();
	b_ssfx_sss_ext = xr_new<CBlender_ssfx_sss_ext>(); // SSS
	b_ssfx_sss = xr_new<CBlender_ssfx_sss>(); // SSS
	b_ssfx_ssr = xr_new<CBlender_ssfx_ssr>(); // SSR
	b_ssfx_volumetric_blur = xr_new<CBlender_ssfx_volumetric_blur>(); // Volumetric Blur
	b_ssfx_ao = xr_new<CBlender_ssfx_ao>(); // AO

	// IX-Ray SSLR
	b_sslr = xr_new<CBlender_sslr>();
	b_vslr = xr_new<CBlender_vslr>();

	// Skin subsurface scattering
	b_skin_sss = xr_new<CBlender_skin_sss>();

	// Reactive mask for DLSS / FSR 3
	b_upscaler_reactive = xr_new<CBlender_upscaler_reactive>();

	// HDAO
	b_hdao_cs = xr_new<CBlender_CS_HDAO>();
	if (RImplementation.o.dx10_msaa)
	{
		b_hdao_msaa_cs = xr_new<CBlender_CS_HDAO_MSAA>();
	}

	if (RImplementation.o.dx10_msaa)
	{
		int bound = RImplementation.o.dx10_msaa_samples;

		if (RImplementation.o.dx10_msaa_opt)
			bound = 1;

		for (int i = 0; i < bound; ++i)
		{
			static LPCSTR SampleDefs[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
			b_combine_msaa[i] = xr_new<CBlender_combine_msaa>();
			b_accum_mask_msaa[i] = xr_new<CBlender_accum_direct_mask_msaa>();
			b_accum_direct_msaa[i] = xr_new<CBlender_accum_direct_msaa>();
			b_accum_direct_volumetric_msaa[i] = xr_new<CBlender_accum_direct_volumetric_msaa>();
			//b_accum_direct_volumetric_sun_msaa[i]	= xr_new<CBlender_accum_direct_volumetric_sun_msaa>			();
			b_accum_spot_msaa[i] = xr_new<CBlender_accum_spot_msaa>();
			b_accum_volumetric_msaa[i] = xr_new<CBlender_accum_volumetric_msaa>();
			b_accum_point_msaa[i] = xr_new<CBlender_accum_point_msaa>();
			b_accum_reflected_msaa[i] = xr_new<CBlender_accum_reflected_msaa>();
			b_ssao_msaa[i] = xr_new<CBlender_SSAO_MSAA>();
			static_cast<CBlender_accum_direct_mask_msaa*>(b_accum_mask_msaa[i])->SetDefine("ISAMPLE", SampleDefs[i]);
			static_cast<CBlender_accum_direct_volumetric_msaa*>(b_accum_direct_volumetric_msaa[i])->SetDefine(
				"ISAMPLE", SampleDefs[i]);
			//static_cast<CBlender_accum_direct_volumetric_sun_msaa*>(b_accum_direct_volumetric_sun_msaa[i])->SetDefine( "ISAMPLE", SampleDefs[i]);
			static_cast<CBlender_accum_direct_msaa*>(b_accum_direct_msaa[i])->SetDefine("ISAMPLE", SampleDefs[i]);
			static_cast<CBlender_accum_volumetric_msaa*>(b_accum_volumetric_msaa[i])->SetDefine(
				"ISAMPLE", SampleDefs[i]);
			static_cast<CBlender_accum_spot_msaa*>(b_accum_spot_msaa[i])->SetDefine("ISAMPLE", SampleDefs[i]);
			static_cast<CBlender_accum_point_msaa*>(b_accum_point_msaa[i])->SetDefine("ISAMPLE", SampleDefs[i]);
			static_cast<CBlender_accum_reflected_msaa*>(b_accum_reflected_msaa[i])->SetDefine("ISAMPLE", SampleDefs[i]);
			static_cast<CBlender_combine_msaa*>(b_combine_msaa[i])->SetDefine("ISAMPLE", SampleDefs[i]);
			static_cast<CBlender_SSAO_MSAA*>(b_ssao_msaa[i])->SetDefine("ISAMPLE", SampleDefs[i]);
		}
	}
	// SVP target builds $id-suffixed RTs and remaps them over the stock names in SetActive
	// for the main target (name == nullptr) createUnique is a verbatim passthrough to rt.create
	const bool isMain = (name == nullptr);
	auto createUnique = [this, isMain, name](LPCSTR Name, u32 cw, u32 ch, D3DFORMAT f, u32 sc = 1, bool uav = false) -> ref_rt
	{
		ref_rt rt;
		if (isMain)
			rt.create(Name, cw, ch, f, sc, uav);
		else
		{
			string256 rtname;
			xr_sprintf(rtname, "%s$%s", Name, name);
			rt.create(rtname, cw, ch, f, sc, uav);
		}
		// remap entry, stock-named texture -> this target's RT, SetActive surface_sets the texture
		// to rt->pSurface so the deferred shaders sample the active viewport, main restores the stock
		// surface, SVP points it at the SVP RT, never applied when PiP is off (SetActive gated)
		ref_texture t;
		t.create(Name);
		RenderTargetRemaps.push_back({ t, rt });
		return rt;
	};

	//	NORMAL
	{
		// main: scene resolution (smaller than the display with DLSS / FSR3); the PiP SVP target
		// renders at its own extent and never goes through the upscaler
		u32 w = isMain ? dlss::RenderWidth() : Width, h = isMain ? dlss::RenderHeight() : Height;

		// PiP base RT/ZB, main restores from the swapchain, SVP renders into its own
		if (isMain)
		{
			baseRT = HW.secret_pBaseRT;
			baseZB = HW.secret_pBaseZB;
		}
		else
		{
			rt_baseRT = createUnique("$user$baseRT", w, h, D3DFMT_A8R8G8B8);
			rt_baseZB = createUnique("$user$baseZB", w, h, D3DFMT_D24S8);
			baseRT = rt_baseRT->pRT;
			baseZB = rt_baseZB->pZRT;
		}

		rt_Position = createUnique(r2_RT_P, w, h, D3DFMT_A16B16G16R16F, SampleCount);
		// IX-Ray PBR: third G-buffer target, written by every deferred shader (zero = legacy lighting)
		rt_Surface = createUnique(r2_RT_surface, w, h, D3DFMT_A8R8G8B8, SampleCount);

		if (RImplementation.o.dx10_msaa)
		{
			rt_MSAADepth = createUnique(r2_RT_MSAAdepth, w, h, D3DFMT_D24S8, SampleCount);
		}

		rt_tempzb = createUnique("$user$temp_zb", w, h, D3DFMT_D24S8); // Redotix99: for 3D Shader Based Scopes

		// select albedo & accum
		if (RImplementation.o.mrtmixdepth)
		{
			// NV50
			rt_Color = createUnique(r2_RT_albedo, w, h, D3DFMT_A8R8G8B8, SampleCount);
			rt_Accumulator = createUnique(r2_RT_accum, w, h, D3DFMT_A16B16G16R16F, SampleCount);
		}
		else
		{
			// can't - mix-depth
			if (RImplementation.o.fp16_blend)
			{
				// NV40
				rt_Color = createUnique(r2_RT_albedo, w, h, D3DFMT_A16B16G16R16F, SampleCount); // expand to full
				rt_Accumulator = createUnique(r2_RT_accum, w, h, D3DFMT_A16B16G16R16F, SampleCount);
			}
			else
			{
				// R4xx, no-fp-blend,-> albedo_wo
				VERIFY(RImplementation.o.albedo_wo);
				rt_Color = createUnique(r2_RT_albedo, w, h, D3DFMT_A8R8G8B8, SampleCount); // normal
				rt_Accumulator = createUnique(r2_RT_accum, w, h, D3DFMT_A16B16G16R16F, SampleCount);
				rt_Accumulator_temp = createUnique(r2_RT_accum_temp, w, h, D3DFMT_A16B16G16R16F, SampleCount);
			}
		}

		// generic(LDR) RTs
		//LV - we should change their formats into D3DFMT_A16B16G16R16F for better HDR support.
		rt_Generic_0 = createUnique(r2_RT_generic0, w, h, D3DFMT_A8R8G8B8, 1);
		rt_Generic_1 = createUnique(r2_RT_generic1, w, h, D3DFMT_A8R8G8B8, 1);
		rt_Generic = createUnique(r2_RT_generic, w, h, D3DFMT_A8R8G8B8, 1);

		rt_fakescope = createUnique(r2_RT_scopert, w, h, D3DFMT_A8R8G8B8, 1); //crookr fakescope

		//--DSR-- HeatVision_start
		rt_Heat = createUnique(r2_RT_heat, w, h, D3DFMT_A8R8G8B8, SampleCount);
		//--DSR-- HeatVision_end

		rt_Generic_temp = createUnique("$user$generic_temp", w, h, D3DFMT_A8R8G8B8, RImplementation.o.dx10_msaa ? SampleCount : 1);

		// Reactive mask for DLSS / FSR 3: a copy of rt_Generic_0 (same size and format, for
		// CopyResource) and the R8 mask (D3DFMT_L8 = DXGI_FORMAT_R8_UNORM), scene resolution.
		// Neither upscaler runs with MSAA. Needs its shaders (r4_rendertarget_phase_reactive.cpp).
		// Main target only: the PiP SVP never goes through the upscaler.
		if (isMain)
		{
			string_path fn;
			if (!RImplementation.o.dx10_msaa && FS.exist(fn, "$game_shaders$", "r3\\upscaler_reactive", ".ps")
				&& FS.exist(fn, "$game_shaders$", "r3\\upscaler_reactive_debug", ".ps"))
			{
				rt_upscaler_opaque.create(r2_RT_upscaler_opaque, w, h, rt_Generic_0->fmt, 1);
				rt_upscaler_reactive.create(r2_RT_upscaler_reactive, w, h, D3DFMT_L8, 1);
				s_upscaler_reactive.create(b_upscaler_reactive);
			}
		}

		rt_dof = createUnique(r2_RT_dof, w, h, D3DFMT_A8R8G8B8);

		// pip rt_secondVP format must match rt_Generic_0/rt_Color so phase_svp_capture's CopyResource
		// (and the legacy fakescope copy) have matching formats, else D3D rejects the copy
		// pip rt_secondVP follows the SVP target (render extent), the DLSS stub copies the scene into it and
		// the scope lens upscales it. Ascii repoints out_rtv to a display-res UAV target for the real eval
		// main target: display resolution, drawn after the upscaler (legacy second viewport, PDA UI)
		u32 svp_out_w = isMain ? Device.dwWidth : w;
		u32 svp_out_h = isMain ? Device.dwHeight : h;
		// pip DLSS output usually needs a UAV, add it ONLY on the SVP rt_secondVP at gate != 0. a BGRA
		// (A8R8G8B8) target cannot take a typed UAV on many GPUs, so create it only on the UAV-safe HDR
		// format, Ascii must pick a UAV-capable SVP output for real DLSS. gate 0 keeps the stock target
		m_svp_dlss_built = (!isMain && ps_r__svp_dlss != 0);
		bool svp_uav = (m_svp_dlss_built && rt_Color->fmt == D3DFMT_A16B16G16R16F);
		rt_secondVP = createUnique(r2_RT_secondVP, svp_out_w, svp_out_h, rt_Color->fmt, 1, svp_uav); //--#SM+#-- +SecondVP+
		rt_ui_pda = createUnique(r2_RT_ui, svp_out_w, svp_out_h, D3DFMT_A8R8G8B8);

		// pip mipped svp color source, feeds the near-blur prefilter so each sparse ring area-averages
		if (!isMain)
		{
			D3D_TEXTURE2D_DESC md;
			rt_secondVP->pSurface->GetDesc(&md);
			md.MipLevels = 0;
			md.BindFlags = D3D_BIND_SHADER_RESOURCE | D3D_BIND_RENDER_TARGET;
			md.MiscFlags = D3D_RESOURCE_MISC_GENERATE_MIPS;
			md.CPUAccessFlags = 0;
			R_CHK(HW.pDevice->CreateTexture2D(&md, 0, &m_svp_nb_mip_surf));
			m_svp_nb_mip_tex = dxRenderDeviceRender::Instance().Resources->_CreateTexture("$user$svp_nb_mip");
			m_svp_nb_mip_tex->surface_set(m_svp_nb_mip_surf);
		}

		// PDA, probably not ideal though
// RT - KD
		rt_sunshafts_0 = createUnique(r2_RT_sunshafts0, w, h, D3DFMT_A8R8G8B8);
		rt_sunshafts_1 = createUnique(r2_RT_sunshafts1, w, h, D3DFMT_A8R8G8B8);

		// RT Blur
		rt_blur_h_2 = createUnique(r2_RT_blur_h_2, u32(w/2), u32(h/2), D3DFMT_A8R8G8B8);
		rt_blur_2 = createUnique(r2_RT_blur_2, u32(w/2), u32(h/2), D3DFMT_A8R8G8B8);

		rt_blur_h_4 = createUnique(r2_RT_blur_h_4, u32(w/4), u32(h/4), D3DFMT_A8R8G8B8);
		rt_blur_4 = createUnique(r2_RT_blur_4, u32(w/4), u32(h/4), D3DFMT_A8R8G8B8);

		rt_blur_h_8 = createUnique(r2_RT_blur_h_8, u32(w/8), u32(h/8), D3DFMT_A8R8G8B8);
		rt_blur_8 = createUnique(r2_RT_blur_8, u32(w/8), u32(h/8), D3DFMT_A8R8G8B8);

		rt_pp_bloom = createUnique(r2_RT_pp_bloom, w, h, D3DFMT_A8R8G8B8);

		// Screen Space Shaders Stuff
		rt_ssfx_taa = createUnique(r2_RT_ssfx_taa, w, h, D3DFMT_A16B16G16R16F, SampleCount); // Temp RT

		rt_ssfx_prev_frame = createUnique(r2_RT_ssfx_prev_frame, w, h, D3DFMT_A8R8G8B8); // Temp RT

		rt_ssfx_motion_vectors = createUnique(r2_RT_ssfx_motion_vectors, w, h, D3DFMT_A16B16G16R16F, SampleCount); // HUD mask & Velocity buffer
		
		rt_ssfx = createUnique(r2_RT_ssfx, w, h, D3DFMT_A8R8G8B8); // Temp RT
		rt_ssfx_temp = createUnique(r2_RT_ssfx_temp, w, h, D3DFMT_A8R8G8B8); // Temp RT
		rt_ssfx_temp2 = createUnique(r2_RT_ssfx_temp2, w, h, D3DFMT_A8R8G8B8); // Temp RT
		rt_ssfx_temp3 = createUnique(r2_RT_ssfx_temp3, w, h, D3DFMT_A8R8G8B8); // Temp RT

		rt_ssfx_accum = createUnique(r2_RT_ssfx_accum, w, h, D3DFMT_A16B16G16R16F, SampleCount); // Volumetric Acc
		rt_ssfx_ssr = createUnique(r2_RT_ssfx_ssr, w, h, D3DFMT_A8R8G8B8); // SSR Acc
		rt_ssfx_water = createUnique(r2_RT_ssfx_water, w, h, D3DFMT_A8R8G8B8); // Water Acc
		rt_ssfx_ao = createUnique(r2_RT_ssfx_ao, w, h, D3DFMT_A8R8G8B8); // AO Acc
		rt_ssfx_il = createUnique(r2_RT_ssfx_il, w, h, D3DFMT_A8R8G8B8); // IL Acc

		if (RImplementation.o.ssfx_sss)
		{
			rt_ssfx_sss = createUnique(r2_RT_ssfx_sss, w, h, D3DFMT_A8R8G8B8); // SSS Acc
			rt_ssfx_sss_ext = createUnique(r2_RT_ssfx_sss_ext, w, h, D3DFMT_A8R8G8B8); // SSS EXT Acc
			rt_ssfx_sss_ext2 = createUnique(r2_RT_ssfx_sss_ext2, w, h, D3DFMT_A8R8G8B8); // SSS EXT Acc
			rt_ssfx_sss_tmp = createUnique(r2_RT_ssfx_sss_tmp, w, h, D3DFMT_A8R8G8B8); // SSS EXT Acc
		}

		if (RImplementation.o.ssfx_bloom)
		{
			rt_ssfx_bloom1 = createUnique(r2_RT_ssfx_bloom1, w / 2.0f, h / 2.0f, D3DFMT_A16B16G16R16F); // Bloom
			rt_ssfx_bloom_emissive = createUnique(r2_RT_ssfx_bloom_emissive, w, h, D3DFMT_A8R8G8B8, SampleCount); // Emissive
			rt_ssfx_bloom_lens = createUnique(r2_RT_ssfx_bloom_lens, w / 4.0f, h / 4.0f, D3DFMT_A8R8G8B8); // Lens

			rt_ssfx_bloom_tmp2 = createUnique(r2_RT_ssfx_bloom_tmp2, w / 2.0f, h / 2.0f, D3DFMT_A16B16G16R16F); // Bloom / 2
			rt_ssfx_bloom_tmp4 = createUnique(r2_RT_ssfx_bloom_tmp4, w / 4.0f, h / 4.0f, D3DFMT_A16B16G16R16F); // Bloom / 4
			rt_ssfx_bloom_tmp8 = createUnique(r2_RT_ssfx_bloom_tmp8, w / 8.0f, h / 8.0f, D3DFMT_A16B16G16R16F); // Bloom / 8
			rt_ssfx_bloom_tmp16 = createUnique(r2_RT_ssfx_bloom_tmp16, w / 16.0f, h / 16.0f, D3DFMT_A16B16G16R16F); // Bloom / 16
			rt_ssfx_bloom_tmp32 = createUnique(r2_RT_ssfx_bloom_tmp32, w / 32.0f, h / 32.0f, D3DFMT_A16B16G16R16F); // Bloom / 32
			rt_ssfx_bloom_tmp64 = createUnique(r2_RT_ssfx_bloom_tmp64, w / 64.0f, h / 64.0f, D3DFMT_A16B16G16R16F); // Bloom / 64

			rt_ssfx_bloom_tmp32_2 = createUnique(r2_RT_ssfx_bloom_tmp32_2, w / 32.0f, h / 32.0f, D3DFMT_A16B16G16R16F); // Bloom / 32
			rt_ssfx_bloom_tmp16_2 = createUnique(r2_RT_ssfx_bloom_tmp16_2, w / 16.0f, h / 16.0f, D3DFMT_A16B16G16R16F); // Bloom / 16
			rt_ssfx_bloom_tmp8_2 = createUnique(r2_RT_ssfx_bloom_tmp8_2, w / 8.0f, h / 8.0f, D3DFMT_A16B16G16R16F); // Bloom / 8
			rt_ssfx_bloom_tmp4_2 = createUnique(r2_RT_ssfx_bloom_tmp4_2, w / 4.0f, h / 4.0f, D3DFMT_A16B16G16R16F); // Bloom / 4
		}

		rt_ssfx_volumetric = createUnique(r2_RT_ssfx_volumetric, w / 8.0f, h / 8.0f, D3DFMT_A16B16G16R16F); // Volumetric
		rt_ssfx_volumetric_tmp = createUnique(r2_RT_ssfx_volumetric_tmp, w / 8.0f, h / 8.0f, D3DFMT_A16B16G16R16F); // Volumetric
		rt_ssfx_rain = createUnique(r2_RT_ssfx_rain, w / 8.0f, h / 8.0f, D3DFMT_A8R8G8B8); // Rain refraction buffer
		rt_ssfx_water_waves = createUnique(r2_RT_ssfx_water_waves, 512, 512, D3DFMT_A8R8G8B8); // Water Waves

		rt_ssfx_prevPos = createUnique(r2_RT_ssfx_prevPos, w, h, D3DFMT_A16B16G16R16F, SampleCount);

		//rt_ssfx_hud = createUnique(r2_RT_ssfx_hud, w, h, D3DFMT_A16B16G16R16F); // Deprecated

		if (RImplementation.o.dx10_msaa)
		{
            rt_Generic_0_r = createUnique(r2_RT_generic0_r, w, h, D3DFMT_A8R8G8B8, SampleCount);
            rt_Generic_1_r = createUnique(r2_RT_generic1_r, w, h, D3DFMT_A8R8G8B8, SampleCount);
			//rt_Generic.create		      (r2_RT_generic,w,h,   D3DFMT_A8R8G8B8, 1		);
		}
		//	Igor: for volumetric lights
		//rt_Generic_2.create			(r2_RT_generic2,w,h,D3DFMT_A8R8G8B8		);
		//	temp: for higher quality blends
		if (RImplementation.o.advancedpp)
			rt_Generic_2 = createUnique(r2_RT_generic2, w, h, D3DFMT_A16B16G16R16F, SampleCount);
	}

	s_sunshafts.create(b_sunshafts, "r2\\sunshafts");
	s_blur.create(b_blur, "r2\\blur");
	s_pp_bloom.create(b_pp_bloom, "r2\\pp_bloom");
	s_dof.create(b_dof, "r2\\dof");
	s_gasmask_drops.create(b_gasmask_drops, "r2\\gasmask_drops");
	s_gasmask_dudv.create(b_gasmask_dudv, "r2\\gasmask_dudv");
	s_nightvision.create(b_nightvision, "r2\\nightvision");

	s_fakescope.create(b_fakescope, "r2\\fakescope"); //crookr

	s_heatvision.create(b_heatvision, "r2\\heatvision"); //--DSR-- HeatVision
	s_lut.create(b_lut, "r2\\lut");
	// OCCLUSION
	s_occq.create(b_occq, "r2\\occq");

	// Screen Space Shaders Stuff
	s_ssfx_fog_scattering.create(b_ssfx_fog_scattering, "ssfx_fog_scattering"); // SSS Fog Scattering
	s_ssfx_motion_blur.create(b_ssfx_motion_blur, "ssfx_motion_blur"); // SSS Motion Blur
	s_ssfx_taa.create(b_ssfx_taa, "ssfx_taa"); // SSS TAA
	s_ssfx_rain.create(b_ssfx_rain, "ssfx_rain"); // SSS Rain
	s_ssfx_bloom.create(b_ssfx_bloom, "ssfx_bloom"); // SSS Bloom
	s_ssfx_bloom_lens.create(b_ssfx_bloom_lens, "ssfx_bloom_flares"); // SSS Bloom Lens flare
	s_ssfx_bloom_downsample.create(b_ssfx_bloom_downsample, "ssfx_bloom_downsample"); // SSS Bloom
	s_ssfx_bloom_upsample.create(b_ssfx_bloom_upsample, "ssfx_bloom_upsample"); // SSS Bloom
	s_ssfx_sss_ext.create(b_ssfx_sss_ext, "ssfx_sss_ext"); // SSS Extended
	s_ssfx_sss.create(b_ssfx_sss, "ssfx_sss"); // SSS
	s_ssfx_ssr.create(b_ssfx_ssr, "ssfx_ssr"); // SSR
	s_ssfx_volumetric_blur.create(b_ssfx_volumetric_blur, "ssfx_volumetric_blur"); // Volumetric Blur
	
	s_ssfx_water_ssr.create("ssfx_water_ssr"); // Water SSR
	s_ssfx_water.create("ssfx_water"); // Water
	s_ssfx_water_blur.create(b_ssfx_water_blur, "ssfx_water_blur"); // Water

	s_ssfx_ao.create(b_ssfx_ao, "ssfx_ao"); // SSR

	// SSS 23: Deprecated
	/*string32 cskin_buffer;
	for (int skin_num = 0; skin_num < 5; skin_num++)
	{
		sprintf(cskin_buffer, "ssfx_hud_skin%i", skin_num);
		s_ssfx_hud[skin_num].create(cskin_buffer);
	}*/

	// DIRECT (spot)
	D3DFORMAT depth_format = (D3DFORMAT)RImplementation.o.HW_smap_FORMAT;

	if (RImplementation.o.HW_smap)
	{
		D3DFORMAT nullrt = D3DFMT_R5G6B5;
		if (RImplementation.o.nullrt) nullrt = (D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L');

		u32 size = RImplementation.o.smapsize;
		// the svp never rasterizes a shadow map, it reads the main atlas through the stock texture name
		const bool smap_alloc = isMain || ps_r__svp_smap_alloc != 0;
		if (smap_alloc)
			rt_smap_depth = createUnique(r2_RT_smap_depth, size, size, depth_format);

		if (RImplementation.o.dx10_minmax_sm)
		{
			if (smap_alloc)
				rt_smap_depth_minmax = createUnique(r2_RT_smap_depth_minmax, size / 4, size / 4, D3DFMT_R32F);
			CBlender_createminmax TempBlender;
			s_create_minmax_sm.create(&TempBlender, "null");
		}

		//rt_smap_surf.create			(r2_RT_smap_surf,			size,size,nullrt		);
		//rt_smap_ZB					= NULL;
		s_accum_mask.create(b_accum_mask, "r3\\accum_mask");
		s_accum_direct.create(b_accum_direct, "r3\\accum_direct");

		// IX-Ray CSM: array of the near sun cascades, created after rt_smap_depth (copies its format)
		if (RImplementation.o.sun_csm)
		{
			if (isMain)
			{
				create_smap_csm(RImplementation.o.smapsize);
				if (csm_slices())
					s_accum_csm.create(b_accum_csm);
				else
					RImplementation.o.sun_csm = FALSE;
			}
			else if (RImplementation.TargetMain && RImplementation.TargetMain->csm_slices())
			{
				// pip the SVP never renders shadow maps: it lights from the main CSM array, which the
				// accum shader binds by its stock name (dual_accum in render_sun_cascade)
				smap_csm_slices = RImplementation.TargetMain->csm_slices();
				smap_csm_size = RImplementation.TargetMain->smap_csm_size;
				s_accum_csm.create(b_accum_csm);
			}
		}

		if (RImplementation.o.dx10_msaa)
		{
			int bound = RImplementation.o.dx10_msaa_samples;

			if (RImplementation.o.dx10_msaa_opt)
				bound = 1;

			for (int i = 0; i < bound; ++i)
			{
				s_accum_direct_msaa[i].create(b_accum_direct_msaa[i], "r3\\accum_direct");
				s_accum_mask_msaa[i].create(b_accum_mask_msaa[i], "r3\\accum_direct");
			}
		}
		if (RImplementation.o.advancedpp)
		{
			s_accum_direct_volumetric.create("accum_volumetric_sun_nomsaa");

			if (RImplementation.o.dx10_minmax_sm)
				s_accum_direct_volumetric_minmax.create("accum_volumetric_sun_nomsaa_minmax");

			if (RImplementation.o.dx10_msaa)
			{
				static LPCSTR snames[] = {
					"accum_volumetric_sun_msaa0",
					"accum_volumetric_sun_msaa1",
					"accum_volumetric_sun_msaa2",
					"accum_volumetric_sun_msaa3",
					"accum_volumetric_sun_msaa4",
					"accum_volumetric_sun_msaa5",
					"accum_volumetric_sun_msaa6",
					"accum_volumetric_sun_msaa7"
				};
				int bound = RImplementation.o.dx10_msaa_samples;

				if (RImplementation.o.dx10_msaa_opt)
					bound = 1;

				for (int i = 0; i < bound; ++i)
				{
					//s_accum_direct_volumetric_msaa[i].create		(b_accum_direct_volumetric_sun_msaa[i],			"r3\\accum_direct");
					s_accum_direct_volumetric_msaa[i].create(snames[i]);
				}
			}
		}
	}
	else
	{
		//	TODO: DX10: Check if we need old-style SMap
		VERIFY(!"Use HW SMAPs only!");
		//u32	size					=RImplementation.o.smapsize	;
		//rt_smap_surf.create			(r2_RT_smap_surf,			size,size,D3DFMT_R32F);
		//rt_smap_depth				= NULL;
		//R_CHK						(HW.pDevice->CreateDepthStencilSurface	(size,size,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,TRUE,&rt_smap_ZB,NULL));
		//s_accum_mask.create			(b_accum_mask,				"r2\\accum_mask");
		//s_accum_direct.create		(b_accum_direct,			"r2\\accum_direct");
		//if (RImplementation.o.advancedpp)
		//	s_accum_direct_volumetric.create("accum_volumetric_sun");
	}

	//	RAIN
	//	TODO: DX10: Create resources only when DX10 rain is enabled.
	//	Or make DX10 rain switch dynamic?
	{
		CBlender_rain TempBlender;
		s_rain.create(&TempBlender, "null");

		if (RImplementation.o.dx10_msaa)
		{
			static LPCSTR SampleDefs[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
			CBlender_rain_msaa TempBlender[8];

			int bound = RImplementation.o.dx10_msaa_samples;

			if (RImplementation.o.dx10_msaa_opt)
				bound = 1;

			for (int i = 0; i < bound; ++i)
			{
				TempBlender[i].SetDefine("ISAMPLE", SampleDefs[i]);
				s_rain_msaa[i].create(&TempBlender[i], "null");
				s_accum_spot_msaa[i].create(b_accum_spot_msaa[i], "r2\\accum_spot_s", "lights\\lights_spot01");
				s_accum_point_msaa[i].create(b_accum_point_msaa[i], "r2\\accum_point_s");
				//s_accum_volume_msaa[i].create(b_accum_direct_volumetric_msaa[i], "lights\\lights_spot01");
				s_accum_volume_msaa[i].create(b_accum_volumetric_msaa[i], "lights\\lights_spot01");
				s_combine_msaa[i].create(b_combine_msaa[i], "r2\\combine");
			}
		}
	}

	if (RImplementation.o.dx10_msaa)
	{
		CBlender_msaa TempBlender;

		s_mark_msaa_edges.create(&TempBlender, "null");
	}

	// POINT
	{
		s_accum_point.create(b_accum_point, "r2\\accum_point_s");
		accum_point_geom_create();
		g_accum_point.create(D3DFVF_XYZ, g_accum_point_vb, g_accum_point_ib);
		accum_omnip_geom_create();
		g_accum_omnipart.create(D3DFVF_XYZ, g_accum_omnip_vb, g_accum_omnip_ib);
	}

	// SPOT
	{
		s_accum_spot.create(b_accum_spot, "r2\\accum_spot_s", "lights\\lights_spot01");
		accum_spot_geom_create();
		g_accum_spot.create(D3DFVF_XYZ, g_accum_spot_vb, g_accum_spot_ib);
	}

	{
		s_accum_volume.create("accum_volumetric", "lights\\lights_spot01");
		accum_volumetric_geom_create();
		g_accum_volumetric.create(D3DFVF_XYZ, g_accum_volumetric_vb, g_accum_volumetric_ib);
	}

	// REFLECTED
	{
		s_accum_reflected.create(b_accum_reflected, "r2\\accum_refl");
		if (RImplementation.o.dx10_msaa)
		{
			int bound = RImplementation.o.dx10_msaa_samples;

			if (RImplementation.o.dx10_msaa_opt)
				bound = 1;

			for (int i = 0; i < bound; ++i)
			{
				s_accum_reflected_msaa[i].create(b_accum_reflected_msaa[i], "null");
			}
		}
	}

	// BLOOM
	{
		D3DFORMAT fmt = D3DFMT_A8R8G8B8; //;		// D3DFMT_X8R8G8B8
		u32 w = BLOOM_size_X, h = BLOOM_size_Y;
		u32 fvf_build = D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1) |
			D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE2(3);
		u32 fvf_filter = (u32)D3DFVF_XYZRHW | D3DFVF_TEX8 | D3DFVF_TEXCOORDSIZE4(0) | D3DFVF_TEXCOORDSIZE4(1) |
			D3DFVF_TEXCOORDSIZE4(2) | D3DFVF_TEXCOORDSIZE4(3) | D3DFVF_TEXCOORDSIZE4(4) | D3DFVF_TEXCOORDSIZE4(5) |
			D3DFVF_TEXCOORDSIZE4(6) | D3DFVF_TEXCOORDSIZE4(7);
		rt_Bloom_1 = createUnique(r2_RT_bloom1, w, h, fmt);
		rt_Bloom_2 = createUnique(r2_RT_bloom2, w, h, fmt);
		g_bloom_build.create(fvf_build, RCache.Vertex.Buffer(), RCache.QuadIB);
		g_bloom_filter.create(fvf_filter, RCache.Vertex.Buffer(), RCache.QuadIB);
		s_bloom_dbg_1.create("effects\\screen_set", r2_RT_bloom1);
		s_bloom_dbg_2.create("effects\\screen_set", r2_RT_bloom2);
		s_bloom.create(b_bloom, "r2\\bloom");
		if (RImplementation.o.dx10_msaa)
		{
			s_bloom_msaa.create(b_bloom_msaa, "r2\\bloom");
			s_postprocess_msaa.create(b_postprocess_msaa, "r2\\post");
		}
		f_bloom_factor = 0.5f;
	}

	//SMAA
	{
		u32 w = dlss::RenderWidth();
		u32 h = dlss::RenderHeight();

		rt_smaa_edgetex = createUnique(r2_RT_smaa_edgetex, w, h, D3DFMT_A8R8G8B8);
		rt_smaa_blendtex = createUnique(r2_RT_smaa_blendtex, w, h, D3DFMT_A8R8G8B8);

		s_smaa.create(b_smaa, "r3\\smaa");
	}

	// TONEMAP
	{
		rt_LUM_64 = createUnique(r2_RT_luminance_t64, 64, 64, D3DFMT_A16B16G16R16F);
		rt_LUM_8 = createUnique(r2_RT_luminance_t8, 8, 8, D3DFMT_A16B16G16R16F);
		s_luminance.create(b_luminance, "r2\\luminance");
		f_luminance_adapt = 0.5f;

		t_LUM_src.create(r2_RT_luminance_src);
		t_LUM_dest.create(r2_RT_luminance_cur);

		// create pool
		for (u32 it = 0; it < 2; it++)
		{
			shared_str name; name.printf("%s_%d", r2_RT_luminance_pool, it);
			rt_LUM_pool[it] = createUnique(name.c_str(), 1, 1, D3DFMT_R32F);

			FLOAT ColorRGBA[4] = { 127.0f / 255.0f, 127.0f / 255.0f, 127.0f / 255.0f, 127.0f / 255.0f };
			HW.pContext->ClearRenderTargetView(rt_LUM_pool[it]->pRT, ColorRGBA);
		}
		u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT,NULL,NULL, HW.pBaseZB);
	}

	// HBAO
	if (RImplementation.o.ssao_opt_data)
	{
		u32 w = 0;
		u32 h = 0;
		if (RImplementation.o.ssao_half_data)
		{
			w = dlss::RenderWidth() / 2;
			h = dlss::RenderHeight() / 2;
		}
		else
		{
			w = dlss::RenderWidth();
			h = dlss::RenderHeight();
		}

		D3DFORMAT fmt = HW.Caps.id_vendor == 0x10DE ? D3DFMT_R32F : D3DFMT_R16F;
		rt_half_depth = createUnique(r2_RT_half_depth, w, h, fmt);

		s_ssao.create(b_ssao, "r2\\ssao");
	}

	//if (RImplementation.o.ssao_blur_on)
	//{
	//	u32		w = Device.dwWidth, h = Device.dwHeight;
	//	rt_ssao_temp.create			(r2_RT_ssao_temp, w, h, D3DFMT_G16R16F, SampleCount);
	//	s_ssao.create				(b_ssao, "r2\\ssao");

	//	if( RImplementation.o.dx10_msaa )
	//	{
	//		int bound = RImplementation.o.dx10_msaa_opt ? 1 : RImplementation.o.dx10_msaa_samples;

	//		for( int i = 0; i < bound; ++i )
	//		{
	//			s_ssao_msaa[i].create( b_ssao_msaa[i], "null");
	//		}
	//	}
	//}

	// IX-Ray SSLR: five full-resolution RGBA16F targets (four with UAVs) and one RGBA8
	// (~115 MB at 4K, ~29 MB at 1080p)
	if (RImplementation.o.sslr && !isMain)
	{
		// pip the SVP pass skips SSLR (phase_sslr, IsSVPFrame); combine_1 still samples the stock
		// name, so the SVP gets its own cleared target (no reflection hit) instead of the main's
		rt_sslr = createUnique(r2_RT_sslr, Width, Height, D3DFMT_A16B16G16R16F, 1, true);
		const float zero[4] = {0, 0, 0, 0};
		HW.pContext->ClearRenderTargetView(rt_sslr->pRT, zero);
	}
	else if (RImplementation.o.sslr)
	{
		// scene resolution (smaller than the display with DLSS / FSR3): copies of rt_Generic_0 /
		// rt_Position below must match them exactly
		u32 w = dlss::RenderWidth(), h = dlss::RenderHeight();
		rt_sslr = createUnique(r2_RT_sslr, w, h, D3DFMT_A16B16G16R16F, 1, true);
		rt_sslr_old.create(r2_RT_sslr_old, w, h, D3DFMT_A16B16G16R16F, 1, true);
		rt_sslr_data.create(r2_RT_sslr_data, w, h, D3DFMT_A16B16G16R16F, 1, true);
		rt_sslr_temp.create(r2_RT_sslr_temp, w, h, D3DFMT_A16B16G16R16F, 1, true);
		// Copies of rt_Generic_0 / rt_Position: CopyResource needs the exact same format and size
		// (SSLR is off with MSAA, so both sources are single-sampled here)
		rt_sslr_color.create(r2_RT_sslr_color, w, h, rt_Generic_0->fmt, 1);
		rt_sslr_pos_old.create(r2_RT_sslr_pos_old, w, h, D3DFMT_A16B16G16R16F, 1);
		// Hi-Z pyramid first: the trace's blender binds it by name (r4_sslr_hiz)
		if (RImplementation.o.sslr_hiz)
		{
			create_sslr_hiz(w, h);
			if (!t_sslr_hiz_surf)
				RImplementation.o.sslr_hiz = FALSE;	// shaders are compiled below, without USE_SSLR_HIZ
		}
		s_sslr.create(b_sslr);
	}

	// Skin subsurface scattering: an RGBA16F and an R11G11B10F target at scene resolution
	// (~18 MB at 4K, ~5 MB at 1080p). D3DFMT_V16U16 maps to DXGI_FORMAT_R11G11B10_FLOAT
	// (dx10TextureUtils.cpp).
	if (RImplementation.o.skin_sss)
	{
		// per viewport: the PiP SVP blurs its own skin light (renderSceneLighting)
		u32 w = isMain ? dlss::RenderWidth() : Width, h = isMain ? dlss::RenderHeight() : Height;
		rt_skin_sss_tmp = createUnique(r2_RT_skin_sss_tmp, w, h, D3DFMT_A16B16G16R16F, 1);
		rt_skin_sss = createUnique(r2_RT_skin_sss, w, h, D3DFMT_V16U16, 1);
		s_skin_sss.create(b_skin_sss);
	}

	// IX-Ray VSLR: two cubemaps, a depth face and the panorama (r4_rendertarget_phase_vslr.cpp)
	// pip the SVP samples the main capture (a camera-centred environment) by its stock names
	if (RImplementation.o.vslr && isMain)
	{
		create_vslr(u32(_max(64, _min(1024, ps_r4_vslr_size))));
		if (t_vslr_env_surf)
			s_vslr.create(b_vslr);
		else
			RImplementation.o.vslr = FALSE;
	}

	// HDAO
	if (RImplementation.o.ssao_hdao && RImplementation.o.ssao_ultra)
	{
		u32 w = isMain ? dlss::RenderWidth() : Width, h = isMain ? dlss::RenderHeight() : Height;
		rt_ssao_temp = createUnique(r2_RT_ssao_temp, w, h, D3DFMT_R16F, 1, true);
		s_hdao_cs.create(b_hdao_cs, "r2\\ssao");
		if (RImplementation.o.dx10_msaa)
		{
			s_hdao_cs_msaa.create(b_hdao_msaa_cs, "r2\\ssao");
		}
	}

	// COMBINE
	{
		static D3DVERTEXELEMENT9 dwDecl[] =
		{
			{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, // pos+uv
			D3DDECL_END()
		};
		s_combine.create(b_combine, "r2\\combine");
		s_combine_volumetric.create("combine_volumetric");
		s_combine_dbg_0.create("effects\\screen_set", r2_RT_smap_surf);
		s_combine_dbg_1.create("effects\\screen_set", r2_RT_luminance_t8);
		s_combine_dbg_Accumulator.create("effects\\screen_set", r2_RT_accum);
		g_combine_VP.create(dwDecl, RCache.Vertex.Buffer(), RCache.QuadIB);
		g_combine.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);
		g_combine_2UV.create(FVF::F_TL2uv, RCache.Vertex.Buffer(), RCache.QuadIB);
		g_combine_cuboid.create(dwDecl, RCache.Vertex.Buffer(), RCache.Index.Buffer());

		u32 fvf_aa_blur = D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1) |
			D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE2(3);
		g_aa_blur.create(fvf_aa_blur, RCache.Vertex.Buffer(), RCache.QuadIB);

		u32 fvf_aa_AA = D3DFVF_XYZRHW | D3DFVF_TEX7 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1) |
			D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE2(3) | D3DFVF_TEXCOORDSIZE2(4) | D3DFVF_TEXCOORDSIZE4(5) |
			D3DFVF_TEXCOORDSIZE4(6);
		g_aa_AA.create(fvf_aa_AA, RCache.Vertex.Buffer(), RCache.QuadIB);

		u32 fvf_KD = D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0);
		g_KD.create(fvf_KD, RCache.Vertex.Buffer(), RCache.QuadIB);

		t_envmap_0.create(r2_T_envs0);
		t_envmap_1.create(r2_T_envs1);
	}
	t_reticle.create("$user$reticle"); // pip per-lens reticle texture for the scope color shader
	if (isMain)
	{
		reticle_glow_create(); // 3DSS reticle glow targets and passes (r4_rendertarget_phase_3dss_reticle.cpp)
		scope_glass_compose_create(); // scope glass composition passes (svp_optics.cpp)
	}

	// Build textures
	{
		// Testure for async sreenshots
		{
			D3D_TEXTURE2D_DESC desc;
			desc.Width = Device.dwWidth;
			desc.Height = Device.dwHeight;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
			desc.Usage = D3D_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D_CPU_ACCESS_READ;
			desc.MiscFlags = 0;

			R_CHK(HW.pDevice->CreateTexture2D(&desc, 0, &t_ss_async));
		}
		// Build material(s)
		{
			//	Create immutable texture.
			//	So we need to init data _before_ the creation.
			// Surface
			//R_CHK						(D3DXCreateVolumeTexture(HW.pDevice,TEX_material_LdotN,TEX_material_LdotH,4,1,0,D3DFMT_A8L8,D3DPOOL_MANAGED,&t_material_surf));
			//t_material					= dxRenderDeviceRender::Instance().Resources->_CreateTexture(r2_material);
			//t_material->surface_set		(t_material_surf);
			//	Use DXGI_FORMAT_R8G8_UNORM

			u16 tempData[TEX_material_LdotN * TEX_material_LdotH * TEX_material_Count];

			D3D_TEXTURE3D_DESC desc;
			desc.Width = TEX_material_LdotN;
			desc.Height = TEX_material_LdotH;
			desc.Depth = TEX_material_Count;
			desc.MipLevels = 1;
			desc.Format = DXGI_FORMAT_R8G8_UNORM;
			desc.Usage = D3D_USAGE_IMMUTABLE;
			desc.BindFlags = D3D_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;

			D3D_SUBRESOURCE_DATA subData;

			subData.pSysMem = tempData;
			subData.SysMemPitch = desc.Width * 2;
			subData.SysMemSlicePitch = desc.Height * subData.SysMemPitch;

			// Fill it (addr: x=dot(L,N),y=dot(L,H))
			//D3DLOCKED_BOX				R;
			//R_CHK						(t_material_surf->LockBox	(0,&R,0,0));
			for (u32 slice = 0; slice < TEX_material_Count; slice++)
			{
				for (u32 y = 0; y < TEX_material_LdotH; y++)
				{
					for (u32 x = 0; x < TEX_material_LdotN; x++)
					{
						u16* p = (u16*)
						(LPBYTE(subData.pSysMem)
							+ slice * subData.SysMemSlicePitch
							+ y * subData.SysMemPitch + x * 2);
						float ld = float(x) / float(TEX_material_LdotN - 1);
						float ls = float(y) / float(TEX_material_LdotH - 1) + EPS_S;
						ls *= powf(ld, 1 / 32.f);
						float fd, fs;

						switch (slice)
						{
						case 0:
							{
								// looks like OrenNayar
								fd = powf(ld, 0.75f); // 0.75
								fs = powf(ls, 16.f) * .5f;
							}
							break;
						case 1:
							{
								// looks like Blinn
								fd = powf(ld, 0.90f); // 0.90
								fs = powf(ls, 24.f);
							}
							break;
						case 2:
							{
								// looks like Phong
								fd = ld; // 1.0
								fs = powf(ls * 1.01f, 128.f);
							}
							break;
						case 3:
							{
								// looks like Metal
								float s0 = _abs(1 - _abs(0.05f * _sin(33.f * ld) + ld - ls));
								float s1 = _abs(1 - _abs(0.05f * _cos(33.f * ld * ls) + ld - ls));
								float s2 = _abs(1 - _abs(ld - ls));
								fd = ld; // 1.0
								fs = powf(_max(_max(s0, s1), s2), 24.f);
								fs *= powf(ld, 1 / 7.f);
							}
							break;
						default:
							fd = fs = 0;
						}
						s32 _d = clampr(iFloor(fd * 255.5f), 0, 255);
						s32 _s = clampr(iFloor(fs * 255.5f), 0, 255);
						if ((y == (TEX_material_LdotH - 1)) && (x == (TEX_material_LdotN - 1)))
						{
							_d = 255;
							_s = 255;
						}
						*p = u16(_s * 256 + _d);
					}
				}
			}
			//R_CHK		(t_material_surf->UnlockBox	(0));

			R_CHK(HW.pDevice->CreateTexture3D(&desc, &subData, &t_material_surf));
			t_material = dxRenderDeviceRender::Instance().Resources->_CreateTexture(r2_material);
			t_material->surface_set(t_material_surf);
			//R_CHK						(D3DXCreateVolumeTexture(HW.pDevice,TEX_material_LdotN,TEX_material_LdotH,4,1,0,D3DFMT_A8L8,D3DPOOL_MANAGED,&t_material_surf));
			//t_material					= dxRenderDeviceRender::Instance().Resources->_CreateTexture(r2_material);
			//t_material->surface_set		(t_material_surf);

			// #ifdef DEBUG
			// R_CHK	(D3DXSaveTextureToFile	("x:\\r2_material.dds",D3DXIFF_DDS,t_material_surf,0));
			// #endif
		}

		// Build noise table
		if (1)
		{
			// Surfaces
			//D3DLOCKED_RECT				R[TEX_jitter_count];

			//for (int it=0; it<TEX_jitter_count; it++)
			//{
			//	string_path					name;
			//	xr_sprintf						(name,"%s%d",r2_jitter,it);
			//	R_CHK	(D3DXCreateTexture	(HW.pDevice,TEX_jitter,TEX_jitter,1,0,D3DFMT_Q8W8V8U8,D3DPOOL_MANAGED,&t_noise_surf[it]));
			//	t_noise[it]					= dxRenderDeviceRender::Instance().Resources->_CreateTexture	(name);
			//	t_noise[it]->surface_set	(t_noise_surf[it]);
			//	R_CHK						(t_noise_surf[it]->LockRect	(0,&R[it],0,0));
			//}
			//	Use DXGI_FORMAT_R8G8B8A8_SNORM

			static const int sampleSize = 4;
			u32 tempData[TEX_jitter_count][TEX_jitter * TEX_jitter];

			D3D_TEXTURE2D_DESC desc;
			desc.Width = TEX_jitter;
			desc.Height = TEX_jitter;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Format = DXGI_FORMAT_R8G8B8A8_SNORM;
			//desc.Usage = D3D_USAGE_IMMUTABLE;
			desc.Usage = D3D_USAGE_DEFAULT;
			desc.BindFlags = D3D_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;

			D3D_SUBRESOURCE_DATA subData[TEX_jitter_count];

			for (int it = 0; it < TEX_jitter_count - 1; it++)
			{
				subData[it].pSysMem = tempData[it];
				subData[it].SysMemPitch = desc.Width * sampleSize;
			}

			// Fill it,
			for (u32 y = 0; y < TEX_jitter; y++)
			{
				for (u32 x = 0; x < TEX_jitter; x++)
				{
					DWORD data [TEX_jitter_count - 1];
					generate_jitter(data, TEX_jitter_count - 1);
					for (u32 it = 0; it < TEX_jitter_count - 1; it++)
					{
						u32* p = (u32*)
						(LPBYTE(subData[it].pSysMem)
							+ y * subData[it].SysMemPitch
							+ x * 4);

						*p = data[it];
					}
				}
			}

			//for (int it=0; it<TEX_jitter_count; it++)	{
			//	R_CHK						(t_noise_surf[it]->UnlockRect(0));
			//}

			for (int it = 0; it < TEX_jitter_count - 1; it++)
			{
				string_path name;
				xr_sprintf(name, "%s%d",r2_jitter, it);
				//R_CHK	(D3DXCreateTexture	(HW.pDevice,TEX_jitter,TEX_jitter,1,0,D3DFMT_Q8W8V8U8,D3DPOOL_MANAGED,&t_noise_surf[it]));
				R_CHK(HW.pDevice->CreateTexture2D(&desc, &subData[it], &t_noise_surf[it]));
				t_noise[it] = dxRenderDeviceRender::Instance().Resources->_CreateTexture(name);
				t_noise[it]->surface_set(t_noise_surf[it]);
				//R_CHK						(t_noise_surf[it]->LockRect	(0,&R[it],0,0));
			}

			float tempDataHBAO[TEX_jitter * TEX_jitter * 4];

			// generate HBAO jitter texture (last)
			D3D_TEXTURE2D_DESC descHBAO;
			descHBAO.Width = TEX_jitter;
			descHBAO.Height = TEX_jitter;
			descHBAO.MipLevels = 1;
			descHBAO.ArraySize = 1;
			descHBAO.SampleDesc.Count = 1;
			descHBAO.SampleDesc.Quality = 0;
			descHBAO.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			//desc.Usage = D3D_USAGE_IMMUTABLE;
			descHBAO.Usage = D3D_USAGE_DEFAULT;
			descHBAO.BindFlags = D3D_BIND_SHADER_RESOURCE;
			descHBAO.CPUAccessFlags = 0;
			descHBAO.MiscFlags = 0;

			int it = TEX_jitter_count - 1;
			subData[it].pSysMem = tempDataHBAO;
			subData[it].SysMemPitch = descHBAO.Width * sampleSize * sizeof(float);

			// Fill it,
			for (u32 y = 0; y < TEX_jitter; y++)
			{
				for (u32 x = 0; x < TEX_jitter; x++)
				{
					float numDir = 1.0f;
					switch (ps_r_ssao)
					{
					case 1: numDir = 4.0f;
						break;
					case 2: numDir = 6.0f;
						break;
					case 3: numDir = 8.0f;
						break;
					case 4: numDir = 8.0f;
						break;
					}
					float angle = 2 * PI * ::Random.randF(0.0f, 1.0f) / numDir;
					float dist = ::Random.randF(0.0f, 1.0f);

					float* p = (float*)
					(LPBYTE(subData[it].pSysMem)
						+ y * subData[it].SysMemPitch
						+ x * 4 * sizeof(float));
					*p = (float)(_cos(angle));
					*(p + 1) = (float)(_sin(angle));
					*(p + 2) = (float)(dist);
					*(p + 3) = 0;
				}
			}

			string_path name;
			xr_sprintf(name, "%s%d",r2_jitter, it);
			//R_CHK	(D3DXCreateTexture	(HW.pDevice,TEX_jitter,TEX_jitter,1,0,D3DFMT_Q8W8V8U8,D3DPOOL_MANAGED,&t_noise_surf[it]));
			R_CHK(HW.pDevice->CreateTexture2D(&descHBAO, &subData[it], &t_noise_surf[it]));
			t_noise[it] = dxRenderDeviceRender::Instance().Resources->_CreateTexture(name);
			t_noise[it]->surface_set(t_noise_surf[it]);

			//	Create noise mipped
			{
				//	Autogen mipmaps
				desc.MipLevels = 0;
				R_CHK(HW.pDevice->CreateTexture2D(&desc, 0, &t_noise_surf_mipped));
				t_noise_mipped = dxRenderDeviceRender::Instance().Resources->_CreateTexture(r2_jitter_mipped);
				t_noise_mipped->surface_set(t_noise_surf_mipped);

				//	Update texture. Generate mips.

				HW.pContext->CopySubresourceRegion(t_noise_surf_mipped, 0, 0, 0, 0, t_noise_surf[0], 0, 0);

				D3DX11FilterTexture(HW.pContext, t_noise_surf_mipped, 0, D3DX10_FILTER_POINT);
			}
		}
	}

	// PP
	s_postprocess.create("postprocess");
	if (dlss::Configured())
	{
		rt_dlss_post_source.create("$user$dlss_post_source", Device.dwWidth, Device.dwHeight, D3DFMT_A16B16G16R16F);
		rt_dlss_post_color.create("$user$dlss_post_color", Device.dwWidth, Device.dwHeight, D3DFMT_A16B16G16R16F);
		t_dlss_output.create("$user$dlss_output");
		t_dlss_output->surface_set(dlss::Output());
		t_dlss_scene_depth.create("$user$dlss_scene_depth");
		t_dlss_scene_depth->surface_set(dlss::SceneDepthTexture());
		CBlender_dlss_ui_depth uiDepth;
		s_dlss_ui_depth.create(&uiDepth, "dlss_ui_depth");
		CBlender_dlss_postprocess blender;
		s_dlss_postprocess.create(&blender, "dlss_postprocess");
		CBlender_dlss_copy copy;
		s_dlss_copy.create(&copy, "dlss_copy");
	}
	g_postprocess.create(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX3, RCache.Vertex.Buffer(),
	                     RCache.QuadIB);

	// Menu
	if (dlss::Configured())
	{
		CBlender_dlss_menu menu;
		s_menu.create(&menu, "dlss_menu");
	}
	else s_menu.create("distort");
	g_menu.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);

	//
	dwWidth = Device.dwWidth;
	dwHeight = Device.dwHeight;
}

CRenderTarget::~CRenderTarget()
{
	if (this == RImplementation.TargetMain)
	{
		// the upscaler's targets belong to the main target; the PiP SVP target is torn down and
		// rebuilt on its own (resize, aim end) and must not release them
		if (t_dlss_output) t_dlss_output->surface_set(nullptr);
		if (t_dlss_scene_depth) t_dlss_scene_depth->surface_set(nullptr);
		dlss::ReleaseTargets();
		scope_vslr_destroy();
	}
	_RELEASE(t_ss_async);

	// Textures
	t_material->surface_set(NULL);

#ifdef DEBUG
	_SHOW_REF					("t_material_surf",t_material_surf);
#endif // DEBUG
	_RELEASE(t_material_surf);

	t_LUM_src->surface_set(NULL);
	t_LUM_dest->surface_set(NULL);

#ifdef DEBUG
	ID3DBaseTexture*	pSurf = 0;

	pSurf = t_envmap_0->surface_get();
	if (pSurf) pSurf->Release();
	_SHOW_REF("t_envmap_0 - #small",pSurf);

	pSurf = t_envmap_1->surface_get();
	if (pSurf) pSurf->Release();
	_SHOW_REF("t_envmap_1 - #small",pSurf);
	//_SHOW_REF("t_envmap_0 - #small",t_envmap_0->pSurface);
	//_SHOW_REF("t_envmap_1 - #small",t_envmap_1->pSurface);
#endif // DEBUG
	t_envmap_0->surface_set(NULL);
	t_envmap_1->surface_set(NULL);
	t_envmap_0.destroy();
	t_envmap_1.destroy();

	//	TODO: DX10: Check if we need old style SMAPs
	//	_RELEASE					(rt_smap_ZB);

	// Jitter
	for (int it = 0; it < TEX_jitter_count; it++)
	{
		t_noise[it]->surface_set(NULL);
#ifdef DEBUG
		_SHOW_REF("t_noise_surf[it]",t_noise_surf[it]);
#endif // DEBUG
		_RELEASE(t_noise_surf[it]);
	}

	t_noise_mipped->surface_set(NULL);
#ifdef DEBUG
	_SHOW_REF("t_noise_surf_mipped",t_noise_surf_mipped);
#endif // DEBUG
	_RELEASE(t_noise_surf_mipped);

	//
	accum_spot_geom_destroy();
	accum_omnip_geom_destroy();
	accum_point_geom_destroy();
	accum_volumetric_geom_destroy();

	// Blenders
	xr_delete(b_combine);
	xr_delete(b_luminance);
	xr_delete(b_bloom);
	xr_delete(b_accum_reflected);
	xr_delete(b_accum_spot);
	xr_delete(b_accum_point);
	destroy_smap_csm();
	xr_delete(b_accum_direct);
	xr_delete(b_accum_csm);
	xr_delete(b_ssao);

	////////////lvutner
	xr_delete(b_blur);
	xr_delete(b_dof);
	xr_delete(b_pp_bloom);
	xr_delete(b_gasmask_drops);
	xr_delete(b_gasmask_dudv);
	xr_delete(b_nightvision);
	xr_delete(b_fakescope); //crookr
	xr_delete(b_heatvision); //--DSR-- HeatVision
	xr_delete(b_lut);
	xr_delete(b_smaa);

	// [ SSS Stuff ]
	xr_delete(b_ssfx_fog_scattering); // SSS MotionBlur
	xr_delete(b_ssfx_motion_blur); // SSS MotionBlur
	xr_delete(b_ssfx_taa); // SSS TAA
	xr_delete(b_ssfx_rain); // SSS Rain
	xr_delete(b_ssfx_water_blur); // SSS Water Blur
	xr_delete(b_ssfx_bloom); // SSS Bloom
	xr_delete(b_ssfx_bloom_lens); // SSS Bloom Lens
	xr_delete(b_ssfx_bloom_downsample); // SSS Bloom Blur
	xr_delete(b_ssfx_bloom_upsample); // SSS Bloom Blur
	xr_delete(b_ssfx_sss_ext); // SSS Phase Ext
	xr_delete(b_ssfx_sss); // SSS Phase
	xr_delete(b_ssfx_ssr); // SSR Phase
	xr_delete(b_ssfx_volumetric_blur); // Volumetric Phase
	xr_delete(b_ssfx_ao); // AO Phase

	if (RImplementation.o.dx10_msaa)
	{
		int bound = RImplementation.o.dx10_msaa_samples;

		if (RImplementation.o.dx10_msaa_opt)
			bound = 1;

		for (int i = 0; i < bound; ++i)
		{
			xr_delete(b_combine_msaa[i]);
			xr_delete(b_accum_direct_msaa[i]);
			xr_delete(b_accum_mask_msaa[i]);
			xr_delete(b_accum_direct_volumetric_msaa[i]);
			//xr_delete					(b_accum_direct_volumetric_sun_msaa[i]);
			xr_delete(b_accum_spot_msaa[i]);
			xr_delete(b_accum_volumetric_msaa[i]);
			xr_delete(b_accum_point_msaa[i]);
			xr_delete(b_accum_reflected_msaa[i]);
			xr_delete(b_ssao_msaa[i]);
		}
		xr_delete(b_postprocess_msaa);
		xr_delete(b_bloom_msaa);
	}
	xr_delete(b_accum_mask);
	xr_delete(b_occq);
	xr_delete(b_sunshafts);

	xr_delete(b_hdao_cs);
	destroy_sslr_hiz();
	xr_delete(b_sslr);
	release_skin_sss_timer();
	xr_delete(b_skin_sss);
	xr_delete(b_upscaler_reactive);
	destroy_vslr();
	xr_delete(b_vslr);
	if (RImplementation.o.dx10_msaa)
	{
		xr_delete(b_hdao_msaa_cs);
	}
}

void CRenderTarget::reset_light_marker(bool bResetStencil)
{
    reset_light_marker(dwLightMarkerID, bResetStencil);
}

void CRenderTarget::reset_light_marker(u32& marker, bool bResetStencil)
{
    marker = 5;
	if (bResetStencil)
	{
		u32 Offset;
		float _w = float(RenderScreenWidth());
		float _h = float(RenderScreenHeight());
		u32 C = color_rgba(255, 255, 255, 255);
		float eps = 0;
		float _dw = 0.5f;
		float _dh = 0.5f;
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(-_dw, _h - _dh, eps, 1.f, C, 0, 0);
		pv++;
		pv->set(-_dw, -_dh, eps, 1.f, C, 0, 0);
		pv++;
		pv->set(_w - _dw, _h - _dh, eps, 1.f, C, 0, 0);
		pv++;
		pv->set(_w - _dw, -_dh, eps, 1.f, C, 0, 0);
		pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);
		RCache.set_Element(s_occq->E[2]);
		RCache.set_Geometry(g_combine);
		// Recording context (qkff99 MT light accumulation): g_combine names the owner's dynamic
		// stream; the quad above went into this context's own stream, so bind that one
		if (RenderContextId())
			RCache.set_Vertices(RCache.Vertex.Buffer(), g_combine->vb_stride);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}
}

void CRenderTarget::increment_light_marker()
{
    increment_light_marker(dwLightMarkerID);
}

void CRenderTarget::increment_light_marker(u32& marker)
{
    marker += 2;

	//if (dwLightMarkerID>10)
	const u32 iMaxMarkerValue = RImplementation.o.dx10_msaa ? 127 : 255;

	if (marker > iMaxMarkerValue)
        reset_light_marker(marker, true);
}

bool CRenderTarget::need_to_render_sunshafts()
{
	if (! (RImplementation.o.advancedpp && ps_sunshafts_mode))
		return false;

	{
		CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
		float fValue = E.m_fSunShaftsIntensity;
		//	TODO: add multiplication by sun color here
		if (fValue < 0.0001) return false;
	}

	return true;
}

bool CRenderTarget::use_minmax_sm_this_frame()
{
	switch (RImplementation.o.dx10_minmax_sm)
	{
	case CRender::MMSM_ON:
		return true;
	case CRender::MMSM_AUTO:
		return need_to_render_sunshafts();
	case CRender::MMSM_AUTODETECT:
		{
			u32 dwScreenArea =
				HW.m_ChainDesc.Width *
				HW.m_ChainDesc.Height;

			if ((dwScreenArea >= RImplementation.o.dx10_minmax_sm_screenarea_threshold))
				return need_to_render_sunshafts();
			else
				return false;
		}

	default:
		return false;
	}
}

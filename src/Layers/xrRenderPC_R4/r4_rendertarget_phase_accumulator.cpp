#include "stdafx.h"
#include "RenderContext.h"

void CRenderTarget::bind_recorded_accumulator()
{
    R_ASSERT(RenderContextId());
    const auto& target = RImplementation.o.fp16_blend ? rt_Accumulator : rt_Accumulator_temp;
    RCache.set_RT(target->pRT, 0);
    for (u32 i = 1; i < 4; ++i) RCache.set_RT(nullptr, i);
    RCache.set_ZB(RImplementation.o.dx10_msaa ? rt_MSAADepth->pZRT : dlss::SceneDepth());
    const D3D11_VIEWPORT viewport{0.f, 0.f, float(target->dwWidth), float(target->dwHeight), 0.f, 1.f};
    RenderDeviceContext()->RSSetViewports(1, &viewport);
    // Detach the shadow DSV before SRVSManager applies the atlas read binding.
    auto* color = target->pRT;
    RenderDeviceContext()->OMSetRenderTargets(1, &color,
        RImplementation.o.dx10_msaa ? rt_MSAADepth->pZRT : dlss::SceneDepth());
    // No shared target dimensions, clear marks or light-marker state are changed.
}

bool CRenderTarget::prepare_light_accumulation(RecordedLight& result, light& source, bool point)
{
    auto& renderer = RImplementation;
    auto& target = *this;
    if (!renderer.o.fp16_blend || renderer.o.nvstencil || renderer.o.nvdbt || source.flags.bHudMode ||
        ps_r2_ls_flags.test(R2FLAG_GI) ||
        (source.flags.bVolumetric && renderer.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS))) return false;
    const bool omni = point || source.flags.type == IRender_Light::OMNIPART;
    auto shader = omni ? source.s_point : source.s_spot;
    auto* msaa = omni ? source.s_point_msaa : source.s_spot_msaa;
    if (!shader)
    {
        shader = omni ? target.s_accum_point : target.s_accum_spot;
        msaa = omni ? target.s_accum_point_msaa : target.s_accum_spot_msaa;
    }
    const u32 element = source.flags.bShadow ?
        (source.X.S.size == renderer.o.smapsize ? SE_L_FULLSIZE : SE_L_NORMAL) : SE_L_UNSHADOWED;
    result.passes.clear();
    const auto accept = [&result](const ref_selement& element)
    {
        if (!element || element->passes.empty() || !ParallelRenderPass(*element->passes[0])) return false;
        result.passes.push_back(element->passes[0]._get());
        return true;
    };
    if (!shader || !accept(target.s_accum_mask->E[point ? SE_MASK_POINT : SE_MASK_SPOT]) ||
        !accept(target.s_occq->E[2]) || !accept(shader->E[element])) return false;
    const u32 samples = renderer.o.dx10_msaa ? (renderer.o.dx10_msaa_opt ? 1 : renderer.o.dx10_msaa_samples) : 0;
    for (u32 i = 0; i < samples; ++i)
        if (!msaa[i] || !accept(msaa[i]->E[element])) return false;
    result.flags.type = source.flags.type;
    result.flags.bShadow = source.flags.bShadow;
    result.m_xform = source.m_xform;
    result.position = source.position;
    result.color = source.color;
    result.range = source.range;
    result.lod = point ? 1.f : source.get_LOD();
    result.sss_id = source.sss_id;
    // Point lights do not use the shadow projection union.
    if (!point)
    {
        result.X.S.view = source.X.S.view;
        result.X.S.project = source.X.S.project;
        result.X.S.size = source.X.S.size;
        result.X.S.posX = source.X.S.posX;
        result.X.S.posY = source.X.S.posY;
    }
    (omni ? result.s_point : result.s_spot) = shader;
    for (u32 i = 0; i < samples; ++i) (omni ? result.s_point_msaa[i] : result.s_spot_msaa[i]) = msaa[i];
    return true;
}

void CRenderTarget::phase_accumulator()
{
	// Targets
	if (dwAccumulatorClearMark == Device.dwFrame)
	{
		// normal operation - setup
		if (!RImplementation.o.dx10_msaa)
		{
			if (RImplementation.o.fp16_blend) u_setrt(rt_Accumulator, NULL,NULL, dlss::SceneDepth());
			else u_setrt(rt_Accumulator_temp, NULL,NULL, dlss::SceneDepth());
		}
		else
		{
			if (RImplementation.o.fp16_blend) u_setrt(rt_Accumulator, NULL,NULL, rt_MSAADepth->pZRT);
			else u_setrt(rt_Accumulator_temp, NULL,NULL, rt_MSAADepth->pZRT);
		}
	}
	else
	{
		// initial setup
		dwAccumulatorClearMark = Device.dwFrame;

		// clear
		if (!RImplementation.o.dx10_msaa)
			u_setrt(rt_Accumulator, NULL,NULL, dlss::SceneDepth());
		else
			u_setrt(rt_Accumulator, NULL,NULL, rt_MSAADepth->pZRT);
		//dwLightMarkerID						= 5;					// start from 5, increment in 2 units
		reset_light_marker();
		//	Igor: AMD bug workaround. Should be fixed in 8.7 catalyst
		//	Need for MSAA to work correctly.
		if (RImplementation.o.dx10_msaa)
		{
			HW.pContext->OMSetRenderTargets(1, &(rt_Accumulator->pRT), 0);
		}
		//		u32		clr4clear					= color_rgba(0,0,0,0);	// 0x00
		//CHK_DX	(HW.pDevice->Clear			( 0L, NULL, D3DCLEAR_TARGET, clr4clear, 1.0f, 0L));
		FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		HW.pContext->ClearRenderTargetView(rt_Accumulator->pRT, ColorRGBA);

		//	render this after sun to avoid troubles with sun
		/*
		// Render emissive geometry, stencil - write 0x0 at pixel pos
		RCache.set_xform_project					(Device.mProject);
		RCache.set_xform_view						(Device.mView);
		// Stencil - write 0x1 at pixel pos -
		RCache.set_Stencil							( TRUE,D3DCMP_ALWAYS,0x01,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		//RCache.set_Stencil						(TRUE,D3DCMP_ALWAYS,0x00,0xff,0xff,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,D3DSTENCILOP_KEEP);
		RCache.set_CullMode							(CULL_CCW);
		RCache.set_ColorWriteEnable					();
		RImplementation.r_dsgraph_render_emissive	();
		*/
		// Stencil	- draw only where stencil >= 0x1
		RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_ColorWriteEnable();
	}

	//	Restore viewport after shadow map rendering
	RImplementation.rmNormal();
}

void CRenderTarget::phase_vol_accumulator()
{
	if (RImplementation.o.ssfx_volumetric)
	{
		// SSS does not require the stencil. ( This also fix the MSAA bug )
		if (!m_bHasActiveVolumetric)
		{
			m_bHasActiveVolumetric = true;

			FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			HW.pContext->ClearRenderTargetView(rt_Generic_2->pRT, ColorRGBA);
		}

		u_setrt(rt_Generic_2, NULL, NULL, NULL);
	}
	else
	{
		// TODO: there is a bug in this function when MSAA is enabled, if you run with the D3D debug layer enabled, you'll get this error:
		// ID3D11DeviceContext::OMSetRenderTargets: The RenderTargetView at slot 0 is not compatible with the DepthStencilView. DepthStencilViews may only be used with RenderTargetViews if the effective dimensions of the Views are equal, as well as the Resource types, multisample count, and multisample quality. The RenderTargetView at slot 0 has (w:2560,h:1440,as:1), while the Resource is a Texture2D with (mc:4,mq:4294967295). The DepthStencilView has (w:2560,h:1440,as:1), while the Resource is a Texture2D with (mc:1,mq:0).
		// this seems to be because the pBaseZB target is always (SampleCount, SampleQuality) = (1, 0) (see dx10HW.cpp) but the
		// `rt_Generic_2` render target is a multisampled rendertarget
		// Not sure how to fix it correctly, if it doesn't need a depth buffer then just pass NULL, if it does
		// we probably need to create a multisampled depth target (e.g. `rt_Generic_2_zb`)
		//
		// The best way I've found to see this happen is to launch the DX11 exe with `--dxgi-dbg` to enable the debug layer,
		// then launch the game in RenderDoc with `Enable API Validation` and `Collect Callstacks` enabled, take a capture
		// then go to `Tools > Load Symbols` to load the callstack symbols. In the bottom left of the main RenderDoc window
		// there's a little status message that you can click on to see all the debug layer errors/warnings that lets you jump
		// directly to the render event where the error occurred.
		//
		// This will let you go to the precise render event that caused the debug layer to log an error, and let you see the
		// callstack where the Render() call occurred
		if (!m_bHasActiveVolumetric)
		{
			m_bHasActiveVolumetric = true;
			if (!RImplementation.o.dx10_msaa)
				u_setrt(rt_Generic_2, NULL, NULL, dlss::SceneDepth());
			else
				u_setrt(rt_Generic_2, NULL, NULL, RImplementation.Target->rt_MSAADepth->pZRT);
			//u32		clr4clearVol				= color_rgba(0,0,0,0);	// 0x00
			//CHK_DX	(HW.pDevice->Clear			( 0L, NULL, D3DCLEAR_TARGET, clr4clearVol, 1.0f, 0L));
			FLOAT ColorRGBA[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			HW.pContext->ClearRenderTargetView(rt_Generic_2->pRT, ColorRGBA);
		}
		else
		{
			if (!RImplementation.o.dx10_msaa)
				u_setrt(rt_Generic_2, NULL, NULL, dlss::SceneDepth());
			else
				u_setrt(rt_Generic_2, NULL, NULL, RImplementation.Target->rt_MSAADepth->pZRT);
		}
	}

	RCache.set_Stencil(FALSE);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_ColorWriteEnable();
}

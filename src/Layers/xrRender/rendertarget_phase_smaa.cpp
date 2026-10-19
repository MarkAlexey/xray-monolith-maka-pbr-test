#include "stdafx.h"

void CRenderTarget::phase_smaa()
{
	//Constants
	u32 C = color_rgba(0, 0, 0, 255);
	FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};

	//////////////////////////////////////////////////////////////////////////
	//Edge detection
	u_setrt(rt_smaa_edgetex, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x1, 0, 0, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
#if defined(USE_DX10) || defined(USE_DX11)	
	HW.pContext->ClearRenderTargetView(rt_smaa_edgetex->pRT, ColorRGBA);
#else
	CHK_DX( HW.pDevice->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L) );
#endif

	// Draw COLOR
	RCache.set_Element(s_smaa->E[0]);
	draw_fullscreen_quad(C);

	//////////////////////////////////////////////////////////////////////////
	//Blending
	u_setrt(rt_smaa_blendtex, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x1, 0, 0, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
#if defined(USE_DX10) || defined(USE_DX11)	
	HW.pContext->ClearRenderTargetView(rt_smaa_blendtex->pRT, ColorRGBA);
#else
	CHK_DX( HW.pDevice->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0L) );	
#endif

	// Draw COLOR
	RCache.set_Element(s_smaa->E[1]);
	draw_fullscreen_quad(C);

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif	

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Draw COLOR
	RCache.set_Element(s_smaa->E[2]);
	draw_fullscreen_quad(C);

#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
}

#if defined(USE_DX11)

void CRenderTarget::phase_ssfx_taa()
{

	u32 C = color_rgba(255, 255, 255, 255);

	// Temp motion vectors
	u_setrt(rt_ssfx_accum, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Draw COLOR
	RCache.set_Element(s_ssfx_taa->E[0]);

	draw_fullscreen_quad(C);
	
	HW.pContext->CopyResource(rt_ssfx_taa->pTexture->surface_get(), rt_ssfx_accum->pTexture->surface_get());

	// TAA
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

	u_setrt(dest_rt, nullptr, nullptr, nullptr); // rt_ssfx_taa
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Draw COLOR
	RCache.set_Element(s_ssfx_taa->E[1]);
	draw_fullscreen_quad(C);

	// Accumulate
	HW.pContext->CopyResource(rt_ssfx_prev_frame->pTexture->surface_get(), dest_rt->pTexture->surface_get());

	// Sharpening phase
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Draw COLOR
	RCache.set_Element(s_ssfx_taa->E[2]);
	
	RCache.set_c("taa_setup", ps_ssfx_taa);

	draw_fullscreen_quad(C);
}

#endif

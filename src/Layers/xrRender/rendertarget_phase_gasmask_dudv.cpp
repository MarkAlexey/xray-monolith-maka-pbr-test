#include "stdafx.h"

void CRenderTarget::phase_gasmask_dudv()
{
	//Constants
	u32 C = color_rgba(0, 0, 0, 255);

	float w = float(Device.dwWidth);

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

	//Set pass
	RCache.set_Element(s_gasmask_dudv->E[0]);

	//Set paramterers
	RCache.set_c("mask_control", ps_r2_mask_control.x, ps_r2_mask_control.y, ps_r2_mask_control.z, ps_r2_mask_control.w);

	//Set geometry
	draw_fullscreen_quad(C);
	
#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
};

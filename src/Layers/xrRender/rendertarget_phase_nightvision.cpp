#include "stdafx.h"

void CRenderTarget::phase_nightvision()
{
	//Constants
	u32 C = color_rgba(0, 0, 0, 255);

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX10) || defined(USE_DX11)
#if defined(USE_DX11)
    ref_rt& dest_rt = PostprocessColor();
#else
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
#endif
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif		

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Set pass
	RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);

	//Set geometry
	draw_fullscreen_quad(C);
	
#if defined(USE_DX10) || defined(USE_DX11)
#if defined(USE_DX11)
    HW.pContext->CopyResource(PostprocessSource()->pSurface, dest_rt->pSurface);
#else
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
#endif
};

//crookr
void CRenderTarget::phase_fakescope()
{
	//Constants
	u32 C = color_rgba(0, 0, 0, 255);

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX10) || defined(USE_DX11)
#if defined(USE_DX11)
    ref_rt& dest_rt = PostprocessColor();
#else
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
#endif
	u_setrt(dest_rt, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Set pass
	RCache.set_Element(s_fakescope->E[ps_r2_nightvision]);

	//Set geometry
	draw_fullscreen_quad(C);

#if defined(USE_DX11)
    HW.pContext->CopyResource(PostprocessSource()->pSurface, dest_rt->pSurface);
#else
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
#else
	//Main pass (we avoid write-read from the same buffer)
	u_setrt(rt_Generic_PingPong, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Set pass
	RCache.set_Element(s_fakescope->E[0]);

	//Set geometry
	draw_fullscreen_quad(C);

	//Draw to rt_Generic_0
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Set pass
	RCache.set_Element(s_fakescope->E[1]);

	//Set geometry
	draw_fullscreen_quad(C);
#endif
};

//--DSR-- HeatVision_start
void CRenderTarget::phase_heatvision()
{
	//Constants
	u32 C = color_rgba(0, 0, 0, 255);

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
#if defined(USE_DX10) || defined(USE_DX11)
#if defined(USE_DX11)
    ref_rt& dest_rt = PostprocessColor();
#else
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
#endif
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif		

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Set pass
	RCache.set_Element(s_heatvision->E[ps_r2_heatvision]);

	//Set geometry
	draw_fullscreen_quad(C);

#if defined(USE_DX10) || defined(USE_DX11)
#if defined(USE_DX11)
    HW.pContext->CopyResource(PostprocessSource()->pSurface, dest_rt->pSurface);
#else
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
#endif
};
//--DSR-- HeatVision_start

#if defined(USE_DX11)	//  Redotix99: for 3D Shader Based Scopes 		(sorry for using the nightvision phase file)
void CRenderTarget::phase_3DSSReticle()
{
	HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), RImplementation.Target->rt_Position->pTexture->surface_get());

	HW.pContext->CopyResource(rt_Generic_temp->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());

	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);

	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	RImplementation.render_Reticle();
};
#endif

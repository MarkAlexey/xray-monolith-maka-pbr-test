#include "stdafx.h"

void CRenderTarget::phase_dof()
{
	//Constants
	u32 C = color_rgba(0, 0, 0, 255);

	//DoF vectors
	Fvector2 vDofKernel;
	vDofKernel.set(0.5f / Device.dwWidth, 0.5f / Device.dwHeight);
	vDofKernel.mul(ps_r2_dof_kernel_size);
	Fvector3 dof;
	g_pGamePersistent->GetCurrentDof(dof);

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
	u_setrt(rt_dof, 0, 0, HW.pBaseZB);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Set pass
	RCache.set_Element(s_dof->E[0]);

	//Set paramterers
	//RCache.set_c("taa_params", ps_taa_params.x, ps_taa_params.y, 0, 0);
	RCache.set_c("dof_params", dof.x, dof.y, dof.z, ps_r2_dof_sky);	
	RCache.set_c("dof_kernel", vDofKernel.x, vDofKernel.y, ps_r2_dof_kernel_size, 0);
	
	//Set geometry
	draw_fullscreen_quad(C);
	////////////////////////////////////////////////////////////////////////////
#if defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif		

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Set pass
	RCache.set_Element(s_dof->E[1]);

	//Set geometry
	draw_fullscreen_quad(C);

	//Resolve RT
#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
};

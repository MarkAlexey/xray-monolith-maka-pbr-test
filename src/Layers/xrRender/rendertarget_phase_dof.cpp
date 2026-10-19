#include "stdafx.h"
#include "RenderDimensions.h"

void CRenderTarget::phase_dof()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(RenderScreenWidth());
	float h = float(RenderScreenHeight());

	Fvector2 p0, p1;
#if defined(USE_DX10) || defined(USE_DX11)	
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
#else
	p0.set(0.5f / w, 0.5f / h);
	p1.set((w + 0.5f) / w, (h + 0.5f) / h);
#endif

	//DoF vectors
	Fvector2 vDofKernel;
	vDofKernel.set(0.5f / RenderScreenWidth(), 0.5f / RenderScreenHeight());
	vDofKernel.mul(ps_r2_dof_kernel_size);
	Fvector3 dof;
	g_pGamePersistent->GetCurrentDof(dof);

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
	u_setrt(rt_dof, 0, 0, HW.pBaseZB);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	quad_fill(0, 0, float(w), float(h), p0.x, p0.y, p1.x, p1.y, d_Z, d_W, C, Offset);

	//Set pass
	RCache.set_Element(s_dof->E[0]);

	//Set paramterers
	//RCache.set_c("taa_params", ps_taa_params.x, ps_taa_params.y, 0, 0);
	RCache.set_c("dof_params", dof.x, dof.y, dof.z, ps_r2_dof_sky);	
	RCache.set_c("dof_kernel", vDofKernel.x, vDofKernel.y, ps_r2_dof_kernel_size, 0);
	
	//Set geometry
	RCache.set_Geometry(quad_geom());
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	////////////////////////////////////////////////////////////////////////////
#if defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif		

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	quad_fill(0, 0, float(w), float(h), p0.x, p0.y, p1.x, p1.y, d_Z, d_W, C, Offset);

	//Set pass
	RCache.set_Element(s_dof->E[1]);

	//Set geometry
	RCache.set_Geometry(quad_geom());
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//Resolve RT
#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
};

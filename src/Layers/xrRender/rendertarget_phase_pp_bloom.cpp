#include "stdafx.h"

extern Fvector4 ps_pp_bloom_thresh;
extern Fvector4 ps_pp_bloom_weight;

void CRenderTarget::phase_pp_bloom()
{
	//Get common data
	u32 C = color_rgba(0, 0, 0, 255);

	//Full resolution
	float w = float(Device.dwWidth);

///////////////////////////////////////////////////////////////////////////////////
////Bloom pass
///////////////////////////////////////////////////////////////////////////////////
	u_setrt(rt_pp_bloom, 0, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Draw COLOR
	RCache.set_Element(s_pp_bloom->E[0]);
	RCache.set_c("pp_bloom_thresh", ps_pp_bloom_thresh.x, ps_pp_bloom_thresh.y, ps_pp_bloom_thresh.z, ps_pp_bloom_thresh.w ); //Change to commands
	RCache.set_c("pp_bloom_weight", ps_pp_bloom_weight.x, ps_pp_bloom_weight.y, ps_pp_bloom_weight.z, ps_pp_bloom_weight.w); //Change to commands blyad
	draw_fullscreen_quad(C);
};
#include "stdafx.h"

void CRenderTarget::phase_rain()
{
	if (!RImplementation.o.dx10_msaa)
		u_setrt(rt_Color,NULL,NULL, dlss::SceneDepth());
	else
		u_setrt(rt_Color,NULL,NULL, rt_MSAADepth->pZRT);
	//u_setrt	(rt_Normal,NULL,NULL,HW.pBaseZB);
	RImplementation.rmNormal();
}

void CRenderTarget::phase_ssfx_rain()
{
	//Constants
	u32 C = color_rgba(0, 0, 0, 255);

	float w = float(RenderScreenWidth());
	float h = float(RenderScreenHeight());

	set_viewport_size(HW.pContext, w / 8.0f, h / 8.0f);

	u_setrt(rt_ssfx_rain, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Draw COLOR
	RCache.set_Element(!RImplementation.o.dx10_msaa ? s_ssfx_rain->E[0] : s_ssfx_rain->E[1]);
	draw_fullscreen_quad(C);

	set_viewport_size(HW.pContext, w, h);
}

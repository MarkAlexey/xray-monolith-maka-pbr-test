#include "stdafx.h"
#include "blender_upscaler_reactive.h"

// Monolith: reactive mask for DLSS / FSR 3. Texture names must match
// gamedata\shaders\r3\upscaler_reactive.ps / upscaler_reactive_debug.ps.

void CBlender_upscaler_reactive::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // rt_Generic_0 now vs. its copy before rain / forward rendering -> rt_upscaler_reactive
		C.r_Pass("stub_screen_space", "upscaler_reactive", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_opaque", r2_RT_upscaler_opaque);
		C.r_dx10Sampler("smp_nofilter");
		C.r_End();
		break;

	case 1: // debug: the mask blended over rt_Generic_0 in magenta
		C.r_Pass("stub_screen_space", "upscaler_reactive_debug", FALSE, FALSE, FALSE, TRUE, D3DBLEND_SRCALPHA,
			D3DBLEND_INVSRCALPHA);
		C.r_dx10Texture("s_reactive", r2_RT_upscaler_reactive);
		C.r_dx10Sampler("smp_nofilter");
		C.r_End();
		break;
	}
}

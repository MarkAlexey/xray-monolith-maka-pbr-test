#include "stdafx.h"
#include "blender_skin_sss.h"

// Monolith: skin subsurface scattering blur. Texture names must match
// gamedata\shaders\r3\skin_sss_blur.h. Both passes read the material target (subsurface value).

void CBlender_skin_sss::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // horizontal: skin irradiance from the light accumulator -> rt_skin_sss_tmp
		C.r_Pass("stub_screen_space", "skin_sss_blur_h", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_accumulator", r2_RT_accum);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_surface", r2_RT_surface);
		C.r_dx10Sampler("smp_nofilter");
		C.r_End();
		break;

	case 1: // vertical: rt_skin_sss_tmp -> rt_skin_sss (read by combine_1 as s_skin_sss)
		C.r_Pass("stub_screen_space", "skin_sss_blur_v", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_skin_sss_tmp", r2_RT_skin_sss_tmp);
		C.r_dx10Texture("s_surface", r2_RT_surface);
		C.r_dx10Sampler("smp_nofilter");
		C.r_End();
		break;
	}
}

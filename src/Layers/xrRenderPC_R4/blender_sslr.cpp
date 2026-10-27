#include "stdafx.h"
#include "blender_sslr.h"

// Monolith: IX-Ray SSLR blender. Texture names must match gamedata\shaders\r3\sslr_common.h.
// s_image / s_refl change meaning per pass (see each .cs file header).

CBlender_sslr::CBlender_sslr() { description.CLS = 0; }
CBlender_sslr::~CBlender_sslr() {}

static void sslr_gbuffer(CBlender_Compile& C)
{
	C.r_dx10Texture("s_position", r2_RT_P);
	C.r_dx10Texture("s_surface", r2_RT_surface);
	C.r_dx10Texture("s_diffuse", r2_RT_albedo);
	C.r_dx10Sampler("smp_nofilter");
	C.r_dx10Sampler("smp_rtlinear");
}

void CBlender_sslr::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // trace: last main-view frame colour -> rt_sslr (colour, confidence) + rt_sslr_data (hit, roughness)
		C.r_ComputePass("sslr_render");
		sslr_gbuffer(C);
		C.r_dx10Texture("s_image", r2_RT_sslr_color);
		C.r_dx10Texture("s_position_prev", r2_RT_sslr_pos_old); // was the hit visible there last frame
		C.r_dx10Texture("s_tonemap", r2_RT_luminance_cur); // exposure, to undo combine_1's tonemap
		if (RImplementation.o.sslr_hiz)
			C.r_dx10Texture("s_hiz", r2_RT_sslr_hiz); // min-depth pyramid (Hi-Z trace)
		C.r_End();
		break;

	case 1: // spatial resolve: rt_sslr + rt_sslr_data -> rt_sslr_temp
		C.r_ComputePass("sslr_filter");
		sslr_gbuffer(C);
		C.r_dx10Texture("s_image", r2_RT_sslr);
		C.r_dx10Texture("s_refl", r2_RT_sslr_data);
		C.r_End();
		break;

	case 2: // temporal: rt_sslr_temp + history rt_sslr_old -> rt_sslr
		C.r_ComputePass("sslr_temporal");
		sslr_gbuffer(C);
		C.r_dx10Texture("s_image", r2_RT_sslr_temp);
		C.r_dx10Texture("s_refl", r2_RT_sslr_old);
		C.r_dx10Texture("s_hit", r2_RT_sslr_data); // hit distance, for mirror reprojection
		C.r_dx10Texture("s_position_prev", r2_RT_sslr_pos_old); // disocclusion test
		C.r_End();
		break;

	case 3: // Hi-Z pyramid level 0: s_position -> -1 / view depth (only with r4_sslr_hiz)
		if (!RImplementation.o.sslr_hiz)
			break;
		C.r_ComputePass("sslr_hiz_copy");
		sslr_gbuffer(C);
		C.r_End();
		break;

	case 4: // Hi-Z pyramid level n from level n - 1 (UAVs only)
		if (!RImplementation.o.sslr_hiz)
			break;
		C.r_ComputePass("sslr_hiz_reduce");
		C.r_End();
		break;
	}
}

void CBlender_vslr::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // cubes -> panorama mip 0 (the mips are generated afterwards)
		C.r_ComputePass("vslr_panorama");
		C.r_dx10Texture("s_vslr_color", r2_RT_vslr_color);
		C.r_dx10Texture("s_vslr_dist", r2_RT_vslr_dist);
		C.r_dx10Sampler("smp_linear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_End();
		break;
	}
}

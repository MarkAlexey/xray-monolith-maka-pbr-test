#include "stdafx.h"
#include "blender_accum_csm.h"

CBlender_accum_csm::CBlender_accum_csm() { description.CLS = 0; }
CBlender_accum_csm::~CBlender_accum_csm() {}

void CBlender_accum_csm::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:
		// Same blend as the legacy sun passes (no blend: the sun is the first light written,
		// cascades never overlap thanks to the stencil). No Z test: cascade selection is done
		// per pixel in the shader, pixels outside the array cascades are discarded.
		C.r_Pass("accum_sun_csm", "accum_sun_csm", false, FALSE, FALSE, FALSE, D3DBLEND_ONE, D3DBLEND_ZERO);
		C.r_CullMode(D3DCULL_NONE);

		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_surface", r2_RT_surface); // IX-Ray PBR
		C.r_dx10Texture("s_diffuse", r2_RT_albedo);
		C.r_dx10Texture("s_material", r2_material);
		C.r_dx10Texture("s_accumulator", r2_RT_accum);
		C.r_dx10Texture("s_lmap", r2_sunmask);
		C.r_dx10Texture("s_smap_csm", r2_RT_smap_csm);
		C.r_dx10Texture("s_ssfx_sss", r2_RT_ssfx_sss); // SSS screen-space shadows, if installed

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_material");
		C.r_dx10Sampler("smp_linear");
		jitter(C);
		C.r_dx10Sampler("smp_smap");

		C.r_End();
		break;
	}
}

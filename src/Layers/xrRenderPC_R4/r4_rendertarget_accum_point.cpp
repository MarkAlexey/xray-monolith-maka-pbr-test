#include "stdafx.h"

template<bool Recorded, class Light>
void CRenderTarget::accum_point_impl(Light* L, u32& marker)
{
	PROF_EVENT("CRenderTarget::accum_point");
    if constexpr (!Recorded)
    {
        phase_accumulator();
        ++RImplementation.stats.l_visible;
    }

	auto* shader = L->s_point._get();
	const ref_shader* shader_msaa = L->s_point_msaa;
	if (!shader)
	{
		shader = s_accum_point._get();
		shader_msaa = s_accum_point_msaa;
	}

	// Common
	Fvector L_pos;
	float L_spec;
	//float		L_R					= L->range;
	float L_R = L->range * .95f;
	Fvector L_clr;
	L_clr.set(L->color.r, L->color.g, L->color.b);
	L_spec = u_diffuse2s(L_clr);
	RenderCameraView().transform_tiny(L_pos, L->position);

	// Xforms
	RCache.set_xform_world(L->m_xform);
	RCache.set_xform_view(RenderCameraView());
	RCache.set_xform_project(RenderCameraProjection());
    if constexpr (!Recorded)
    {
        enable_scissor(L);
        enable_dbt_bounds(L);
    }

	// *****************************	Mask by stencil		*************************************
	// *** similar to "Carmack's reverse", but assumes convex, non intersecting objects,
	// *** thus can cope without stencil clear with 127 lights
	// *** in practice, 'cause we "clear" it back to 0x1 it usually allows us to > 200 lights :)
	RCache.set_Element(s_accum_mask->E[SE_MASK_POINT]); // masker
	//	Done in blender!
	//RCache.set_ColorWriteEnable		(FALSE);

	// backfaces: if (1<=stencil && zfail)	stencil = light_id
	RCache.set_CullMode(CULL_CW);
	if (! RImplementation.o.dx10_msaa)
		RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, marker, 0x01, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP,
		                   D3DSTENCILOP_REPLACE);
	else
		RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, marker, 0x01, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP,
		                   D3DSTENCILOP_REPLACE);
	draw_volume(L->flags.type);

	// frontfaces: if (1<=stencil && zfail)	stencil = 0x1
	RCache.set_CullMode(CULL_CCW);
	if (! RImplementation.o.dx10_msaa)
		RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP,
		                   D3DSTENCILOP_REPLACE);
	else
		RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0x7f, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_KEEP,
		                   D3DSTENCILOP_REPLACE);
	draw_volume(L->flags.type);

	// nv-stencil recompression
	if constexpr (!Recorded)
        if (RImplementation.o.nvstencil) u_stencil_optimize();

	// *****************************	Minimize overdraw	*************************************
	// Select shader (front or back-faces), *** back, if intersect near plane
	RCache.set_ColorWriteEnable();
	RCache.set_CullMode(CULL_CW); // back
	/*
	if (bIntersect)	RCache.set_CullMode		(CULL_CW);		// back
	else			RCache.set_CullMode		(CULL_CCW);		// front
	*/

	// 2D texgens 
	Fmatrix m_Texgen;
	u_compute_texgen_screen(m_Texgen);
	Fmatrix m_Texgen_J;
	u_compute_texgen_jitter(m_Texgen_J);

	// Draw volume with projective texgen
	{
		// Select shader
		u32 _id = 0;
		if (L->flags.bShadow)
		{
			bool bFullSize = (L->X.S.size == u32(RImplementation.o.smapsize));
			if (L->X.S.transluent) _id = SE_L_TRANSLUENT;
			else if (bFullSize) _id = SE_L_FULLSIZE;
			else _id = SE_L_NORMAL;
		}
		else
		{
			_id = SE_L_UNSHADOWED;
			//m_Shadow				= m_Lmap;
		}
		RCache.set_Element(shader->E[_id]);

		// Constants
		RCache.set_c("Ldynamic_pos", L_pos.x, L_pos.y, L_pos.z, 1 / (L_R * L_R));
		RCache.set_c("Ldynamic_color", L_clr.x, L_clr.y, L_clr.z, L_spec);
		RCache.set_c("m_texgen", m_Texgen);

		if (!Device.m_SecondViewport.IsSVPFrame())
			RCache.set_c("sss_id", L->sss_id);
		else
			RCache.set_c("sss_id", -1);

		// Fetch4 : enable
		//		if (RImplementation.o.HW_smap_FETCH4)	{
		//. we hacked the shader to force smap on S0
		//#			define FOURCC_GET4  MAKEFOURCC('G','E','T','4') 
		//			HW.pDevice->SetSamplerState	( 0, D3DSAMP_MIPMAPLODBIAS, FOURCC_GET4 );
		//		}

		RCache.set_CullMode(CULL_CW); // back
		// Render if (light_id <= stencil && z-pass)
		if (! RImplementation.o.dx10_msaa)
		{
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, marker, 0xff, 0x00);
			draw_volume(L->flags.type);
		}
		else // checked Holger
		{
			// per pixel
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, marker, 0xff, 0x00);
			draw_volume(L->flags.type);

			// per sample
			if (RImplementation.o.dx10_msaa_opt)
			{
				RCache.set_Element(shader_msaa[0]->E[_id]);
				RCache.set_Stencil(TRUE, D3DCMP_EQUAL, marker | 0x80, 0xff, 0x00);
				RCache.set_CullMode(D3DCULL_CW);
				draw_volume(L->flags.type);
			}
			else // checked Holger
			{
				for (u32 i = 0; i < RImplementation.o.dx10_msaa_samples; ++i)
				{
					RCache.set_Element(shader_msaa[i]->E[_id]);
					StateManager.SetSampleMask(u32(1) << i);
					RCache.set_Stencil(TRUE, D3DCMP_EQUAL, marker | 0x80, 0xff, 0x00);
					RCache.set_CullMode(D3DCULL_CW);
					draw_volume(L->flags.type);
				}
				StateManager.SetSampleMask(0xffffffff);
			}
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, marker, 0xff, 0x00);
		}

		// Fetch4 : disable
		//		if (RImplementation.o.HW_smap_FETCH4)	{
		//. we hacked the shader to force smap on S0
		//#			define FOURCC_GET1  MAKEFOURCC('G','E','T','1') 
		//			HW.pDevice->SetSamplerState	( 0, D3DSAMP_MIPMAPLODBIAS, FOURCC_GET1 );
		//		}
	}

	// blend-copy
	if (!RImplementation.o.fp16_blend)
	{
		if (! RImplementation.o.dx10_msaa)
			u_setrt(rt_Accumulator,NULL,NULL, dlss::SceneDepth());
		else
			u_setrt(rt_Accumulator,NULL,NULL, rt_MSAADepth->pZRT);
		RCache.set_Element(s_accum_mask->E[SE_MASK_ACCUM_VOL]);
		RCache.set_c("m_texgen", m_Texgen);
		if (! RImplementation.o.dx10_msaa)
		{
			RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, marker, 0xff, 0x00);
			draw_volume(L->flags.type);
		}
		else // checked Holger
		{
			// per pixel
			RCache.set_CullMode(D3DCULL_CW);
			RCache.set_Stencil(TRUE, D3DCMP_EQUAL, marker, 0xff, 0x00);
			draw_volume(L->flags.type);
			if (RImplementation.o.dx10_msaa_opt)
			{
				// per sample
				RCache.set_Element(s_accum_mask_msaa[0]->E[SE_MASK_ACCUM_VOL]);
				RCache.set_CullMode(D3DCULL_CW);
				RCache.set_Stencil(TRUE, D3DCMP_EQUAL, marker | 0x80, 0xff, 0x00);
				draw_volume(L->flags.type);
			}
			else // checked Holger	
			{
				for (u32 i = 0; i < RImplementation.o.dx10_msaa_samples; ++i)
				{
					RCache.set_Element(s_accum_mask_msaa[i]->E[SE_MASK_ACCUM_VOL]);
					RCache.set_CullMode(D3DCULL_CW);
					StateManager.SetSampleMask(u32(1) << i);
					RCache.set_Stencil(TRUE, D3DCMP_EQUAL, marker | 0x80, 0xff, 0x00);
					draw_volume(L->flags.type);
				}
				StateManager.SetSampleMask(0xffffffff);
			}
			RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, marker, 0xff, 0x00);
		}
	}

	//CHK_DX		(HW.pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
	RCache.set_Scissor(0);

	//dwLightMarkerID					+=	2;	// keep lowest bit always setted up
	increment_light_marker(marker);

	u_DBT_disable();
}

void CRenderTarget::accum_point(light* L)
{
    accum_point_impl<false>(L, dwLightMarkerID);
}

void CRenderTarget::accum_point(const RecordedLight& L, u32& marker)
{
    accum_point_impl<true>(&L, marker);
}

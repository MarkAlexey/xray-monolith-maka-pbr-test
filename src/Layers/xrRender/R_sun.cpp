#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/irenderable.h"
#include "FBasicVisual.h"

#include "R_sun_support.h"

constexpr float tweak_COP_initial_offs = 1200.f;

float OLES_SUN_LIMIT_27_01_07 = 100.f;

//////////////////////////////////////////////////////////////////////////
// tables to calculate view-frustum bounds in world space
// note: D3D uses [0..1] range for Z
static Fvector3 corners[8] = {
	{-1, -1, 0}, {-1, -1, +1},
	{-1, +1, +1}, {-1, +1, 0},
	{+1, +1, +1}, {+1, +1, 0},
	{+1, -1, +1}, {+1, -1, 0}
};
static int facetable[6][4] = {
	{6, 7, 5, 4}, {1, 0, 7, 6},
	{1, 2, 3, 0}, {3, 2, 4, 5},
	// near and far planes
	{0, 3, 5, 7}, {1, 6, 4, 2},
};

//////////////////////////////////////////////////////////////////////////
Fvector3 wform(Fmatrix& m, Fvector3 const& v)
{
	Fvector4 r;
	r.x = v.x * m._11 + v.y * m._21 + v.z * m._31 + m._41;
	r.y = v.x * m._12 + v.y * m._22 + v.z * m._32 + m._42;
	r.z = v.x * m._13 + v.y * m._23 + v.z * m._33 + m._43;
	r.w = v.x * m._14 + v.y * m._24 + v.z * m._34 + m._44;
	// VERIFY		(r.w>0.f);
	float invW = 1.0f / r.w;
	Fvector3 r3 = { r.x * invW, r.y * invW, r.z * invW };
	return r3;
}

static bool add_shadow_receiver_bounds(Fbox& receiver, const Fbox& world, const Fmatrix& transform)
{
    if (!world.is_valid() || !_valid(world.min) || !_valid(world.max) || !_valid(transform)) return false;
    Fbox projected;
    projected.xform(world, transform);
    if (!projected.is_valid() || !_valid(projected.min) || !_valid(projected.max)) return false;
    receiver.merge(projected);
    return true;
}

void CRender::init_cacades()
{
	u32 cascade_count = 3;
	m_sun_cascades.resize(cascade_count);

	float fBias = -0.0000025f;
	//	float size = MAP_SIZE_START;
	m_sun_cascades[0].reset_chain = true;
	m_sun_cascades[0].size = ps_ssfx_shadow_cascades.x; //20
	m_sun_cascades[0].bias = m_sun_cascades[0].size * fBias;

	m_sun_cascades[1].size = ps_ssfx_shadow_cascades.y; //40
	m_sun_cascades[1].bias = m_sun_cascades[1].size * fBias;

	m_sun_cascades[2].size = ps_ssfx_shadow_cascades.z; //160
	m_sun_cascades[2].bias = m_sun_cascades[2].size * fBias;

	// 	for( u32 i = 0; i < cascade_count; ++i )
	// 	{
	// 		m_sun_cascades[i].size = size;
	// 		size *= MAP_GROW_FACTOR;
	// 	}
	/// 	m_sun_cascades[m_sun_cascades.size()-1].size = 80;
}

void CRender::render_sun_cascades()
{
	bool b_need_to_render_sunshafts = Target->need_to_render_sunshafts();
	bool last_cascade_chain_mode = m_sun_cascades.back().reset_chain;
	if (b_need_to_render_sunshafts)
		m_sun_cascades[m_sun_cascades.size() - 1].reset_chain = true;

    Fmatrix full, inverse;
    full.mul(Device.mProject, Device.mView);
    D3DXMatrixInverse((D3DXMATRIX*)&inverse, nullptr, (D3DXMATRIX*)&full);
    for (u32 i = 0; i < m_sun_cascades.size(); ++i) prepare_sun_cascade(i, inverse);

#ifdef USE_DX11
	// IX-Ray CSM: all array slices are cleared once, so a cascade with nothing to draw
	// does not keep last frame's depth
	if (o.sun_csm)
		Target->clear_smap_csm();
#endif

#if RENDER == R_R4
    auto& candidates = sun_query_results;
    const CFrustum* frustums[3]{};
    const bool compatible = m_sun_cascades.size() == 3 &&
        m_sun_cascades[0].GMCascade.i_doptions == m_sun_cascades[1].GMCascade.i_doptions &&
        m_sun_cascades[0].GMCascade.i_doptions == m_sun_cascades[2].GMCascade.i_doptions;
    u64 revision = 0;
    if (compatible)
    {
        for (u32 i = 0; i < 3; ++i) frustums[i] = &m_sun_cascades[i].cull_frustum;
        revision = g_SpatialSpace->q_frustums(candidates, m_sun_cascades[0].GMCascade.i_doptions, frustums);
    }
    for (u32 i = 0; i < m_sun_cascades.size(); ++i)
        render_sun_cascade(i, compatible ? &candidates[i] : nullptr, revision);
#else
    for (u32 i = 0; i < m_sun_cascades.size(); ++i) render_sun_cascade(i);
#endif

	if (b_need_to_render_sunshafts)
		m_sun_cascades[m_sun_cascades.size() - 1].reset_chain = last_cascade_chain_mode;
}

void CRender::prepare_sun_cascade(u32 cascade_ind, const Fmatrix& inverse)
{
	PROF_EVENT("Render Cascade: PrepareView");
	light* fuckingsun = (light*)Lights.sun_adapted._get();
	sun::cascade& cascade = m_sun_cascades[cascade_ind];

	CFrustum& cull_frustum = cascade.cull_frustum;
	xr_vector<Fplane> cull_planes;
	Fvector3& cull_COP = cascade.cull_COP;
	Fmatrix& cull_xform = cascade.cull_xform;

	{
		PROF_EVENT("Render Cascade: Prepass");

		// calculate view-frustum bounds in world space
		const Fmatrix& ex_full_inverse = inverse;

		// Compute volume(s) - something like a frustum for infinite directional light
		// Also compute virtual light position and sector it is inside
		{
			FPU::m64r();
			// Lets begin from base frustum
			Fmatrix fullxform_inv = ex_full_inverse;
			//******************************* Need to be placed after cuboid built **************************

			// COP - 100 km away
			cull_COP.mad(Device.vCameraPosition, fuckingsun->direction, -tweak_COP_initial_offs);			

			// Create approximate ortho-xform
			// view: auto find 'up' and 'right' vectors
			Fmatrix mdir_View, mdir_Project;
			Fvector L_dir, L_up, L_right, L_pos;
			L_pos.set(fuckingsun->position);
			L_dir.set(fuckingsun->direction).normalize();
			L_right.set(1, 0, 0);
			if (_abs(L_right.dotproduct(L_dir)) > .99f) L_right.set(0, 0, 1);
			L_up.crossproduct(L_dir, L_right).normalize();
			L_right.crossproduct(L_up, L_dir).normalize();
			mdir_View.build_camera_dir(L_pos, L_dir, L_up);


			//////////////////////////////////////////////////////////////////////////
	#ifdef	_DEBUG
			typedef FixedConvexVolume<true> t_cuboid;
	#else
			typedef FixedConvexVolume<false> t_cuboid;
	#endif

			static t_cuboid light_cuboid;
			light_cuboid.view_frustum_rays.clear();
			{
				// Initialize the first cascade rays, then each cascade will initialize rays for next one.
				if (cascade_ind == 0 || cascade.reset_chain)
				{
					Fvector3 near_p, edge_vec;
					for (int p = 0; p < 4; p++)
					{
						// 					Fvector asd = Device.vCameraDirection;
						// 					asd.mul(-2);
						// 					asd.add(Device.vCameraPosition);
						// 					near_p		= Device.vCameraPosition;//wform		(fullxform_inv,asd); //
						near_p = wform(fullxform_inv, corners[facetable[4][p]]);

						edge_vec = wform(fullxform_inv, corners[facetable[5][p]]);
						edge_vec.sub(near_p);
						edge_vec.normalize();

						light_cuboid.view_frustum_rays.push_back(sun::ray(near_p, edge_vec));
					}
				}
				else
					light_cuboid.view_frustum_rays = cascade.rays;

				light_cuboid.view_ray.P = Device.vCameraPosition;
				light_cuboid.view_ray.D = Device.vCameraDirection;
				light_cuboid.light_ray.P = L_pos;
				light_cuboid.light_ray.D = L_dir;
			}

			// THIS NEED TO BE A CONSTATNT
			Fplane light_top_plane;
			light_top_plane.build_unit_normal(L_pos, L_dir);
			float dist = light_top_plane.classify(Device.vCameraPosition);

			float map_size = cascade.size;
			D3DXMatrixOrthoOffCenterLH((D3DXMATRIX*)&mdir_Project, -map_size * 0.5f, map_size * 0.5f, -map_size * 0.5f,
																map_size * 0.5f, 0.1, dist + map_size);

			// build viewport xform
			float view_dim = float(o.smapsize);
			Fmatrix m_viewport = {
				view_dim / 2.f, 0.0f, 0.0f, 0.0f,
				0.0f, -view_dim / 2.f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				view_dim / 2.f, view_dim / 2.f, 0.0f, 1.0f
			};
			Fmatrix m_viewport_inv;
			D3DXMatrixInverse((D3DXMATRIX*)&m_viewport_inv, 0, (D3DXMATRIX*)&m_viewport);

			// snap view-position to pixel
			cull_xform.mul(mdir_Project, mdir_View);
			Fmatrix cull_xform_inv;
			cull_xform_inv.invert(cull_xform);


			//		light_cuboid.light_cuboid_points.reserve		(9);
			for (int p = 0; p < 8; p++)
				light_cuboid.light_cuboid_points[p] = wform(cull_xform_inv, corners[p]);

			// only side planes
			for (int plane = 0; plane < 4; plane++)
			{
				for (int pt = 0; pt < 4; pt++)
					light_cuboid.light_cuboid_polys[plane].points[pt] = facetable[plane][pt];
			}


			Fvector lightXZshift;
			light_cuboid.compute_caster_model_fixed(cull_planes, lightXZshift, cascade.size, cascade.reset_chain);
			Fvector proj_view = Device.vCameraDirection;
			proj_view.y = 0;
			proj_view.normalize();
			//			lightXZshift.mad(proj_view, 20);

			// Initialize rays for the next cascade
			if (cascade_ind < m_sun_cascades.size() - 1)
				m_sun_cascades[cascade_ind + 1].rays = light_cuboid.view_frustum_rays;

#ifdef _DEBUG
			static bool draw_debug = false;
			if (draw_debug && cascade_ind == 0)
				for (u32 it = 0; it < cull_planes.size(); it++)
					Target->dbg_addplane(cull_planes[it], it * 0xFFF);
#endif

			Fvector cam_shifted = L_pos;
			cam_shifted.add(lightXZshift);

			// rebuild the view transform with the shift.
			mdir_View.identity();
			mdir_View.build_camera_dir(cam_shifted, L_dir, L_up);
			cull_xform.identity();
			cull_xform.mul(mdir_Project, mdir_View);
			cull_xform_inv.invert(cull_xform);


			// Create frustum for query
			cull_frustum._clear();
			for (u32 p = 0; p < cull_planes.size(); p++)
				cull_frustum._add(cull_planes[p]);

			{
				Fvector cam_proj = Device.vCameraPosition;
				const float align_aim_step_coef = 4.f;
				cam_proj.set(floorf(cam_proj.x / align_aim_step_coef) + align_aim_step_coef / 2,
										floorf(cam_proj.y / align_aim_step_coef) + align_aim_step_coef / 2,
										floorf(cam_proj.z / align_aim_step_coef) + align_aim_step_coef / 2);
				cam_proj.mul(align_aim_step_coef);
				Fvector cam_pixel = wform(cull_xform, cam_proj);
				cam_pixel = wform(m_viewport, cam_pixel);
				Fvector shift_proj = lightXZshift;
				cull_xform.transform_dir(shift_proj);
				m_viewport.transform_dir(shift_proj);

				const float align_granularity = 4.f;
				shift_proj.x = shift_proj.x > 0 ? align_granularity : -align_granularity;
				shift_proj.y = shift_proj.y > 0 ? align_granularity : -align_granularity;
				shift_proj.z = 0;

				cam_pixel.x = cam_pixel.x / align_granularity - floorf(cam_pixel.x / align_granularity);
				cam_pixel.y = cam_pixel.y / align_granularity - floorf(cam_pixel.y / align_granularity);
				cam_pixel.x *= align_granularity;
				cam_pixel.y *= align_granularity;
				cam_pixel.z = 0;

				cam_pixel.sub(shift_proj);

				m_viewport_inv.transform_dir(cam_pixel);
				cull_xform_inv.transform_dir(cam_pixel);
				Fvector diff = cam_pixel;
				static float sign_test = -1.f;
				diff.mul(sign_test);
				Fmatrix adjust;
				adjust.translate(diff);
				cull_xform.mulB_44(adjust);
			}

			cascade.xform = cull_xform;

			s32 limit = o.smapsize - 1;
			fuckingsun->X.D.minX = 0;
			fuckingsun->X.D.maxX = limit;
			fuckingsun->X.D.minY = 0;
			fuckingsun->X.D.maxY = limit;

			// full-xform
			FPU::m24r();
		}
	}

}

void CRender::render_sun_cascade(u32 cascade_ind, const xr_vector<ISpatialShared>* prepared, u64 revision)
{
    PROF_EVENT("Render Cascade");
    light* fuckingsun = (light*)Lights.sun_adapted._get();
    sun::cascade& cascade = m_sun_cascades[cascade_ind];
    Fmatrix& cull_xform = cascade.cull_xform;

	// Begin SMAP-render
    {
        PROF_EVENT("Render Cascade: SMAP traverse");
        phase = PHASE_SMAP;
        cascade.GMCascade.set_shadow_receiver_bounds(nullptr);
        cascade.GMCascade.traverse(pOutdoorSector, cascade.cull_frustum, cascade.cull_COP, cascade.cull_xform);
#if RENDER == R_R4
        PROF_EVENT("Render.Shadow.ReceiverBounds");
        Fbox receiver_bounds;
        receiver_bounds.invalidate();
        bool receiver_bounds_valid = false;
        bool receiver_bounds_complete = true;
        bool outdoor_visible = false;
        {
            xrSRWLockGuard guard(&GMBase.S_LC, true);
            if (pOutdoorSector)
            {
                if (auto node = GMBase.m_sector_frustums.find(pOutdoorSector))
                    outdoor_visible = !node->val.first.empty();
            }

            if (outdoor_visible)
            {
                for (dxRender_Visual* visual : GMBase.m_static_seen)
                {
                    if (!visual)
                        continue;

                    receiver_bounds_complete &= add_shadow_receiver_bounds(receiver_bounds, visual->vis.box, cull_xform);
                    receiver_bounds_valid = true;
                }
            }
            else
            {
                for (auto& pair : GMBase.m_sector_frustums)
                {
                    if (pair.val.first.empty() || !pair.key || !pair.key->root())
                        continue;

                    receiver_bounds_complete &= add_shadow_receiver_bounds(receiver_bounds, pair.key->root()->vis.box, cull_xform);
                    receiver_bounds_valid = true;
                }
            }

            for (const ISpatialShared& spatial : GMBase.lstRenderables)
            {
                if (!spatial || !(spatial->spatial.type & (STYPE_RENDERABLE | STYPE_PARTICLE)))
                    continue;

                CSector* sector = static_cast<CSector*>(spatial->spatial.sector);
                auto node = sector ? GMBase.m_sector_frustums.find(sector) : nullptr;
                if (!node || node->val.first.empty())
                    continue;

                const Fsphere& sphere = spatial->spatial.sphere;
                Fvector radius;
                radius.set(sphere.R, sphere.R, sphere.R);
                Fbox world_bounds;
                world_bounds.setb(sphere.P, radius);
                receiver_bounds_complete &= add_shadow_receiver_bounds(receiver_bounds, world_bounds, cull_xform);
                receiver_bounds_valid = true;
            }
        }

        Fbox detail_bounds;
        if (Details && Details->GetVisibleBounds(detail_bounds))
        {
            receiver_bounds_complete &= add_shadow_receiver_bounds(receiver_bounds, detail_bounds, cull_xform);
            receiver_bounds_valid = true;
        }

        else if (Details && psDeviceFlags.test(rsDetails)) receiver_bounds_complete = false;

        const bool volumetric_sunshafts_need_full_shadow =
            cascade_ind == m_sun_cascades.size() - 1 &&
            RImplementation.o.advancedpp &&
            (ps_sunshafts_mode == R2SS_VOLUMETRIC || ps_sunshafts_mode == R2SS_COMBINE_SUNSHAFTS) &&
            Target->need_to_render_sunshafts();
        if (!volumetric_sunshafts_need_full_shadow && receiver_bounds_valid && receiver_bounds_complete &&
            receiver_bounds.max.x >= -1.f && receiver_bounds.max.y >= -1.f &&
            receiver_bounds.min.x <= 1.f && receiver_bounds.min.y <= 1.f)
        {
            receiver_bounds.min.x = _max(receiver_bounds.min.x, -1.f);
            receiver_bounds.min.y = _max(receiver_bounds.min.y, -1.f);
            receiver_bounds.max.x = _min(receiver_bounds.max.x, 1.f);
            receiver_bounds.max.y = _min(receiver_bounds.max.y, 1.f);
            const float receiver_margin = _max(128.f / float(o.smapsize), 4.f / cascade.size);
            receiver_bounds.min.x -= receiver_margin;
            receiver_bounds.min.y -= receiver_margin;
            receiver_bounds.max.x += receiver_margin;
            receiver_bounds.max.y += receiver_margin;
            receiver_bounds.min.x = _max(receiver_bounds.min.x, -1.f);
            receiver_bounds.min.y = _max(receiver_bounds.min.y, -1.f);
            receiver_bounds.max.x = _min(receiver_bounds.max.x, 1.f);
            receiver_bounds.max.y = _min(receiver_bounds.max.y, 1.f);
            Fvector depth_margin;
            depth_margin.mul(fuckingsun->direction, 2.f);
            cull_xform.transform_dir(depth_margin);
            receiver_bounds.max.z += _abs(depth_margin.z);
            cascade.GMCascade.set_shadow_receiver_bounds(&receiver_bounds);
            PROF_TAG("Receiver clipping active", true);
        }
#endif
        cascade.GMCascade.r_dsgraph_capture_static();
        cascade.GMCascade.r_dsgraph_capture_dynamic(nullptr, prepared, revision);
    }

    // Finalize & Cleanup
    {
        PROF_EVENT("Render Cascade: SMAP Finalize");

        fuckingsun->X.D.combine = cull_xform;

        // Render shadow-map
        //. !!! We should clip based on shrinked frustum (again)
        {
            bool bDeffered_Shadows = cascade.GMCascade.RGraph.mapStaticPasses[0][0].size() || cascade.GMCascade.RGraph.mapDynamicPasses[0][0].size();
            bool bForward_Shadows = cascade.GMCascade.RGraph.mapStaticPasses[1][0].size() || cascade.GMCascade.RGraph.mapDynamicPasses[1][0].size() || cascade.GMCascade.RGraph.mapStaticSorted.Sorted.size() || cascade.GMCascade.RGraph.mapDynamicSorted.Sorted.size();
            if (bDeffered_Shadows || bForward_Shadows)
            {
#ifdef USE_DX11
                // IX-Ray CSM: near cascades go into their own array slice
                if (o.sun_csm && cascade_ind < Target->csm_slices())
                    Target->phase_smap_csm(fuckingsun, cascade_ind);
                else
#endif
                Target->phase_smap_direct(fuckingsun, SE_SUN_FAR);
                RCache.set_xform_world(Fidentity);
                RCache.set_xform_view(Fidentity);
                RCache.set_xform_project(fuckingsun->X.D.combine);
                cascade.GMCascade.r_dsgraph_render_graph(0);

                if (psDeviceFlags2.test(rsGrassShadow) && cascade_ind <= ps_ssfx_grass_shadows.x)
                {
                    Details->fade_distance = dm_fade * dm_fade * ps_ssfx_grass_shadows.y;
                    if (ps_r__detail_shadow_cull)
                    {
                        CFrustum sun_cull;
                        sun_cull.CreateFromMatrix(fuckingsun->X.D.combine, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);
                        Details->m_sun_cull = &sun_cull;
                        Details->Render();
                        Details->m_sun_cull = nullptr;
                    }
                    else
                        Details->Render();
                }

                fuckingsun->X.D.transluent = FALSE;
                if (bForward_Shadows)
                {
                    fuckingsun->X.D.transluent = TRUE;
                    Target->phase_smap_direct_tsh(fuckingsun, SE_SUN_FAR);
                    cascade.GMCascade.r_dsgraph_render_graph(1);			// normal level, secondary priority
                    cascade.GMCascade.r_dsgraph_render_sorted();			// strict-sorted geoms
                }
            }
        }

        // End SMAP-render
    }	

#ifdef USE_DX11
	// IX-Ray CSM: array cascades are lit together in one pass after the last of them is
	// rendered; the far cascade continues below with the legacy (installed) far shader
	if (o.sun_csm && cascade_ind < Target->csm_slices())
	{
		if (cascade_ind + 1 == Target->csm_slices())
			Target->accum_direct_csm();

		RCache.set_xform_world(Fidentity);
		RCache.set_xform_view(Device.mView);
		RCache.set_xform_project(Device.mProject);
		return;
	}
#endif

	// Accumulate
	PROF_EVENT("Render Cascade: Accumulate");
	Target->phase_accumulator();

#if defined(USE_DX10) || defined(USE_DX11)
	if (Target->use_minmax_sm_this_frame())
	{
#if defined(USE_DX10) || defined(USE_DX11)
		PIX_EVENT(SE_SUN_NEAR_MINMAX_GENERATE);
#endif
		Target->create_minmax_SM();
	}
#endif

#if defined(USE_DX10) || defined(USE_DX11)
	PIX_EVENT(SE_SUN_NEAR);
#endif

	if (cascade_ind == 0)
		Target->accum_direct_cascade(SE_SUN_NEAR, cascade.xform, cascade.xform,
		                             cascade.bias);
	else if (cascade_ind < m_sun_cascades.size() - 1)
		Target->accum_direct_cascade(SE_SUN_MIDDLE, cascade.xform,
		                             m_sun_cascades[cascade_ind - 1].xform, cascade.bias);
	else
		Target->accum_direct_cascade(SE_SUN_FAR, cascade.xform,
		                             m_sun_cascades[cascade_ind - 1].xform, cascade.bias);

	// Restore XForms
	RCache.set_xform_world(Fidentity);
	RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);
}

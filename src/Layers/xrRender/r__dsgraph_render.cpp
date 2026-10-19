#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/irenderable.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/CustomHUD.h"

#include "FBasicVisual.h"
#include "FVisual.h"
#include "FTreeVisual.h"
#if defined(USE_DX11) && RENDER == R_R4
#include "StaticDrawPacketPolicy.h"
#endif
#include "CHudInitializer.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "../../xrEngine/fmesh.h"
#include "flod.h"

#include "../../xrEngine/xr_object.h"

using namespace R_dsgraph;

extern float r_ssaDISCARD;
extern float r_ssaDONTSORT;
extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

ICF bool R4FullDetailRejectSpatial(ISpatial* spatial, bool normalPhase)
{
#if RENDER == R_R4
	if (ps_r4_full_detail_distance_scale >= 1.f || !normalPhase || !spatial || !g_pGamePersistent ||
		!g_pGamePersistent->Environment().CurrentEnv)
		return false;

	const float farPlane = g_pGamePersistent->Environment().CurrentEnv->far_plane;
	const float fullDetailDistance = std::max(100.f, farPlane * ps_r4_full_detail_distance_scale);
	if (Device.vCameraPosition.distance_to_sqr(spatial->spatial.sphere.P) <=
		_sqr(fullDetailDistance + spatial->spatial.sphere.R))
		return false;

	if (spatial->spatial.type & STYPE_LIGHTSOURCE)
	{
		light* L = (light*)spatial->dcast_Light();
		if (L && L->flags.bHudMode)
			return false;
	}

	return true;
#else
	return false;
#endif
}

ICF float calcLOD(float ssa/*fDistSq*/, float R)
{
	return _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}

template<typename T, bool Reverse>
void CDSGraphManager::r_dsgraph_render_graph_sorted(R_dsgraph::mapDSGraphItems<T, Reverse>& graph, bool _clear)
{
    if (graph.empty())
        return;

    std::sort(graph.begin(), graph.end());

	for (auto& item : graph)
	{
		dxRender_Visual* V = item.pVisual;
		VERIFY(V && V->shader._get());
		RCache.set_Element(item.pSE);
		RCache.set_xform_world(*item.pMatrix);
		RImplementation.apply_object(item.pObject);
		RImplementation.apply_lmaterial();
		//if (item.b_hud_mode)
		//{
		//	//new feature
		//}
		V->Render(calcLOD(item.ssa, V->vis.sphere.R));
	}

	if (_clear)
		graph.clear();

	RCache.set_xform_world(Fidentity);
}

#if defined(USE_DX11) && RENDER == R_R4
void CDSGraphManager::SetPreparedWorld(const Fmatrix* world)
{
    const Fmatrix& view = RCache.get_xform_view();
    const Fmatrix& projection = RCache.get_xform_project();
    if (prepared_frame != Device.dwFrame || memcmp(&prepared_view, &view, sizeof(Fmatrix)) ||
        memcmp(&prepared_projection, &projection, sizeof(Fmatrix)))
    {
        prepared_worlds.clear();
        prepared_frame = Device.dwFrame;
        prepared_view = view;
        prepared_projection = projection;
    }
    auto found = prepared_worlds.find(world);
    if (found == prepared_worlds.end() || memcmp(&found->second.world, world, sizeof(Fmatrix)))
    {
        PROF_EVENT("Render.Graph.PrepareWorldMatrix");
        // Bound retained scratch; a very large scene retains the native path.
        if (prepared_worlds.size() >= 8192)
        {
            RCache.set_xform_world(*world);
            return;
        }
        auto& result = prepared_worlds[world];
        result.world = *world;
        result.view.mul_43(view, result.world);
        result.projection.mul(projection, result.view);
        found = prepared_worlds.find(world);
    }
    const auto& result = found->second;
    RCache.xforms.set_W_prepared(result.world, result.view, result.projection);
}
#endif

void CDSGraphManager::r_dsgraph_render_graph(RenderQueueArray& queues, u32 _priority, bool _clear, bool static_geometry)
{
	RCache.set_xform_world(Fidentity);

	for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
	{
		auto& queue = queues[_priority][iPass];
		if (queue.empty())
			continue;

		// Exact pipelines retain their key ordering; immutable members order by shared IB range.
#if defined(USE_DX11) && RENDER == R_R4
        const auto compare = [](const RenderPacket& a, const RenderPacket& b)
        {
            if (!(a.sortKey == b.sortKey)) return a.sortKey < b.sortKey;
            if (a.static_batch != b.static_batch) return std::less<const void*>()(a.static_batch, b.static_batch);
            return a.static_first_index < b.static_first_index;
        };
#else
        const auto compare = [](const RenderPacket& a, const RenderPacket& b) { return a < b; };
#endif
		if (queue.size() < 4096)
            std::sort(queue.begin(), queue.end(), compare);
		else
            xr_parallel_sort(queue.begin(), queue.end(), compare);

#if defined(USE_DX11) && RENDER == R_R4
        if (static_geometry && _priority == 0 && RImplementation.phase == CRender::PHASE_SMAP && !RImplementation.o.Tshadows)
        {
            PROF_EVENT("Render.Shadow.SelectStaticGroups");
            static u32 serial = 0;
            for (size_t first = 0; first < queue.size();)
            {
                const auto* pass = queue[first].source_pass;
                const u32 type = queue[first].item.pVisual->Type;
                size_t end = first;
                while (end < queue.size() && queue[end].source_pass == pass && queue[end].item.pVisual->Type == type && (type == MT_NORMAL || type == MT_TREE_ST)) ++end;
                if (end - first > 1 && pass && (type == MT_TREE_ST || Fvisual::ShadowBatchPassSafe(*pass)))
                {
                    if (!++serial) ++serial;
                    for (size_t i = first; i < end; ++i)
                    {
                        if (type == MT_TREE_ST)
                        {
                            static_cast<FTreeVisual*>(queue[i].item.pVisual)->SelectShadowBatch(serial);
                            queue[i].shadow_ticket = serial;
                            continue;
                        }
                        auto& mesh = *static_cast<Fvisual*>(queue[i].item.pVisual);
                        if (!mesh.shadow_batch || !mesh.ShadowBatchMemberValid()) continue;
                        auto& batch = *mesh.shadow_batch;
                        if (batch.selection.begin(Device.dwFrame, serial))
                            batch.binding_valid = batch.pass._get() == pass && batch.vertex_revision == pass->vs->shadow_shader_revision &&
                                batch.pixel_revision == pass->ps->shadow_shader_revision;
                        if (batch.binding_valid)
                        {
                            batch.selection.add(mesh.shadow_batch_member, u32(batch.members.size()));
                            queue[i].shadow_ticket = serial;
                        }
                    }
                }
                first = end > first ? end : first + 1;
            }
        }
#endif
		// 2. Render
		vs_type pVS = nullptr;

#if defined(USE_DX10) || defined(USE_DX11)
		gs_type pGS = nullptr;
#endif

		ps_type pPS = nullptr;

#ifdef USE_DX11
		hs_type pHS = nullptr;
		ds_type pDS = nullptr;
#endif
		
		R_constant_table* pCS = nullptr;
		ID3DState* pState = nullptr;
		STextureList* pTextures = nullptr;

        u64 high = 0;

        #if defined(USE_DX11) && RENDER == R_R4
        std::shared_ptr<Fvisual::StaticBatch> batch;
        const SPass* batch_pass = nullptr;
        u32 batch_first = 0, batch_count = 0, batch_vertices = 0, combined_draws = 0, combined_members = 0;
        float batch_lod = 0.f;
        auto flush_static = [&]()
        {
            if (!batch) return;
            RCache.LOD.set_LOD(batch_lod);
            RCache.set_Geometry(batch->geometry);
            RCache.Render(D3DPT_TRIANGLELIST, batch->base_vertex, 0, batch->vertex_count, batch_first, batch_count / 3);
            RCache.stat.r.s_static.add(batch_vertices);
            batch.reset();
            batch_count = batch_vertices = 0;
            ++combined_draws;
        };
#endif
        for (auto& packet : queue)
        {
            #if defined(USE_DX11) && RENDER == R_R4
            const float packet_lod = calcLOD(packet.item.ssa, packet.item.pVisual->vis.sphere.R);
            auto* mesh = packet.item.pVisual->Type == MT_NORMAL ? static_cast<Fvisual*>(packet.item.pVisual) : nullptr;
            const auto* pass = packet.source_pass;
            const bool eligible = static_geometry && RImplementation.phase == CRender::PHASE_NORMAL && _priority == 0 && mesh &&
                mesh->StaticBatchValid() && mesh->static_batch.get() == packet.static_batch &&
                mesh->static_batch_first_index == packet.static_first_index && pass && Fvisual::StaticPassSafe(*pass) &&
                pass->vs._get() == packet.pVS && pass->ps->ps == packet.pPS && pass->constants._get() == packet.pCS &&
                pass->state->state == packet.pState && pass->T._get() == packet.pTextures;
            if (batch && (!eligible || batch != mesh->static_batch || batch_pass != pass || batch_lod != packet_lod ||
                !aoe_render::adjacent_static_range(batch_first, batch_count, mesh->static_batch_first_index))) flush_static();
#endif
            auto& currentKey = packet.sortKey;
            if (currentKey.high != high)
            {
                high = currentKey.high;

                if (packet.pState != pState)
                {
                    pState = packet.pState;
                    RCache.set_States(pState);
                }

#if defined(USE_DX10) || defined(USE_DX11)
                if (packet.pGS != pGS)
                {
                    pGS = packet.pGS;
                    RCache.set_GS(pGS);
                }
#endif

#ifdef USE_DX11
                if (packet.pHS != pHS)
                {
                    pHS = packet.pHS;
                    RCache.set_HS(pHS);
                }
                if (packet.pDS != pDS)
                {
                    pDS = packet.pDS;
                    RCache.set_DS(pDS);
                }
#endif
            }

            // Compare low key stuff regardless, too high collision probability
            if (packet.pVS != pVS)
            {
                pVS = packet.pVS;
                RCache.set_VS(pVS);
            }

            if (packet.pPS != pPS)
            {
                pPS = packet.pPS;
                RCache.set_PS(pPS);
            }

            if (packet.pCS != pCS)
            {
                pCS = packet.pCS;
                RCache.set_Constants(pCS);
            }

            if (packet.pTextures != pTextures)
            {
                pTextures = packet.pTextures;
                RCache.set_Textures(pTextures);
                RImplementation.apply_lmaterial();
            }

			auto& item = packet.item;
			if (!static_geometry)
			{
				#if defined(USE_DX11) && RENDER == R_R4
                SetPreparedWorld(item.pMatrix);
#else
                RCache.set_xform_world(*item.pMatrix);
#endif
				RImplementation.apply_object(item.pObject);
				RImplementation.apply_lmaterial();
			}

			float LOD = calcLOD(item.ssa, item.pVisual->vis.sphere.R);
#ifdef USE_DX11
			RCache.LOD.set_LOD(LOD);
#endif
			#if defined(USE_DX11) && RENDER == R_R4
            if (eligible)
            {
                if (!batch)
                {
                    batch = mesh->static_batch;
                    batch_pass = pass;
                    batch_lod = LOD;
                    batch_first = mesh->static_batch_first_index;
                }
                batch_count += mesh->iCount;
                batch_vertices += mesh->vCount;
                ++combined_members;
                continue;
            }
#endif
#if defined(USE_DX11) && RENDER == R_R4
            if (static_geometry && item.pVisual->Type == MT_TREE_ST && !RCache.is_TessEnabled() &&
                static_cast<FTreeVisual*>(item.pVisual)->DrawShadowBatch(LOD, packet.shadow_ticket)) continue;
            if (static_geometry && mesh && !RCache.is_TessEnabled() &&
                !memcmp(&RCache.xforms.m_w, &Fidentity, sizeof(Fmatrix)) && mesh->DrawShadowBatch(packet.shadow_ticket)) continue;
#endif
            item.pVisual->Render(LOD);
		}

#if defined(USE_DX11) && RENDER == R_R4
        flush_static();
        {
            PROF_EVENT("Render.Static.BatchStats");
            PROF_TAG("Draws", combined_draws);
            PROF_TAG("Members", combined_members);
        }
#endif
		if (_clear)
			queue.clear();
	}
}

//////////////////////////////////////////////////////////////////////////
// HUD render
void CDSGraphManager::r_dsgraph_render_hud()
{
	PROF_EVENT("r_dsgraph_render_hud");
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUD);

	RImplementation.rmNormal();

#if defined(USE_DX11) //  Redotix99: for 3D Shader Based Scopes 		
	if (scope_3D_fake_enabled)
	{
		RCache.set_RT(RImplementation.Target->rt_ssfx_temp->pRT, 3); // Render scope_3D to any buffer

		r_dsgraph_render_graph_sorted(RGraph.mapScopeHUD);

		if (!RImplementation.o.ssfx_motionvectors)
			RCache.set_RT(NULL, 3);
		else
			RCache.set_RT(RImplementation.Target->rt_ssfx_motion_vectors->pRT, 3);
	}
#endif

	if (RGraph.mapCamAttached.size())
	{
		RImplementation.rmNear();

		// Change projection
		initializer.SetCamMode();

		// Rendering
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttached);

		RImplementation.rmNormal();
	}
}

void CDSGraphManager::r_dsgraph_render_hud_ui()
{
	PROF_EVENT("r_dsgraph_render_hud_ui");
	CHudInitializer initializer(true);

	RImplementation.rmNear();
	g_hud->RenderActiveItemUI();
	RImplementation.rmNormal();
}

void CDSGraphManager::r_dsgraph_render_cam_ui()
{
	PROF_EVENT("r_dsgraph_render_cam_ui");

	// Change projection
	CHudInitializer initializer(2);
	
	// Rendering
	RImplementation.rmNear();
	g_hud->RenderCamAttachedUI();
	RImplementation.rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_sorted(bool render_hud)
{
	{
		PROF_EVENT("r_dsgraph_render_sorted");
		// Rendering
		r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Sorted, true);
		r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Sorted, true);
	}

	if (render_hud)
		r_dsgraph_render_sorted_hud();

	// Camera Script Attachments
	if (RGraph.mapCamAttachedSorted.Sorted.size())
	{
		RImplementation.rmNear();
		// Change projection
		CHudInitializer initializer(2);

		// Rendering
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttachedSorted.Sorted, true);
		RImplementation.rmNormal();
	}
}

void CDSGraphManager::r_dsgraph_capture_hud()
{
	if (g_hud)
	{
		g_hud->Render_Last(dcast_IPortalTraverser());
		set_Object();
	}
}

#if defined(USE_DX11)
//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_ScopeSorted()  //  Redotix99: for 3D Shader Based Scopes 	
{
	// Change projection
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapScopeHUDSorted, true);
	RImplementation.rmNormal();
}
#endif

void CDSGraphManager::r_dsgraph_render_sorted_hud()
{
	PROF_EVENT("r_dsgraph_render_sorted_hud");
#if	RENDER==R_R4
	HW.pContext->CopyResource(RImplementation.Target->rt_Accumulator->pSurface, RImplementation.Target->rt_Generic_0->pSurface);
#endif
	CHudInitializer initializer(true);

	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, true);
	RImplementation.rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_emissive(bool clear, bool renderHUD)
{
	PROF_EVENT("r_dsgraph_render_emissive");
#if	RENDER!=R_R1
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Emissive, clear);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Emissive, clear);
	
	//	HACK: Calculate this only once
	CHudInitializer initializer(true);

	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Emissive, clear);
	
	if (renderHUD)
		r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, false);

	RImplementation.rmNormal();
#endif
}

void CDSGraphManager::r_dsgraph_render_water_ssr()
{
#ifdef USE_DX11
	PROF_EVENT("r_dsgraph_render_water_ssr");
	std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end());
	for (auto& N : RGraph.mapWater)
	{
		dxRender_Visual* V = N.pVisual;
		VERIFY(V);

		RCache.set_Shader(RImplementation.Target->s_ssfx_water_ssr);

		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		RCache.set_c("cam_pos", RImplementation.Target->Position_previous.x, RImplementation.Target->Position_previous.y, RImplementation.Target->Position_previous.z, 0.0f);

		// Previous matrix data
		RCache.set_c("m_current", RImplementation.Target->Matrix_current);
		RCache.set_c("m_previous", RImplementation.Target->Matrix_previous);

		V->Render(calcLOD(N.ssa, V->vis.sphere.R));
	}
#endif
}

void CDSGraphManager::r_dsgraph_render_water()
{
	PROF_EVENT("r_dsgraph_render_water_ssr");
    std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end());
    for (auto& N : RGraph.mapWater)
	{
		dxRender_Visual* V = N.pVisual;
		VERIFY(V);

#ifdef USE_DX11
		if (RImplementation.o.ssfx_water)
		{
			RCache.set_Shader(RImplementation.Target->s_ssfx_water);
		}
#endif

		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		// Wind settings
		float WindDir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
		float WindVel = g_pGamePersistent->Environment().CurrentEnv->wind_velocity;
		RCache.set_c("wind_setup", WindDir, WindVel, 0, 0);

		V->Render(calcLOD(N.ssa, V->vis.sphere.R));
	}
	RGraph.mapWater.clear();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_wmarks()
{
	PROF_EVENT("r_dsgraph_render_wmarks");
#if	RENDER!=R_R1
	// Rendering
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Wmark);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Wmark);
	//	HACK: Calculate this only once
	CHudInitializer initalizer(true);

	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Wmark);
	RImplementation.rmNormal();
#endif
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_distort()
{
	PROF_EVENT("r_dsgraph_render_distort");
	// Rendering
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Distort, true);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Distort, true);
	//	HACK: Calculate this only once
	CHudInitializer initalizer(true);

	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Distort);
	RImplementation.rmNormal();
}

#include"LightTrack.h"
void CDSGraphManager::r_dsgraph_capture_static()
{
    PROF_EVENT("Render.Static.UnionFrustumCapture");
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);

	if (i_start)
	{
		// Traverse sector/portal structure
		if (psDeviceFlags.test(rsDrawStatic))
		{
			if (dbg)
			{
				const u32 sector_nodes = u32(m_sector_frustums.size());
				dbg->static_sector_nodes += sector_nodes;
				if (opt_bucket)
					dbg->static_sector_nodes_opt += sector_nodes;
				else
					dbg->static_sector_nodes_noopt += sector_nodes;
			}

			// Determine visibility for static geometry hierrarhy
			for (auto& pair : m_sector_frustums)
			{
				xr_vector<CFrustum>& frustums = pair.val.first;
				if (frustums.empty())
					continue;

				if (frustums.size() == 1)
				{
					if (dbg)
					{
						++dbg->static_frustum_nodes;
						++dbg->static_add_root_calls;
						if (opt_bucket)
						{
							++dbg->static_frustum_nodes_opt;
							++dbg->static_add_root_calls_opt;
						}
						else
						{
							++dbg->static_frustum_nodes_noopt;
							++dbg->static_add_root_calls_noopt;
						}
					}
					add_Static((IRenderVisual*)pair.key->root(), frustums.front(), frustums.front().getMask());
					continue;
				}

				xr_vector<u32> masks;
				masks.reserve(frustums.size());
				for (CFrustum& frustum_node : frustums)
					masks.push_back(frustum_node.getMask());

				if (dbg)
				{
					dbg->static_frustum_nodes += u32(frustums.size());
					++dbg->static_add_root_calls;
					if (opt_bucket)
					{
						dbg->static_frustum_nodes_opt += u32(frustums.size());
						++dbg->static_add_root_calls_opt;
					}
					else
					{
						dbg->static_frustum_nodes_noopt += u32(frustums.size());
						++dbg->static_add_root_calls_noopt;
					}
				}

				add_Static_MultiFrustum((IRenderVisual*)pair.key->root(), frustums, masks);
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture_lights()
{
	PROF_EVENT("r_dsgraph_capture_lights")
	g_SpatialSpaceLights->q_frustum
	(
		lstLights,
		ISpatial_DB::O_ORDERED,
		STYPE_LIGHTSOURCE,
		i_frustum
	);

#if	RENDER==R_R1
	std::sort(lstLights.begin(), lstLights.end(), [](const ISpatialShared& _1, const ISpatialShared& _2) noexcept
	{
		if (!_1.get() || !_2.get()) return false;

		return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
	});
#endif

	for (ISpatialShared spatial : lstLights)
	{
		if (0 == spatial) continue;
		if (R4FullDetailRejectSpatial(spatial.get(), i_mask[CDSGraphManager::fl_normal])) continue;
		spatial->spatial_updatesector();
		CSector* sector = (CSector*)spatial->spatial.sector;
		if (0 == sector) continue;

		if (!RImplementation.HOM.visible(spatial->spatial.sphere)) continue;

		if ((spatial->spatial.type & STYPE_LIGHTSOURCE))
		{
			// lightsource
			if (light* L = (light*)(spatial->dcast_Light()))
			{
#if	RENDER==R_R1
				RImplementation.L_DB->add_light(L);
#else
				if (L->get_LOD() > ps_r2_shadow_lod_min && L->has_light_visible_from_sectors(*this))
				{
					RImplementation.Lights.add_light(L);
				}
#endif
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture_dynamic(CObject* O, const xr_vector<ISpatialShared>* prepared, u64 revision)
{
	PROF_EVENT("r_dsgraph_capture_dynamic")
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);

	if (i_start)
	{
		if (psDeviceFlags.test(rsDrawDynamic))
		{
			// Traverse object database
            bool used_prepared = false;
            if (prepared)
            {
                xrSRWLockGuard guard(&g_SpatialSpace->db_lock, true);
                if (revision == g_SpatialSpace->query_revision.load(std::memory_order_acquire))
                {
                    lstRenderables = *prepared;
                    used_prepared = true;
                }
            }
            if (!used_prepared)
            {
			g_SpatialSpace->q_frustum
			(
				lstRenderables,
				ISpatial_DB::O_ORDERED,
				i_doptions,
				i_frustum
			);
            }
            PROF_TAG("Prepared spatial candidates", used_prepared);

			if (dbg)
			{
				const u32 spatial_count = u32(lstRenderables.size());
				dbg->dynamic_spatials += spatial_count;
				if (opt_bucket)
					dbg->dynamic_spatials_opt += spatial_count;
				else
					dbg->dynamic_spatials_noopt += spatial_count;
			}
			set_Object();
#if	RENDER==R_R1
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				std::sort(lstRenderables.begin(), lstRenderables.end(), [](const ISpatialShared& _1, const ISpatialShared& _2) noexcept
				{
					if (!_1.get() || !_2.get()) return false;

					return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
				});

				if (ps_actor_shadow_flags.test(1))
					g_hud->Render_First(dcast_IPortalTraverser());

				r_dsgraph_capture_hud();
			}
#endif
			u32 uID_LTRACK = u32(-1);
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				// update light-vis for current entity / actor
				if (CObject* O = g_pGameLevel->CurrentViewEntity())
				{
					if (!O->getDestroy())
					{
						if (CROS_impl* R = (CROS_impl*)O->ROS())
							R->update(O);
					}
				}

				RImplementation.uLastLTRACK++;
				if (!lstRenderables.empty())
				{
					uID_LTRACK = RImplementation.uLastLTRACK % lstRenderables.size();
#if	RENDER!=R_R1
					// update light-vis for selected entity
					// track lighting environment
					if (IRenderable* renderable = (IRenderable*)lstRenderables[uID_LTRACK]->dcast_Renderable())
					{
						if (CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
							T->update(renderable);
					}
#endif
				}

			}

			// Determine visibility for dynamic part of scene
			for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++)
			{
				ISpatialShared spatial = lstRenderables[o_it];
				if (0 == spatial) continue;
				if (R4FullDetailRejectSpatial(spatial.get(), i_mask[CDSGraphManager::fl_normal])) continue;
				CSector* sector = (CSector*)spatial->spatial.sector;
				if (0 == sector) continue;

				if (i_mask[CDSGraphManager::fl_normal] && !RImplementation.HOM.visible(spatial->spatial.sphere))
					continue;

#if	RENDER==R_R1
				if ((spatial->spatial.type & STYPE_GLOW))
				{
					if (CGlow* glow = spatial->dcast_CGlow())
					{
						// It may be an glow
						RImplementation.L_Glows->add(glow);
					}
					continue;
				}
#endif

//				if ((spatial->spatial.type & STYPE_LIGHTSOURCE))
//				{
//					// lightsource
//					if (light* L = (light*)(spatial->dcast_Light()))
//					{
//#if	RENDER==R_R1
//						RImplementation.L_DB->add_light(L);
//#else
//						if (L->get_LOD() > EPS_L && L->has_light_visible_from_sectors(PT))
//						{
//							RImplementation.Lights.add_light(L);
//						}
//#endif
//					}
//					continue;
//				}

				if(!(spatial->spatial.type & STYPE_RENDERABLE) && !(spatial->spatial.type & STYPE_PARTICLE) && !(spatial->spatial.type & STYPE_RENDERABLESHADOW))
					continue;
				if (!is_sector_visible(sector))
					continue;

				for (CFrustum& frustum : m_sector_frustums.find(sector)->val.first)
				{
					if (dbg)
					{
						++dbg->dynamic_frustum_tests;
						if (opt_bucket)
							++dbg->dynamic_frustum_tests_opt;
						else
							++dbg->dynamic_frustum_tests_noopt;
					}

					if (frustum.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
					{
						if (dbg)
						{
							++dbg->dynamic_frustum_hits;
							if (opt_bucket)
								++dbg->dynamic_frustum_hits_opt;
							else
								++dbg->dynamic_frustum_hits_noopt;
						}

						// renderable
						IRenderable* renderable = spatial->dcast_Renderable();
						if (0 == renderable) break;

						if (O && O->dcast_Renderable() == renderable) break;

						// Rendering
#if	RENDER==R_R1
						if (i_mask[CDSGraphManager::fl_normal] && o_it == uID_LTRACK && renderable->renderable_ROS())
						{
							// track lighting environment
							if(CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
								T->update(renderable);
						}
#endif
						if (i_mask[CDSGraphManager::fl_normal] && !(spatial->spatial.type & STYPE_PARTICLE))
							set_Object(renderable);

						renderable->renderable_Render(dcast_IPortalTraverser());
						if (dbg)
						{
							++dbg->dynamic_rendered;
							if (opt_bucket)
								++dbg->dynamic_rendered_opt;
							else
								++dbg->dynamic_rendered_noopt;
						}

						if (i_mask[CDSGraphManager::fl_normal] && !(spatial->spatial.type & STYPE_PARTICLE))
							set_Object();
						break;
					}
				}
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture(bool lights, bool dynamic, CObject* O)
{
	PROF_EVENT("r_dsgraph_capture")
	r_dsgraph_capture_static();

	if(lights)
		r_dsgraph_capture_lights();

	if (dynamic)
		r_dsgraph_capture_dynamic(O);
}

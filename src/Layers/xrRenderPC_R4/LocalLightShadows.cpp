#include "stdafx.h"
#include "LocalLightShadows.h"
#include "LocalShadowVisibility.h"
#include "RenderContext.h"
#include "../xrRender/FVisual.h"
#include "../xrRender/FTreeVisual.h"
#include "../xrRender/FSkinned.h"
#include "../../xrEngine/xr_object.h"
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr u32 no_index = LocalShadowVisibility::none;

struct ShadowConstants
{
    u32 stage, slot, bytes, matrix_offset = no_index;
};

struct ShadowPass
{
    ref_pass owner;
    std::vector<ShadowConstants> constants;
    ComPtr<ID3D11DepthStencilState> query_depth;
};

struct ShadowDraw
{
    ref_geom geometry;
    ID3D11InputLayout* layout;
    u32 pass, base_vertex, vertices, first_index, index_count;
};

struct ShadowCache
{
    struct PassStamp
    {
        ref_pass pass;
        u32 vertex_revision, pixel_revision;
    };
    std::vector<PassStamp> stamps;
    std::vector<ShadowPass> passes;
    std::vector<ShadowDraw> draws;
    LocalShadowVisibility visibility;
    ComPtr<ID3D11Query> query;
    Fmatrix transform;
    D3D11_VIEWPORT viewport{};
    u32 selected = no_index, generation = 0, queue_revision = 0;
    bool recorded = false;

    void ResetVisibility()
    {
        visibility.Reset(static_cast<u32>(draws.size()));
        query.Reset();
        selected = no_index;
    }

    bool Current(const light& source) const
    {
        if (queue_revision != source.GMLight.static_queue_revision) return false;
        for (const auto& stamp : stamps)
            if (stamp.pass->vs->shadow_shader_revision != stamp.vertex_revision ||
                stamp.pass->ps->shadow_shader_revision != stamp.pixel_revision) return false;
        return true;
    }

    void UpdateView(const light& source)
    {
        if (memcmp(&transform, &source.X.S.combine, sizeof(transform)) ||
            viewport.TopLeftX != float(source.X.S.posX) || viewport.TopLeftY != float(source.X.S.posY) ||
            viewport.Width != float(source.X.S.size))
        {
            // Atlas repacking invalidates query evidence, not immutable geometry.
            ResetVisibility();
            transform = source.X.S.combine;
            viewport = {float(source.X.S.posX), float(source.X.S.posY),
                float(source.X.S.size), float(source.X.S.size), 0.f, 1.f};
        }
    }
};

ID3D11InputLayout* ShadowLayout(SDeclaration& declaration, SVS& shader)
{
    if (!shader.signature || declaration.dx10_dcl_code.size() < 2) return nullptr;
    auto* signature = shader.signature->signature;
    auto found = declaration.vs_to_layout.find(signature);
    if (found != declaration.vs_to_layout.end()) return found->second;
    ID3D11InputLayout* layout = nullptr;
    const HRESULT result = HW.pDevice->CreateInputLayout(declaration.dx10_dcl_code.data(),
        static_cast<UINT>(declaration.dx10_dcl_code.size() - 1),
        signature->GetBufferPointer(), signature->GetBufferSize(), &layout);
    if (FAILED(result)) return nullptr;
    declaration.vs_to_layout.emplace(signature, layout);
    return layout;
}

bool PreparePass(ShadowPass& output, const SPass& pass)
{
    if (!Fvisual::ShadowBatchPassSafe(pass)) return false;
    D3D11_RASTERIZER_DESC rasterizer{};
    pass.state->state->RasterizerState()->GetDesc(&rasterizer);
    if (rasterizer.ScissorEnable) return false; // No inherited immediate-context rectangle.
    auto* matrix = pass.constants->get("m_WVP");
    if (!matrix || matrix->type != RC_float || (matrix->destination & 0xff) != RC_dest_vertex ||
        matrix->vs.cls != RC_4x4) return false;
    const u32 matrix_slot = (matrix->destination & RC_dest_vertex_cb_index_mask) >> RC_dest_vertex_cb_index_shift;
    bool bound = false;
    for (const auto& entry : pass.constants->m_CBTable)
    {
        const u32 stage = entry.first & CB_BufferTypeMask;
        const u32 slot = entry.first & CB_BufferIndexMask;
        if ((stage != CB_BufferVertexShader && stage != CB_BufferPixelShader) ||
            slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) return false;
        D3D11_BUFFER_DESC description{};
        entry.second->GetBuffer()->GetDesc(&description);
        ShadowConstants constants{stage, slot, description.ByteWidth};
        if (!constants.bytes || constants.bytes > D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16 || constants.bytes % 16)
            return false;
        if (stage == CB_BufferVertexShader && slot == matrix_slot)
        {
            if (matrix->vs.index > constants.bytes || sizeof(Fmatrix) > constants.bytes - matrix->vs.index)
                return false;
            constants.matrix_offset = matrix->vs.index;
            bound = true;
        }
        output.constants.push_back(constants);
    }
    if (!bound) return false;
    D3D11_DEPTH_STENCIL_DESC depth{};
    pass.state->state->DepthStencilState()->GetDesc(&depth);
    // The test includes the candidate's own previously written depth. Equal
    // depth must pass, even when the original pass used strict LESS.
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(HW.pDevice->CreateDepthStencilState(&depth, output.query_depth.GetAddressOf()))) return false;
    output.owner = ref_pass(const_cast<SPass*>(&pass));
    return true;
}

std::unique_ptr<ShadowCache> PrepareCache(light& source)
{
    PROF_EVENT("Render.LocalShadows.CompileCache");
    auto cache = std::make_unique<ShadowCache>();
    cache->queue_revision = source.GMLight.static_queue_revision;
    cache->transform = source.X.S.combine;
    cache->viewport = {float(source.X.S.posX), float(source.X.S.posY),
        float(source.X.S.size), float(source.X.S.size), 0.f, 1.f};
    std::map<const SPass*, u32> passes;
    for (u32 p = 0; p < SHADER_PASSES_MAX; ++p)
    {
        auto& queue = source.GMLight.RGraph.mapStaticPasses[0][p];
        for (auto& packet : queue)
        {
            packet.local_shadow_eligible = false;
            packet.local_shadow_index = no_index;
            packet.local_shadow_hidden = false;
            auto* visual = packet.item.pVisual;
            const auto* pass = packet.source_pass;
            if (pass && pass->vs && pass->ps && std::none_of(cache->stamps.begin(), cache->stamps.end(),
                [pass](const ShadowCache::PassStamp& stamp) { return stamp.pass._get() == pass; }))
                cache->stamps.push_back({ref_pass(const_cast<SPass*>(pass)),
                    pass->vs->shadow_shader_revision, pass->ps->shadow_shader_revision});
            if (!visual || visual->Type != MT_NORMAL || !pass || p != 0) continue;
            const auto& mesh = *static_cast<Fvisual*>(visual);
            const auto* element = mesh.shader ? mesh.shader->E[SE_R2_SHADOW]._get() : nullptr;
            if (!mesh.immutable_level_geometry || !element || element->passes.size() != 1 ||
                element->passes[0]._get() != pass || element->flags.bStrictB2F || element->flags.bEmissive ||
                element->flags.bDistort || element->flags.bWmark || element->flags.isWater || element->flags.iScopeLense ||
                pass->vs._get() != packet.pVS || pass->ps->ps != packet.pPS ||
                pass->constants._get() != packet.pCS || pass->state->state != packet.pState) continue;
            const auto& geometry = mesh.m_fast ? *mesh.m_fast : static_cast<const IRender_Mesh&>(mesh);
            if (!geometry.rm_geom || !geometry.rm_geom->dcl || !geometry.rm_geom->vb || !geometry.rm_geom->ib ||
                !geometry.vCount || !geometry.dwPrimitives || geometry.iCount != geometry.dwPrimitives * 3) continue;
            auto found = passes.find(pass);
            if (found == passes.end())
            {
                ShadowPass prepared{};
                const bool valid = PreparePass(prepared, *pass);
                const u32 index = valid ? static_cast<u32>(cache->passes.size()) : no_index;
                if (valid) cache->passes.push_back(std::move(prepared));
                found = passes.emplace(pass, index).first;
            }
            if (found->second == no_index) continue;
            auto* layout = ShadowLayout(*geometry.rm_geom->dcl, *pass->vs);
            if (!layout) continue;
            packet.local_shadow_index = static_cast<u32>(cache->draws.size());
            cache->draws.push_back({geometry.rm_geom, layout, found->second, geometry.vBase,
                geometry.vCount, geometry.iBase, geometry.iCount});
            packet.local_shadow_eligible = true;
        }
    }
    cache->ResetVisibility();
    return cache;
}

struct LightShadowJob
{
    light* source = nullptr;
    ShadowCache* cache = nullptr;
    CFrustum frustum;
    xr_vector<ISpatialShared> candidates;
    std::vector<Fmatrix> worlds;
    u64 revision = u64(-1);
    bool decorative = false, deferred = false, forward = false;
};

struct ShadowAtlasJob
{
    u32 lane = 0;
    std::vector<LightShadowJob*> lights;
    ComPtr<ID3D11CommandList> commands;
    CBackend::_stats statistics{};
    bool valid = false;
};

void SetShadowView(light& source, ID3D11DepthStencilView* depth)
{
    RCache.set_RT(nullptr, 0); RCache.set_RT(nullptr, 1);
    RCache.set_RT(nullptr, 2); RCache.set_RT(nullptr, 3);
    RCache.set_ZB(depth);
    D3D11_VIEWPORT view{float(source.X.S.posX), float(source.X.S.posY),
        float(source.X.S.size), float(source.X.S.size), 0.f, 1.f};
    RenderDeviceContext()->RSSetViewports(1, &view);
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable(FALSE);
    RCache.set_xform_world(Fidentity);
    RCache.set_xform_view(source.X.S.view);
    RCache.set_xform_project(source.X.S.project);
}

void RecordLight(LightShadowJob& job, ID3D11DepthStencilView* depth)
{
    auto& graph = job.source->GMLight;
    struct GraphScope
    {
        CDSGraphManager& graph;
        ~GraphScope() { graph.shadow_execution = graph.shadow_static_subset = 0; }
    } graph_scope{graph};
    graph.shadow_execution = 1;
    SetShadowView(*job.source, depth);
    // Learn occlusion only against immutable opaque depth. The original graph
    // renderer still selects complete native shadow batches for this subset.
    graph.shadow_static_subset = 1;
    graph.r_dsgraph_render_static(0, false);
    if (job.cache && job.cache->selected != no_index)
    {
        auto& cache = *job.cache;
        const auto& draw = cache.draws[cache.selected];
        const auto& pass = cache.passes[draw.pass];
        RCache.set_States(pass.owner->state);
        RCache.set_VS(pass.owner->vs);
        RCache.set_PS(pass.owner->ps);
        RCache.set_GS(pass.owner->gs);
        RCache.set_HS(pass.owner->hs);
        RCache.set_DS(pass.owner->ds);
        RCache.set_Constants(pass.owner->constants);
        RCache.set_Textures(pass.owner->T);
        RCache.set_xform_world(Fidentity);
        RCache.set_Geometry(draw.geometry._get());
        StateManager.Apply();
        RenderDeviceContext()->OMSetDepthStencilState(pass.query_depth.Get(), 0);
        RenderDeviceContext()->Begin(cache.query.Get());
        RCache.Render(D3DPT_TRIANGLELIST, draw.base_vertex, 0, draw.vertices, draw.first_index, draw.index_count / 3);
        RenderDeviceContext()->End(cache.query.Get());
        // Direct query depth binding must not leak into the subsequent tree,
        // alpha-tested or dynamic passes through a stale state-cache entry.
        RCache.ResetBindings();
        SetShadowView(*job.source, depth);
    }
    graph.shadow_static_subset = 2;
    graph.r_dsgraph_render_static(0, false);
    graph.shadow_static_subset = 0;
    graph.r_dsgraph_render_dynamic(0, false);
}


}

struct LocalLightShadows::State
{
    static constexpr u32 lanes = 4;
    std::map<light*, std::unique_ptr<ShadowCache>> caches;
    std::vector<std::unique_ptr<LightShadowJob>> lights;
    std::vector<ShadowAtlasJob> atlases;
    RenderTask workers[lanes];
    ShadowMap maps[lanes];
    light_Package* package = nullptr;
    bool initialized = false, supported = false, querying = false, recording = false, ready = false;
    bool visibility = true, parallel = true;
    u32 rejected = 0;

    bool Initialize()
    {
        if (initialized) return supported;
        initialized = true;
        const char* names[lanes] = {"Render.LocalShadows.0", "Render.LocalShadows.1", "Render.LocalShadows.2", "Render.LocalShadows.3"};
        for (u32 i = 0; i < lanes; ++i)
            if (!workers[i].Start(i + 1, names[i]) || !maps[i].Initialize(i == 0))
            {
                Msg("! Local shadows: context/atlas creation failed; keeping original renderer");
                return false;
            }
        supported = true;
        Msg("* Local shadows: four complete recording backends ready");
        return true;
    }

    void Query(u32 lane)
    {
        PROF_EVENT("Render.LocalShadows.Query");
        for (size_t i = lane; i < lights.size(); i += lanes)
        {
            auto& job = *lights[i];
            if (job.decorative || !psDeviceFlags.test(rsDrawDynamic) || !job.source->GMLight.i_start) continue;
            job.revision = g_SpatialSpace->q_frustum_snapshot(job.candidates,
                job.source->GMLight.i_doptions, job.frustum);
        }
    }

    void Draw(u32 lane, RenderContext& context)
    {
        PROF_EVENT("Render.LocalShadows.Record");
        FTreeVisual::ResetShadowStats();
        dx10ConstantBuffer::ResetStats();
        u32 calls = 0, hidden = 0, queries = 0;
        for (auto& atlas : atlases)
        {
            if (atlas.lane != lane) continue;
            context.Begin(CRender::PHASE_SMAP);
            context.DeviceContext()->ClearDepthStencilView(maps[lane].Depth(), D3D11_CLEAR_DEPTH, 1.f, 0);
            for (auto* job : atlas.lights)
            {
                RecordLight(*job, maps[lane].Depth());
                if (job->cache)
                {
                    for (const auto bit : job->cache->visibility.hidden) hidden += bit != 0;
                    queries += job->cache->selected != no_index;
                }
            }
            const HRESULT result = context.Finish(atlas.commands.GetAddressOf());
            atlas.valid = SUCCEEDED(result);
            atlas.statistics = context.Backend().stat;
            calls += atlas.statistics.calls;
        }
        PROF_TAG("Draws", calls);
        PROF_TAG("Hidden casters", hidden);
        PROF_TAG("Queries", queries);
        FTreeVisual::ProfileShadowStats();
        dx10ConstantBuffer::ProfileStats();
    }
};

LocalLightShadows::LocalLightShadows() : state(std::make_unique<State>()) {}
LocalLightShadows::~LocalLightShadows() { EndFrame(); }

void LocalLightShadows::BeginFrame(light_Package& package)
{
    EndFrame();
    PROF_EVENT("Render.LocalShadows.PrepareFrame");
    const bool enabled = ps_r4_mt_local_shadows || ps_r4_smapvis;
    if (!enabled || RImplementation.o.Tshadows || !state->Initialize())
    {
        for (auto& cache : state->caches) cache.second->ResetVisibility();
        for (auto* light : package.v_shadowed)
            for (auto& queue : light->GMLight.RGraph.mapStaticPasses[0])
                for (auto& packet : queue) packet.local_shadow_hidden = false;
        return;
    }
    state->parallel = ps_r4_mt_local_shadows != 0;
    if (state->visibility != bool(ps_r4_smapvis))
    {
        for (auto& cache : state->caches) cache.second->ResetVisibility();
        state->visibility = ps_r4_smapvis != 0;
    }
    state->package = &package;
    state->rejected = 0;
    auto& source = package.v_shadowed;
    source.erase(std::remove_if(source.begin(), source.end(), [this](light* item)
    {
        if (!item->m_parent || ps_r2_shadow_omnipart_vischeck) item->vis_update();
        else if (item->m_parent->omnipart[0] == item)
        {
            item->m_parent->vis_update();
            for (auto* face : item->m_parent->omnipart)
            {
                face->vis.pending = item->m_parent->vis.pending;
                face->vis.visible = item->m_parent->vis.visible;
            }
        }
        if (!item->vis.visible) { ++state->rejected; return true; }
        item->optimize_smap_size();
        return false;
    }), source.end());

    xr_vector<light*> remaining = source;
    u16 atlas_id = 0;
    const auto saved_phase = RImplementation.phase;
    RImplementation.phase = CRender::PHASE_SMAP;
    FTreeVisual::PrepareFrame();
    while (!remaining.empty())
    {
        RImplementation.LP_smap_pool.initialize(RImplementation.o.smapsize);
        std::sort(remaining.begin(), remaining.end(), [](light* a, light* b) { return a->X.S.size > b->X.S.size; });
        state->atlases.emplace_back();
        auto& atlas = state->atlases.back();
        atlas.lane = atlas_id % State::lanes;
        for (auto it = remaining.begin(); it != remaining.end();)
        {
            light& item = **it;
            SMAP_Rect rectangle;
            if (!RImplementation.LP_smap_pool.push(rectangle, item.X.S.size)) { ++it; continue; }
            item.X.S.posX = rectangle.min.x; item.X.S.posY = rectangle.min.y; item.vis.smap_ID = atlas_id;
            auto job = std::make_unique<LightShadowJob>();
            job->source = &item;
            job->frustum = item.X.S.frustum;
            job->decorative = std::any_of(std::begin(item.decor_object), std::end(item.decor_object),
                [](CObject* object) { return object && !object->getDestroy(); });
            const Fvector saved_position = item.position;
            if (item.flags.bHudMode) Device.hud_to_world(item.position);
            if (!job->decorative && item.m_moving_frames < 32u)
            {
                item.GMLight.RGraph.clear_static<false>();
                item.GMLight.traverse((CSector*)item.SpatialComponent->spatial.sector,
                    item.X.S.frustum, item.position, item.X.S.combine);
                item.GMLight.r_dsgraph_capture_static();
                ++item.m_moving_frames;
            }
            item.position = saved_position;
            if (item.m_moving_frames >= 32 && !job->decorative && !item.flags.bHudMode)
            {
                auto& cache = state->caches[&item];
                if (!cache || !cache->Current(item)) cache = PrepareCache(item);
                cache->UpdateView(item);
                cache->selected = no_index;
                if (state->visibility && !cache->draws.empty())
                {
                    if (cache->visibility.pending != no_index)
                    {
                        UINT64 samples = 0;
                        const HRESULT result = HW.pContext->GetData(cache->query.Get(), &samples, sizeof(samples), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                        cache->visibility.Resolve(result != S_FALSE, SUCCEEDED(result), samples);
                    }
                    cache->selected = cache->visibility.Select();
                    if (cache->selected != no_index && !cache->query)
                    {
                        D3D11_QUERY_DESC description{D3D11_QUERY_OCCLUSION, 0};
                        if (FAILED(HW.pDevice->CreateQuery(&description, cache->query.GetAddressOf())))
                        {
                            cache->visibility.failed = true; cache->selected = no_index;
                        }
                    }
                }
                job->cache = cache.get();
            }
            else state->caches.erase(&item);
            for (auto& queue : item.GMLight.RGraph.mapStaticPasses[0])
                for (auto& packet : queue)
                    packet.local_shadow_hidden = job->cache && state->visibility && packet.local_shadow_index < job->cache->visibility.hidden.size() &&
                        job->cache->visibility.hidden[packet.local_shadow_index];
            atlas.lights.push_back(job.get());
            state->lights.push_back(std::move(job));
            it = remaining.erase(it);
        }
        R_ASSERT2(!atlas.lights.empty(), "A light shadow map does not fit the atlas");
        ++atlas_id;
    }
    RImplementation.phase = saved_phase;
    // The last atlas uses the original resource, preserving its public texture
    // identity and final contents for later passes without a full-map GPU copy.
    for (size_t i = 0; i < state->atlases.size(); ++i)
        state->atlases[i].lane = u32((state->atlases.size() - 1 - i) % State::lanes);
    for (u32 lane = 0; lane < State::lanes; ++lane)
    {
        state->workers[lane].Context().Synchronize();
        if (state->parallel) state->workers[lane].Run([this, lane](RenderContext&) { state->Query(lane); });
        else state->Query(lane);
    }
    state->querying = true;
}

void LocalLightShadows::Record()
{
    if (!state->package || !state->querying) return;
    {
        PROF_EVENT("Render.LocalShadows.QueryWait");
        bool success = true;
        for (auto& worker : state->workers) success = worker.Wait() && success;
        if (!success) for (auto& job : state->lights) job->revision = u64(-1);
    }
    state->querying = false;
    PROF_EVENT("Render.LocalShadows.CaptureAndPrime");
    const auto saved_phase = RImplementation.phase;
    RImplementation.phase = CRender::PHASE_SMAP;
    for (auto& atlas : state->atlases)
    {
        auto& context = state->workers[atlas.lane].Context();
        for (auto* job : atlas.lights)
        {
            auto& item = *job->source;
            auto& graph = item.GMLight;
            graph.RGraph.clear_dynamic<false>();
            if (job->decorative)
            {
                for (auto* object : item.decor_object)
                    if (object && !object->getDestroy()) object->renderable_Render(&graph);
            }
            else graph.r_dsgraph_capture_dynamic(item.ignore_object, &job->candidates, job->revision);
            job->deferred = !graph.RGraph.mapStaticPasses[0][0].empty() || !graph.RGraph.mapDynamicPasses[0][0].empty();
            job->forward = !graph.RGraph.mapStaticPasses[1][0].empty() || !graph.RGraph.mapDynamicPasses[1][0].empty() ||
                !graph.RGraph.mapStaticSorted.Sorted.empty() || !graph.RGraph.mapDynamicSorted.Sorted.empty();
            PrepareShadowPackets(graph, context, job->worlds);
        }
    }
    RImplementation.phase = saved_phase;
    RCache.InvalidateAfterCommandList(); // Owner texture advancement touched SRV caches.
    for (u32 lane = 0; lane < State::lanes; ++lane)
        if (state->parallel) state->workers[lane].Run([this, lane](RenderContext& context) { state->Draw(lane, context); });
        else
        {
            RenderContext::Scope scope(state->workers[lane].Context());
            state->Draw(lane, state->workers[lane].Context());
        }
    state->recording = true;
}

bool LocalLightShadows::RenderShadows(light_Package& package)
{
    if (state->package != &package) return false;
    if (!state->recording) Record();
    {
        PROF_EVENT("Render.LocalShadows.Wait");
        for (auto& worker : state->workers)
            if (!worker.Wait())
            {
                Msg("! Local shadows: recording task failed; restoring owner submission");
                state->supported = false;
            }
    }
    state->recording = false;
    const auto saved_phase = RImplementation.phase;
    RImplementation.phase = CRender::PHASE_SMAP;
    RImplementation.stats.ls_shadowed_in += state->rejected;
    RImplementation.stats.ls_shadowed_peak_in = _max(RImplementation.stats.ls_shadowed_peak_in,
        u32(package.v_shadowed.size()) + state->rejected);
    RImplementation.stats.ls_shadowed_invisible_skipped += state->rejected;
    RImplementation.stats.ls_shadowed_after_vis += u32(package.v_shadowed.size());
    RImplementation.stats.ls_shadowed_peak_after_vis = _max(RImplementation.stats.ls_shadowed_peak_after_vis,
        u32(package.v_shadowed.size()));
    for (auto& atlas : state->atlases)
    {
        const bool recorded = state->supported && atlas.valid && atlas.commands;
        {
            PROF_EVENT("Render.LocalShadows.Submit");
            if (recorded)
            {
                HW.pContext->ExecuteCommandList(atlas.commands.Get(), FALSE);
                MergeStatistics(atlas.statistics);
            }
            else HW.pContext->ClearDepthStencilView(state->maps[atlas.lane].Depth(), D3D11_CLEAR_DEPTH, 1.f, 0);
            RCache.shadow_texture_source = RImplementation.Target->rt_smap_depth->pTexture._get();
            RCache.shadow_texture_view = state->maps[atlas.lane].View();
            RCache.InvalidateAfterCommandList();
        }
        for (auto* job : atlas.lights)
        {
            auto& item = *job->source;
            auto& graph = item.GMLight;
            if (recorded && job->cache && job->cache->selected != no_index)
                job->cache->visibility.pending = job->cache->selected;
            if (job->deferred || job->forward)
            {
                SetShadowView(item, state->maps[atlas.lane].Depth());
                graph.shadow_execution = recorded ? 2 : 0;
                graph.r_dsgraph_render_static(0, false);
                graph.r_dsgraph_render_dynamic(0, true);
                graph.shadow_execution = 0;
                if (RImplementation.Details && RImplementation.Details->dtFS &&
                    ps_ssfx_grass_shadows.x >= 3 && psDeviceFlags2.test(rsGrassShadow) &&
                    item.vis.distance <= ps_ssfx_grass_shadows.z && item.flags.bShadow && !job->decorative)
                {
                    u32 planes = 0xff;
                    if (RImplementation.ViewBase.testSphere(item.position, item.range * .6f, planes))
                    {
                        // Same as the owner path in lights_render.cpp (grass rework, on the owner)
                        RImplementation.Details->fade_distance = -1; // light position drives the fade
                        RImplementation.Details->light_position.set(item.position);
                        RImplementation.Details->hw_Render(&item);
                    }
                }
                item.X.S.transluent = FALSE;
                if (job->forward)
                {
                    item.X.S.transluent = TRUE;
                    RImplementation.Target->phase_smap_spot_tsh(&item);
                    graph.r_dsgraph_render_static(1, false); graph.r_dsgraph_render_dynamic(1, true);
                    graph.r_dsgraph_render_sorted();
                }
            }
        }
        // A lane's atlas may only be overwritten by the NEXT command list after
        // every light sampling its current contents has accumulated.
        for (auto* job : atlas.lights)
        {
            auto& item = *job->source;
            if (!job->deferred && !job->forward && !(item.flags.bVolumetric && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS))) continue;
            ++RImplementation.stats.ls_shadowed_rendered;
            RImplementation.Target->accum_spot(&item);
            RImplementation.render_indirect(&item);
            if (item.flags.bVolumetric && RImplementation.o.advancedpp && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS))
            {
                if (RImplementation.o.ssfx_volumetric)
                    RImplementation.Target->set_viewport_size(HW.pContext, float(Device.dwWidth) / RImplementation.o.volsize,
                        float(Device.dwHeight) / RImplementation.o.volsize);
                if (ps_pfx_volumetric_mode == 1) RImplementation.Target->accum_volumetric_lv(&item);
                else RImplementation.Target->accum_volumetric(&item);
                if (RImplementation.o.ssfx_volumetric)
                    RImplementation.Target->set_viewport_size(HW.pContext, float(Device.dwWidth), float(Device.dwHeight));
            }
        }
        atlas.commands.Reset();
    }
    RCache.shadow_texture_source = nullptr; RCache.shadow_texture_view = nullptr;
    RCache.InvalidateAfterCommandList();
    RImplementation.phase = state->atlases.empty() ? saved_phase : CRender::PHASE_SMAP;
    package.v_shadowed.clear();
    state->ready = true;
    return true;
}

void LocalLightShadows::EndFrame()
{
    for (auto& worker : state->workers) worker.Wait();
    if (!state->ready)
        for (auto& job : state->lights)
        {
            job->source->GMLight.RGraph.clear_dynamic<false>();
            job->source->GMLight.shadow_execution = job->source->GMLight.shadow_static_subset = 0;
        }
    state->querying = state->recording = state->ready = false;
    state->package = nullptr;
    state->atlases.clear(); state->lights.clear();
}

void LocalLightShadows::Forget(light* source)
{
    if (std::any_of(state->lights.begin(), state->lights.end(),
        [source](const std::unique_ptr<LightShadowJob>& job) { return job->source == source; })) EndFrame();
    state->caches.erase(source);
}

void LocalLightShadows::Clear()
{
    EndFrame();
    state = std::make_unique<State>();
}

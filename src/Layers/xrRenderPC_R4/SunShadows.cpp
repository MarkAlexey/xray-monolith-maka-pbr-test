#include "stdafx.h"
#include "SunShadows.h"
#include "RenderContext.h"
#include "../xrRender/FTreeVisual.h"
#include "../xrRenderDX10/dx10ConstantBuffer.h"
#include <wrl/client.h>
#include <mutex>
#include <condition_variable>

using Microsoft::WRL::ComPtr;

namespace
{
void SetSunShadowView(const Fmatrix& projection, ID3D11DepthStencilView* depth)
{
    for (u32 i = 0; i < 4; ++i) RCache.set_RT(nullptr, i);
    RCache.set_ZB(depth);
    const float size = float(RImplementation.o.smapsize);
    const D3D11_VIEWPORT viewport{0.f, 0.f, size, size, 0.f, 1.f};
    RenderDeviceContext()->RSSetViewports(1, &viewport);
    RCache.set_Stencil(FALSE);
    RCache.set_CullMode(CULL_CCW);
    RCache.set_ColorWriteEnable(FALSE);
    RCache.set_xform_world(Fidentity);
    RCache.set_xform_view(Fidentity);
    RCache.set_xform_project(projection);
}
}

struct SunShadows::State
{
    static constexpr u32 count = 3;
    RenderTask workers[count];
    ShadowMap maps[count];
    bool csm[count]{}; // cascade records into a CSM array slice (no private map, no SRV redirect)
    ComPtr<ID3D11CommandList> commands[count];
    ComPtr<ID3D11CommandList> static_commands[count];
    CBackend::_stats statistics[count]{};
    CBackend::_stats static_statistics[count]{};
    ShadowFallback fallback[count];
    std::vector<Fmatrix> worlds[count];
    xr_vector<ISpatialShared> candidates[count];
    CRender* renderer = nullptr;
    u64 revision = 0;
    bool initialized = false, supported = false, prepared = false, consumed = false;
    bool capturing[count]{}, recording[count]{};
    bool early_static[count]{}, static_valid[count]{};
    bool grass_ordered = false;
    bool valid[count]{}, grass[count]{};
    std::mutex grass_mutex;
    std::condition_variable grass_condition;
    u32 next_grass = 0;
    bool cancel_grass = false;

    bool Initialize()
    {
        if (initialized) return supported;
        initialized = true;
        const char* names[count] = {"Render.SunShadows.0", "Render.SunShadows.1", "Render.SunShadows.2"};
        // Monolith CSM (r4_sun_csm): the near cascades record straight into their slice of the
        // shadow-map array, which accum_direct_csm samples; the far cascade into rt_smap_depth.
        // Without CSM, cascades 0/1 get private maps (qkff99), the far one rt_smap_depth.
        // The slice views belong to the render target: reset_begin() clears this state before
        // the target is rebuilt, so they are never kept past a vid_restart.
        auto* target = RImplementation.Target;
        const u32 slices = RImplementation.o.sun_csm && target ? _min(target->csm_slices(), count - 1) : 0;
        for (u32 i = 0; i < count; ++i)
        {
            csm[i] = i < slices;
            const bool map_ready = csm[i] ? maps[i].InitializeDepth(target->rt_smap_csm_dsv[i]) : maps[i].Initialize(i == count - 1);
            if (!workers[i].Start(5 + i, names[i]) || !map_ready)
            {
                Msg("! Sun shadows: context/atlas creation failed; keeping original renderer");
                return false;
            }
        }
        supported = true;
        Msg("* Sun shadows: three complete recording backends ready");
        return true;
    }

    void FinishGrass(u32 index, bool draw)
    {
        if (!grass_ordered) return;
        std::unique_lock<std::mutex> lock(grass_mutex);
        if (!cancel_grass && next_grass < index)
        {
            PROF_EVENT("Render.SunShadows.GrassWait");
            grass_condition.wait(lock, [this, index]() { return cancel_grass || next_grass >= index; });
        }
        if (cancel_grass || next_grass != index) return;
        // Preserve the original order of the shared alpha/fade updates. Only
        // this graphics-only part is ordered; model recording stays parallel.
        try
        {
            // Never true here: CDetailManager::CanRecordShadow() is false, grass is drawn by
            // the owner after Bind() (R_sun.cpp). Kept so qkff99's ordering stays intact.
            (void)draw;
        }
        catch (...)
        {
            ++next_grass;
            grass_condition.notify_all();
            throw;
        }
        ++next_grass;
        grass_condition.notify_all();
    }

    void CancelGrass()
    {
        std::lock_guard<std::mutex> lock(grass_mutex);
        cancel_grass = true;
        grass_condition.notify_all();
    }

    void RestoreOwner()
    {
        CancelGrass();
        for (auto& worker : workers) worker.Wait();
        for (auto& cascade : renderer->m_sun_cascades)
        {
            cascade.GMCascade.shadow_execution = 0;
            cascade.GMCascade.RGraph.clear_static<false>();
            cascade.GMCascade.RGraph.clear_dynamic<false>();
        }
        supported = prepared = false;
        Msg("! Sun shadows: preparation failed; restoring original renderer");
    }

    void Draw(u32 index, RenderContext& context)
    {
        PROF_EVENT("Render.SunShadows.Record");
        struct GrassCompletion
        {
            State& state;
            u32 index;
            ~GrassCompletion() { state.FinishGrass(index, false); }
        } completion{*this, index};
        auto& cascade = renderer->m_sun_cascades[index];
        context.Begin(CRender::PHASE_SMAP);
        const bool split = early_static[index];
        if (!split) context.DeviceContext()->ClearDepthStencilView(maps[index].Depth(), D3D11_CLEAR_DEPTH, 1.f, 0);
        SetSunShadowView(cascade.cull_xform, maps[index].Depth());
        FTreeVisual::ResetShadowStats();
        dx10ConstantBuffer::ResetStats();
        cascade.GMCascade.shadow_execution = 1;
        if (!split) cascade.GMCascade.r_dsgraph_render_static(0, false);
        cascade.GMCascade.r_dsgraph_render_dynamic(0, false);
        cascade.GMCascade.shadow_execution = 0;
        FinishGrass(index, true);
        valid[index] = SUCCEEDED(context.Finish(commands[index].GetAddressOf()));
        statistics[index] = context.Backend().stat;
        PROF_TAG("Draws", statistics[index].calls);
        FTreeVisual::ProfileShadowStats();
        dx10ConstantBuffer::ProfileStats();
    }
};

SunShadows::SunShadows() : state(std::make_unique<State>()) {}
SunShadows::~SunShadows() { EndFrame(); }

bool SunShadows::Prepare(CRender& renderer)
{
    EndFrame();
    if (!ps_r4_mt_sun_shadows || renderer.o.Tshadows || renderer.m_sun_cascades.size() != State::count ||
        !state->Initialize()) return false;
    PROF_EVENT("Render.SunShadows.Prepare");
    state->renderer = &renderer;
    auto& cascades = renderer.m_sun_cascades;
    const bool chain = cascades.back().reset_chain;
    if (renderer.Target->need_to_render_sunshafts()) cascades.back().reset_chain = true;
    Fmatrix full, inverse;
    full.mul(Device.mProject, Device.mView);
    D3DXMatrixInverse((D3DXMATRIX*)&inverse, nullptr, (D3DXMATRIX*)&full);
    for (u32 i = 0; i < State::count; ++i) renderer.prepare_sun_cascade(i, inverse);
    cascades.back().reset_chain = chain;
    FTreeVisual::PrepareFrame();
    for (u32 i = 0; i < State::count; ++i)
    {
        auto& graph = cascades[i].GMCascade;
        graph.RGraph.clear_static<false>();
        graph.RGraph.clear_dynamic<false>();
        state->workers[i].Context().Synchronize();
    }
    state->prepared = true;
    CapturePending();
    return true;
}

void SunShadows::CapturePending(bool details_prepared)
{
    if (!state->prepared) return;
    auto& renderer = *state->renderer;
    Fbox details;
    const bool bounds_ready = details_prepared || !renderer.Details || !psDeviceFlags.is(rsDetails) ||
        renderer.Details->TryGetVisibleBounds(details);
    u32 deferred = 0;
    for (u32 i = State::count; i-- > 0;)
    {
        if (state->capturing[i]) continue;
        if (!bounds_ready && renderer.sun_cascade_needs_receiver_bounds(i))
        {
            ++deferred;
            continue;
        }
        state->capturing[i] = true;
        state->workers[i].Run([this, i](RenderContext& context)
        {
            PROF_EVENT("Render.SunShadows.CaptureStatic");
            context.Backend().recording_phase = CRender::PHASE_SMAP;
            state->renderer->capture_sun_cascade(i, nullptr, 0, true);
        });
    }
    PROF_TAG("Sun captures awaiting detail bounds", deferred);
}

void SunShadows::RecordStatic()
{
    // The near cascade is already hidden; only split the measured middle/far work.
    RecordStatic(State::count - 1);
    RecordStatic(1);
}

void SunShadows::RecordStatic(u32 index)
{
    if (!state->prepared || state->early_static[index] || state->recording[index] ||
        !state->capturing[index] || !state->workers[index].Ready()) return;
    if (!state->workers[index].Wait())
    {
        state->RestoreOwner();
        return;
    }
    PROF_EVENT_DYNAMIC(index == State::count - 1 ? "Render.SunShadows.PrimeStaticFar" : "Render.SunShadows.PrimeStaticMiddle");
    auto& graph = state->renderer->m_sun_cascades[index].GMCascade;
    auto& context = state->workers[index].Context();
    state->fallback[index] = PrepareShadowPackets(graph, context, state->worlds[index]);
    bool native = false;
    for (const auto& queue : graph.RGraph.mapStaticPasses[0])
        for (const auto& packet : queue) native = native || packet.parallel_shadow;
    RCache.InvalidateAfterCommandList();
    if (!native) return;
    state->early_static[index] = true;
    state->workers[index].Run([this, index](RenderContext& active)
    {
        PROF_EVENT_DYNAMIC(index == State::count - 1 ? "Render.SunShadows.RecordStaticFar" : "Render.SunShadows.RecordStaticMiddle");
        auto& cascade = state->renderer->m_sun_cascades[index];
        active.Begin(CRender::PHASE_SMAP);
        active.DeviceContext()->ClearDepthStencilView(state->maps[index].Depth(), D3D11_CLEAR_DEPTH, 1.f, 0);
        SetSunShadowView(cascade.cull_xform, state->maps[index].Depth());
        FTreeVisual::ResetShadowStats();
        dx10ConstantBuffer::ResetStats();
        cascade.GMCascade.shadow_execution = 1;
        cascade.GMCascade.r_dsgraph_render_static(0, false);
        cascade.GMCascade.shadow_execution = 0;
        state->static_valid[index] = SUCCEEDED(active.Finish(state->static_commands[index].GetAddressOf()));
        state->static_statistics[index] = active.Backend().stat;
        PROF_TAG("Draws", state->static_statistics[index].calls);
        FTreeVisual::ProfileShadowStats();
        dx10ConstantBuffer::ProfileStats();
    });
}

void SunShadows::Record()
{
    if (!state->prepared || state->recording[0]) return;
    CapturePending(true);
    auto& renderer = *state->renderer;
    struct CancelOnFailure
    {
        State& state;
        bool complete = false;
        ~CancelOnFailure() { if (!complete) state.RestoreOwner(); }
    } completion{*state};
    PROF_EVENT("Render.SunShadows.CaptureAndPrime");
    auto& cascades = renderer.m_sun_cascades;
    state->revision = 0;
    const bool compatible = cascades[0].GMCascade.i_doptions == cascades[1].GMCascade.i_doptions &&
        cascades[0].GMCascade.i_doptions == cascades[2].GMCascade.i_doptions;
    if (compatible)
    {
        const CFrustum* frustums[State::count]{};
        for (u32 i = 0; i < State::count; ++i) frustums[i] = &cascades[i].cull_frustum;
        state->revision = g_SpatialSpace->q_frustums(state->candidates, cascades[0].GMCascade.i_doptions, frustums);
    }
    // Preserve renderable/Lua callback order: local lights, then cascades 0/1/2.
    // Each completed cascade starts recording before the next one is primed.
    for (u32 i = 0; i < State::count; ++i)
    {
        bool success;
        {
            PROF_EVENT("Render.SunShadows.CaptureWait");
            success = state->workers[i].Wait();
        }
        if (!success || (state->early_static[i] && !state->static_valid[i])) return;
        auto& graph = renderer.m_sun_cascades[i].GMCascade;
        const auto saved_phase = renderer.phase;
        struct RestorePhase
        {
            u32& phase;
            u32 saved;
            ~RestorePhase() { phase = saved; }
        } phase_guard{renderer.phase, saved_phase};
        renderer.phase = CRender::PHASE_SMAP;
        graph.r_dsgraph_capture_dynamic(nullptr, state->revision ? &state->candidates[i] : nullptr, state->revision);
        auto& context = state->workers[i].Context();
        auto fallback = PrepareShadowPackets(graph, context, state->worlds[i], !state->early_static[i]);
        if (state->early_static[i]) fallback.static_packets = state->fallback[i].static_packets;
        state->fallback[i] = fallback;
        const bool geometry = !graph.RGraph.mapStaticPasses[0][0].empty() || !graph.RGraph.mapDynamicPasses[0][0].empty();
        // Validate one common grass policy before alpha advancement. Prime a
        // context only after its own task has joined (far static may be busy).
        if (!i)
        {
            const bool safe = renderer.Details && renderer.Details->dtFS && psDeviceFlags2.test(rsGrassShadow) &&
                renderer.Details->CanRecordShadow();
            for (u32 c = 0; c < State::count; ++c) state->grass[c] = safe && c <= ps_ssfx_grass_shadows.x;
            state->grass_ordered = std::any_of(std::begin(state->grass), std::end(state->grass), [](bool value) { return value; });
        }
        state->grass[i] = state->grass[i] && geometry;
        if (state->grass[i] && !renderer.Details->PrimeShadow(context)) return;
        RCache.InvalidateAfterCommandList();
        state->workers[i].Run([this, i](RenderContext& active) { state->Draw(i, active); });
        state->recording[i] = true;
    }
    completion.complete = true;
}

bool SunShadows::Prepared() const { return state->prepared; }

bool SunShadows::Bind(u32 index)
{
    if (!state->prepared || index >= State::count) return false;
    Record();
    if (!state->prepared) return false;
    bool success;
    {
        PROF_EVENT("Render.SunShadows.Wait");
        success = state->workers[index].Wait() && state->valid[index] && state->commands[index] &&
            (!state->early_static[index] || (state->static_valid[index] && state->static_commands[index]));
    }
    PROF_EVENT("Render.SunShadows.Submit");
    if (success)
    {
        if (state->early_static[index])
        {
            HW.pContext->ExecuteCommandList(state->static_commands[index].Get(), FALSE);
            MergeStatistics(state->static_statistics[index]);
        }
        HW.pContext->ExecuteCommandList(state->commands[index].Get(), FALSE);
        MergeStatistics(state->statistics[index]);
        // Priority 0 is already drawn. Keep only actual owner fallback, and
        // preserve the priority-1/sorted queues for translucent shadows.
        auto& queues = state->renderer->m_sun_cascades[index].GMCascade.RGraph;
        if (!state->fallback[index].static_packets) queues.clear_graph<false>(queues.mapStaticPasses, 0);
        if (!state->fallback[index].dynamic_packets) queues.clear_graph<false>(queues.mapDynamicPasses, 0);
        PROF_TAG("Sun skipped static scan", state->fallback[index].static_packets == 0 ? 1 : 0);
        PROF_TAG("Sun skipped dynamic scan", state->fallback[index].dynamic_packets == 0 ? 1 : 0);
    }
    else
    {
        Msg("! Sun shadows: recording failed; restoring owner submission");
        HW.pContext->ClearDepthStencilView(state->maps[index].Depth(), D3D11_CLEAR_DEPTH, 1.f, 0);
        state->grass[index] = false;
    }
    if (state->csm[index])
    {
        RCache.shadow_texture_source = nullptr;
        RCache.shadow_texture_view = nullptr;
    }
    else
    {
        RCache.shadow_texture_source = RImplementation.Target->rt_smap_depth->pTexture._get();
        RCache.shadow_texture_view = state->maps[index].View();
    }
    RCache.InvalidateAfterCommandList();
    // Same targets as phase_smap_direct / phase_smap_csm (the colour surface is what a
    // translucent-shadow pass clears), minus their clear: the list already cleared the depth
    RImplementation.Target->u_setrt(RImplementation.Target->rt_smap_surf, nullptr, nullptr, state->maps[index].Depth());
    SetSunShadowView(state->renderer->m_sun_cascades[index].cull_xform, state->maps[index].Depth());
    state->renderer->m_sun_cascades[index].GMCascade.shadow_execution = success ? 2 : 0;
    state->commands[index].Reset();
    state->consumed = index == State::count - 1;
    return true;
}

bool SunShadows::GrassRecorded(u32 index) const { return state->prepared && state->grass[index]; }

void SunShadows::EndFrame()
{
    state->CancelGrass();
    for (auto& worker : state->workers) worker.Wait();
    if (state->renderer)
        for (auto& cascade : state->renderer->m_sun_cascades)
        {
            cascade.GMCascade.shadow_execution = 0;
            if (!state->consumed)
            {
                cascade.GMCascade.RGraph.clear_static<false>();
                cascade.GMCascade.RGraph.clear_dynamic<false>();
            }
        }
    for (u32 i = 0; i < State::count; ++i)
    {
        state->commands[i].Reset();
        state->worlds[i].clear();
        state->candidates[i].clear();
        state->valid[i] = state->grass[i] = false;
        state->capturing[i] = state->recording[i] = false;
        state->static_commands[i].Reset();
        state->early_static[i] = state->static_valid[i] = false;
        state->fallback[i] = {};
    }
    state->grass_ordered = false;
    state->next_grass = 0;
    state->cancel_grass = false;
    state->renderer = nullptr;
    state->prepared = state->consumed = false;
}

void SunShadows::Clear()
{
    EndFrame();
    state = std::make_unique<State>();
}

struct RainShadows::State
{
    RenderTask worker;
    ShadowMap map;
    ComPtr<ID3D11CommandList> commands;
    CBackend::_stats statistics{};
    std::vector<Fmatrix> worlds;
    CRender* renderer = nullptr;
    bool initialized = false, supported = false, prepared = false, valid = false, consumed = false;
};

RainShadows::RainShadows() : state(std::make_unique<State>()) {}
RainShadows::~RainShadows() { EndFrame(); }

bool RainShadows::Prepare(CRender& renderer)
{
    EndFrame();
    if (!ps_r4_mt_rain_shadows || renderer.o.Tshadows || !renderer.prepare_rain_view()) return false;
    PROF_EVENT("Render.RainShadows.Prepare");
    if (!state->initialized)
    {
        state->initialized = true;
        state->supported = state->worker.Start(8, "Render.RainShadows") && state->map.Initialize(true);
        if (!state->supported) Msg("! Rain shadows: context creation failed; keeping original renderer");
    }
    if (!state->supported) return false;
    state->renderer = &renderer;
    auto& graph = renderer.GMRainWet;
    const auto phase = renderer.phase;
    renderer.phase = CRender::PHASE_SMAP;
    graph.RGraph.clear_static<false>();
    graph.RGraph.clear_dynamic<false>();
    graph.traverse(renderer.pOutdoorSector, renderer.rainwet_cull_frustum,
        renderer.rainwet_cull_COP, renderer.rainwet_cull_xform);
    graph.r_dsgraph_capture_static();
    renderer.phase = phase;
    auto& context = state->worker.Context();
    context.Synchronize();
    FTreeVisual::PrepareFrame();
    PrepareShadowPackets(graph, context, state->worlds);
    RCache.InvalidateAfterCommandList();
    state->worker.Run([this](RenderContext& context)
    {
        PROF_EVENT("Render.RainShadows.Record");
        auto& renderer = *state->renderer;
        auto& graph = renderer.GMRainWet;
        context.Begin(CRender::PHASE_SMAP);
        context.DeviceContext()->ClearDepthStencilView(state->map.Depth(), D3D11_CLEAR_DEPTH, 1.f, 0);
        SetSunShadowView(renderer.rainwet_cull_xform, state->map.Depth());
        const auto& light = *renderer.Lights.rain_light;
        const D3D11_VIEWPORT viewport{float(light.X.D.minX), float(light.X.D.minY),
            float(light.X.D.maxX - light.X.D.minX), float(light.X.D.maxY - light.X.D.minY), 0.f, 1.f};
        context.DeviceContext()->RSSetViewports(1, &viewport);
        graph.shadow_execution = 1;
        graph.r_dsgraph_render_static(0, false);
        graph.shadow_execution = 0;
        state->valid = SUCCEEDED(context.Finish(state->commands.GetAddressOf()));
        state->statistics = context.Backend().stat;
        PROF_TAG("Draws", state->statistics.calls);
    });
    state->prepared = true;
    return true;
}

bool RainShadows::Prepared() const { return state->prepared; }

bool RainShadows::Bind()
{
    if (!state->prepared) return false;
    bool success;
    {
        PROF_EVENT("Render.RainShadows.Wait");
        success = state->worker.Wait() && state->valid && state->commands;
    }
    if (!success)
    {
        state->renderer->GMRainWet.shadow_execution = 0;
        state->consumed = true;
        return false;
    }
    {
        PROF_EVENT("Render.RainShadows.Submit");
        HW.pContext->ExecuteCommandList(state->commands.Get(), FALSE);
        MergeStatistics(state->statistics);
        RCache.InvalidateAfterCommandList();
        RImplementation.Target->u_setrt(RImplementation.o.smapsize, RImplementation.o.smapsize,
            nullptr, nullptr, nullptr, state->map.Depth());
        SetSunShadowView(state->renderer->rainwet_cull_xform, state->map.Depth());
        const auto& light = *state->renderer->Lights.rain_light;
        const D3D11_VIEWPORT viewport{float(light.X.D.minX), float(light.X.D.minY),
            float(light.X.D.maxX - light.X.D.minX), float(light.X.D.maxY - light.X.D.minY), 0.f, 1.f};
        HW.pContext->RSSetViewports(1, &viewport);
        state->renderer->GMRainWet.shadow_execution = 2;
    }
    state->commands.Reset();
    state->consumed = true;
    return true;
}

void RainShadows::EndFrame()
{
    state->worker.Wait();
    if (state->renderer)
    {
        state->renderer->GMRainWet.shadow_execution = 0;
        if (!state->consumed) state->renderer->GMRainWet.RGraph.clear_static<false>();
    }
    state->commands.Reset();
    state->worlds.clear();
    state->renderer = nullptr;
    state->prepared = state->valid = state->consumed = false;
}

void RainShadows::Clear()
{
    EndFrame();
    state = std::make_unique<State>();
}

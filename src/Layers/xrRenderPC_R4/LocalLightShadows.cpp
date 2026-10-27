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

//////////////////////////////////////////////////////////////////////////////////////////////
// Monolith: spot shadow cache (r4_spot_shadow_cache 1)
//
// Rewrite of IX-Ray's spot light shadow cache (ixray-1.6-stcop 6eb13153, "Spotlight Shadow
// cashe") for Monolith's recorded local shadows.
//
// A still light (its static caster graph frozen: m_moving_frames >= 32, not decorative, not HUD)
// keeps the depth of its immutable static casters in a texture of its own, the size of its atlas
// tile. Every frame the stored depth is copied into the light's tile (a full-tile draw writing
// SV_Depth, since D3D11 cannot copy into part of a depth texture) and only the other casters are
// drawn on top: alpha-tested and tree statics, dynamic objects, and anything the owner draws
// (fallback packets, grass, translucent casters).
//
// Which casters are stored: the packets PrepareCache marks local_shadow_eligible (immutable level
// geometry, MT_NORMAL, a pure depth pass: the vertex shader reads only m_WVP, no textures) that
// record on the lanes (parallel_shadow). The same split smapvis already uses
// (shadow_static_subset 1 / 2), so stored and per-frame casters never overlap.
//
// The stored depth is rebuilt when the light's transform or tile size changes (moving lights, the
// tile size following the camera distance), after the two have been unchanged for two frames,
// and whenever the ShadowCache itself is rebuilt (static graph or shadow shaders changed).
// Textures are kept under r4_spot_shadow_cache_mb, least recently used first.
//
// Differences from IX-Ray:
//   - Only depth-only casters are stored (IX-Ray also stored alpha-tested ones and tracked texture
//     streaming for them); alpha-tested statics stay per-frame.
//   - The classification is Monolith's existing one (shader reflection, immutable geometry), no
//     shader-name matching; no separate dsgraph build pass.
//   - The occlusion queries of smapvis are off for a light while its depth is stored (its stored
//     casters are not drawn, so there is nothing to test).
//
// Requirements / caveats:
//   - Only on the recorded path (r4_mt_local_shadows 1 or r4_smapvis 1); the owner path in
//     lights_render.cpp renders as before.
//   - Memory: size^2 x 4 bytes per light (4 MB for a 1024 tile).
//   - A level object the engine treats as immutable geometry but that changes anyway would keep
//     its old shadow until the light's cache is rebuilt.
//////////////////////////////////////////////////////////////////////////////////////////////
enum : u8 { depth_none = 0, depth_build, depth_restore };

struct StoredDepth
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11ShaderResourceView> view;
    Fmatrix transform{};
    u32 size = 0;           // tile size the texture was made for
    u32 stable = 0;         // frames transform and size have been unchanged without stored depth
    u32 used = 0;           // last frame it was selected
    u64 bytes = 0;
    bool valid = false;     // the texture holds the stored casters for transform / size
    bool active = false;    // the light currently uses the cache (smapvis is off for it)

    void Release()
    {
        if (texture) HW.stats_manager.decrement_stats_rtarget(texture.Get());
        texture.Reset(); depth.Reset(); view.Reset();
        bytes = 0;
        valid = false;
    }
    ~StoredDepth() { Release(); }
};

// Copies a stored depth texture into a tile: full-tile triangle, SV_Depth from Load, depth test
// ALWAYS. Raw D3D11 on the recording context; the caller resets the backend bindings after.
struct DepthCopy
{
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> rasterizer;
    bool created = false, failed = false;

    bool Ready()
    {
        if (created || failed) return created;
        static const char source[] =
            "Texture2D<float> s_stored : register(t0);\n"
            "struct v2p { float4 hpos : SV_Position; float2 tc : TEXCOORD0; };\n"
            "v2p vs_main(uint id : SV_VertexID)\n"
            "{\n"
            "    v2p o;\n"
            "    o.tc = float2((id << 1) & 2, id & 2);\n"
            "    o.hpos = float4(o.tc * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
            "    return o;\n"
            "}\n"
            "float ps_main(v2p i) : SV_Depth\n"
            "{\n"
            "    uint w, h;\n"
            "    s_stored.GetDimensions(w, h);\n"
            "    return s_stored.Load(int3(int2(i.tc * float2(w, h)), 0));\n"
            "}\n";
        failed = true;
        ComPtr<ID3DBlob> vs_code, ps_code, errors;
        if (FAILED(D3DCompile(source, sizeof(source) - 1, "spot_shadow_cache_copy", nullptr, nullptr, "vs_main", "vs_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vs_code.GetAddressOf(), errors.ReleaseAndGetAddressOf())) ||
            FAILED(D3DCompile(source, sizeof(source) - 1, "spot_shadow_cache_copy", nullptr, nullptr, "ps_main", "ps_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ps_code.GetAddressOf(), errors.ReleaseAndGetAddressOf())))
        {
            Msg("! Spot shadow cache: copy shader failed to compile: %s",
                errors ? (const char*)errors->GetBufferPointer() : "no message");
            return false;
        }
        D3D11_DEPTH_STENCIL_DESC depth_desc{};
        depth_desc.DepthEnable = TRUE;
        depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        D3D11_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D11_FILL_SOLID;
        raster_desc.CullMode = D3D11_CULL_NONE;
        raster_desc.DepthClipEnable = TRUE;
        if (FAILED(HW.pDevice->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, vs.GetAddressOf())) ||
            FAILED(HW.pDevice->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, ps.GetAddressOf())) ||
            FAILED(HW.pDevice->CreateDepthStencilState(&depth_desc, depth.GetAddressOf())) ||
            FAILED(HW.pDevice->CreateRasterizerState(&raster_desc, rasterizer.GetAddressOf())))
        {
            Msg("! Spot shadow cache: copy pipeline creation failed");
            return false;
        }
        failed = false;
        created = true;
        return true;
    }

    void Draw(ID3D11DeviceContext* context, ID3D11DepthStencilView* target, ID3D11ShaderResourceView* stored,
        const D3D11_VIEWPORT& tile) const
    {
        // Binding the tile's map as the only output also unbinds the stored texture's DSV
        context->OMSetRenderTargets(0, nullptr, target);
        context->OMSetDepthStencilState(depth.Get(), 0);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->RSSetState(rasterizer.Get());
        context->RSSetViewports(1, &tile);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &stored);
        context->Draw(3, 0);
        ID3D11ShaderResourceView* none = nullptr;
        context->PSSetShaderResources(0, 1, &none);
    }
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
    StoredDepth stored; // spot shadow cache

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
    std::vector<const SPass*> passes;
    u64 revision = u64(-1);
    ShadowFallback fallback;
    RecordedLight illumination;
    bool decorative = false, deferred = false, forward = false, grass = false, accumulate = false;
    u8 depth = depth_none; // spot shadow cache: build / restore the stored static depth
};

struct ShadowAtlasJob
{
    u32 lane = 0;
    std::vector<LightShadowJob*> lights;
    bool accumulate = false;
    u64 work = 1;
};

struct LightBlock
{
    size_t first = 0, end = 0;
    u32 lane = 0, marker = 5, visible = 0;
    bool accumulate = false, unshadowed = false, valid = false, ready = false;
    ComPtr<ID3D11CommandList> commands;
    CBackend::_stats statistics{};
};

struct UnshadowedLight
{
    light* source;
    RecordedLight illumination;
    bool point;
};


// origin: the viewport starts at 0,0 (a light's own stored-depth texture) instead of its tile
void SetShadowView(light& source, ID3D11DepthStencilView* depth, bool origin = false)
{
    RCache.set_RT(nullptr, 0); RCache.set_RT(nullptr, 1);
    RCache.set_RT(nullptr, 2); RCache.set_RT(nullptr, 3);
    RCache.set_ZB(depth);
    D3D11_VIEWPORT view{origin ? 0.f : float(source.X.S.posX), origin ? 0.f : float(source.X.S.posY),
        float(source.X.S.size), float(source.X.S.size), 0.f, 1.f};
    RenderDeviceContext()->RSSetViewports(1, &view);
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable(FALSE);
    RCache.set_xform_world(Fidentity);
    RCache.set_xform_view(source.X.S.view);
    RCache.set_xform_project(source.X.S.project);
}

void RecordLight(LightShadowJob& job, ID3D11DepthStencilView* depth, const DepthCopy& copy)
{
    auto& graph = job.source->GMLight;
    struct GraphScope
    {
        CDSGraphManager& graph;
        ~GraphScope() { graph.shadow_execution = graph.shadow_static_subset = 0; }
    } graph_scope{graph};
    graph.shadow_execution = 1;
    if (job.depth != depth_none && job.cache)
    {
        // Spot shadow cache: stored casters come from the light's own texture (built first when
        // needed), the rest is drawn on top as usual
        auto& stored = job.cache->stored;
        if (job.depth == depth_build)
        {
            RenderDeviceContext()->ClearDepthStencilView(stored.depth.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
            SetShadowView(*job.source, stored.depth.Get(), true);
            graph.shadow_static_subset = 1;
            graph.r_dsgraph_render_static(0, false);
        }
        const D3D11_VIEWPORT tile{float(job.source->X.S.posX), float(job.source->X.S.posY),
            float(job.source->X.S.size), float(job.source->X.S.size), 0.f, 1.f};
        copy.Draw(RenderDeviceContext(), depth, stored.view.Get(), tile);
        RCache.ResetBindings();
        SetShadowView(*job.source, depth);
        graph.shadow_static_subset = 2;
        graph.r_dsgraph_render_static(0, false);
        graph.shadow_static_subset = 0;
        graph.r_dsgraph_render_dynamic(0, false);
        return;
    }
    SetShadowView(*job.source, depth);
    // Learn occlusion only against immutable opaque depth. The original graph
    // renderer still selects complete native shadow batches for this subset.
    if (job.cache && job.cache->selected != no_index)
    {
        graph.shadow_static_subset = 1;
        graph.r_dsgraph_render_static(0, false);
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
        graph.shadow_static_subset = 2;
    }
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
    std::vector<LightBlock> blocks;
    std::vector<UnshadowedLight> unshadowed;
    RenderTask workers[lanes];
    ShadowMap maps[lanes];
    std::mutex completion_mutex;
    std::condition_variable completion;
    bool finished[lanes]{};
    light_Package* package = nullptr;
    bool initialized = false, supported = false, querying = false, recording = false, ready = false;
    bool visibility = true, parallel = true;
    u32 rejected = 0;

    // Spot shadow cache (see StoredDepth)
    DepthCopy copy;
    struct
    {
        u32 frames = 0, restores = 0, builds = 0, waiting = 0, over_budget = 0, failures = 0;
    } stored_stats;

    u64 StoredBytes() const
    {
        u64 bytes = 0;
        for (const auto& cache : caches) bytes += cache.second->stored.bytes;
        return bytes;
    }

    u8 SelectDepth(light& item, ShadowCache& cache)
    {
        auto& stored = cache.stored;
        if (!ps_r4_spot_shadow_cache || cache.draws.empty() || !copy.Ready())
        {
            stored.Release();
            stored.stable = 0;
            return depth_none;
        }
        const u32 size = item.X.S.size;
        if (!size || stored.size != size || memcmp(&stored.transform, &item.X.S.combine, sizeof(Fmatrix)))
        {
            // Moved, or the tile size followed the camera distance: wait until both are stable
            stored.valid = false;
            stored.stable = 0;
            stored.size = size;
            stored.transform = item.X.S.combine;
            ++stored_stats.waiting;
            return depth_none;
        }
        stored.used = Device.dwFrame;
        if (stored.valid && stored.texture)
        {
            ++stored_stats.restores;
            return depth_restore;
        }
        if (++stored.stable < 2)
        {
            ++stored_stats.waiting;
            return depth_none;
        }
        if (!CreateTexture(cache)) return depth_none;
        ++stored_stats.builds;
        return depth_build;
    }

    // Texture for a light's stored depth, inside r4_spot_shadow_cache_mb (the least recently
    // used textures of other lights are released first)
    bool CreateTexture(ShadowCache& owner)
    {
        auto& stored = owner.stored;
        const auto& source = RImplementation.Target->rt_smap_depth;
        D3D11_TEXTURE2D_DESC description{};
        source->pSurface->GetDesc(&description);
        if (stored.texture)
        {
            D3D11_TEXTURE2D_DESC current{};
            stored.texture->GetDesc(&current);
            if (current.Width == stored.size && current.Format == description.Format) return true;
            stored.Release();
        }
        const u32 texel = description.Format == DXGI_FORMAT_R16_TYPELESS || description.Format == DXGI_FORMAT_D16_UNORM ? 2u : 4u;
        const u64 bytes = u64(stored.size) * stored.size * texel;
        const u64 budget = u64(ps_r4_spot_shadow_cache_mb) * 1024 * 1024;
        u64 used = StoredBytes();
        while (used + bytes > budget)
        {
            ShadowCache* oldest = nullptr;
            for (auto& cache : caches)
            {
                auto& other = cache.second->stored;
                if (cache.second.get() == &owner || !other.bytes || other.used == Device.dwFrame) continue;
                if (!oldest || other.used < oldest->stored.used) oldest = cache.second.get();
            }
            if (!oldest) break;
            used -= oldest->stored.bytes;
            oldest->stored.Release();
        }
        if (used + bytes > budget)
        {
            ++stored_stats.over_budget;
            return false;
        }
        description.Width = description.Height = stored.size;
        description.MipLevels = description.ArraySize = 1;
        description.SampleDesc = {1, 0};
        D3D11_DEPTH_STENCIL_VIEW_DESC depth{};
        source->pZRT->GetDesc(&depth);
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        source->pTexture->get_SRView()->GetDesc(&view);
        if (FAILED(HW.pDevice->CreateTexture2D(&description, nullptr, stored.texture.GetAddressOf())))
        {
            ++stored_stats.failures;
            return false;
        }
        HW.stats_manager.increment_stats_rtarget(stored.texture.Get());
        stored.bytes = bytes;
        if (FAILED(HW.pDevice->CreateDepthStencilView(stored.texture.Get(), &depth, stored.depth.GetAddressOf())) ||
            FAILED(HW.pDevice->CreateShaderResourceView(stored.texture.Get(), &view, stored.view.GetAddressOf())))
        {
            stored.Release();
            ++stored_stats.failures;
            return false;
        }
        return true;
    }

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

    void AssignLanes()
    {
        if (atlases.empty()) return;
        const bool merged = std::any_of(atlases.begin(), atlases.end(),
            [](const ShadowAtlasJob& atlas) { return atlas.accumulate; });
        const size_t used = std::min<size_t>(lanes, atlases.size());
        u64 total = 0, prefix = 0;
        for (const auto& atlas : atlases) total += atlas.work;
        size_t group = 0;
        for (size_t i = 0; i < atlases.size(); ++i)
        {
            if (!merged)
            {
                // Shadow-only lists need no contiguous ranges. Preserve the
                // original round-robin balance when accumulation cannot record.
                atlases[i].lane = u32((atlases.size() - 1 - i) % lanes);
                continue;
            }
            atlases[i].lane = u32(used - 1 - group);
            prefix += atlases[i].work;
            const size_t left = atlases.size() - i - 1, groups_left = used - group - 1;
            if (groups_left && left >= groups_left &&
                (prefix * used >= total * (group + 1) || left == groups_left)) ++group;
        }
    }

    void Draw(u32 lane, RenderContext& context)
    {
        PROF_EVENT("Render.LocalShadows.Record");
        struct Completion
        {
            State& state;
            u32 lane;
            ~Completion()
            {
                std::lock_guard<std::mutex> lock(state.completion_mutex);
                state.finished[lane] = true;
                state.completion.notify_all();
            }
        } done{*this, lane};
        FTreeVisual::ResetShadowStats();
        dx10ConstantBuffer::ResetStats();
        u32 calls = 0, hidden = 0, queries = 0, volumes = 0;
        for (auto& block : blocks)
        {
            if (block.lane != lane) continue;
            context.Begin(CRender::PHASE_SMAP);
            bool reset_marker = true;
            for (size_t i = block.first; i < block.end; ++i)
            {
                auto& atlas = atlases[i];
                // Lighting reads this lane's map before the next atlas reuses it.
                // Reset both API and software bindings after those SRV reads.
                context.DeviceContext()->ClearState();
                RCache.ResetBindings();
                RCache.recording_phase = CRender::PHASE_SMAP;
                context.DeviceContext()->ClearDepthStencilView(maps[lane].Depth(), D3D11_CLEAR_DEPTH, 1.f, 0);
                for (auto* job : atlas.lights)
                {
                    RecordLight(*job, maps[lane].Depth(), copy);
                    if (job->cache)
                    {
                        for (const auto bit : job->cache->visibility.hidden) hidden += bit != 0;
                        queries += job->cache->selected != no_index;
                    }
                }
                if (block.accumulate)
                {
                    PROF_EVENT("Render.LocalLights.Accumulate");
                    RCache.recording_phase = CRender::PHASE_NORMAL;
                    RCache.shadow_texture_source = RImplementation.Target->rt_smap_depth->pTexture._get();
                    RCache.shadow_texture_view = maps[lane].View();
                    RImplementation.Target->bind_recorded_accumulator();
                    if (reset_marker) RImplementation.Target->reset_light_marker(block.marker, true);
                    reset_marker = false;
                    for (auto* job : atlas.lights)
                    {
                        if (!job->accumulate) continue;
                        RImplementation.Target->accum_spot(job->illumination, block.marker);
                        ++block.visible;
                    }
                    RCache.shadow_texture_source = nullptr;
                    RCache.shadow_texture_view = nullptr;
                }
            }
            if (block.unshadowed)
            {
                PROF_EVENT("Render.LocalLights.Unshadowed");
                RCache.recording_phase = CRender::PHASE_NORMAL;
                RCache.shadow_texture_source = nullptr;
                RCache.shadow_texture_view = nullptr;
                RImplementation.Target->bind_recorded_accumulator();
                if (reset_marker) RImplementation.Target->reset_light_marker(block.marker, true);
                for (const auto& item : unshadowed)
                {
                    if (item.point) RImplementation.Target->accum_point(item.illumination, block.marker);
                    else RImplementation.Target->accum_spot(item.illumination, block.marker);
                    ++block.visible;
                }
            }
            block.valid = SUCCEEDED(context.Finish(block.commands.GetAddressOf()));
            block.statistics = context.Backend().stat;
            calls += block.statistics.calls;
            volumes += block.visible;
            // No access to this block after publication: owner may consume it
            // while this lane starts recording the next atlas range.
            {
                std::lock_guard<std::mutex> lock(completion_mutex);
                block.ready = true;
            }
            completion.notify_all();
        }
        PROF_TAG("Draws", calls);
        PROF_TAG("Recorded light volumes", volumes);
        PROF_TAG("Hidden casters", hidden);
        PROF_TAG("Queries", queries);
        FTreeVisual::ProfileShadowStats();
        dx10ConstantBuffer::ProfileStats();
    }

    bool WaitBlock(const LightBlock& block)
    {
        PROF_EVENT("Render.LocalShadows.Wait");
        std::unique_lock<std::mutex> lock(completion_mutex);
        completion.wait(lock, [this, &block]() { return block.ready || finished[block.lane]; });
        return block.ready;
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
                // Spot shadow cache. A light using it draws none of its stored casters, so smapvis
                // has nothing to query: its evidence is reset once and stays empty meanwhile
                job->depth = state->SelectDepth(item, *cache);
                if (job->depth != depth_none && !cache->stored.active) cache->ResetVisibility();
                cache->stored.active = job->depth != depth_none;
                if (state->visibility && !cache->draws.empty() && job->depth == depth_none)
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
            job->fallback = PrepareShadowPackets(graph, context, job->worlds, true, &job->passes);
            if (RImplementation.Details && RImplementation.Details->dtFS &&
                ps_ssfx_grass_shadows.x >= 3 && psDeviceFlags2.test(rsGrassShadow) &&
                item.vis.distance <= ps_ssfx_grass_shadows.z && item.flags.bShadow && !job->decorative)
            {
                u32 planes = 0xff;
                job->grass = RImplementation.ViewBase.testSphere(item.position, item.range * .6f, planes);
            }
            const bool illuminate = job->deferred || job->forward ||
                (item.flags.bVolumetric && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS));
            job->accumulate = illuminate && !job->forward && !job->grass &&
                !job->fallback.static_packets && !job->fallback.dynamic_packets &&
                RImplementation.Target->prepare_light_accumulation(job->illumination, item, false);
            if (job->accumulate && job->deferred) item.X.S.transluent = FALSE;
        }
    }
    // Only complete atlases can be illuminated before owner fallback. Unknown
    // cases split the run; they retain one shadow-only list and owner lighting.
    for (auto& atlas : state->atlases)
    {
        atlas.accumulate = std::all_of(atlas.lights.begin(), atlas.lights.end(), [](const LightShadowJob* job)
        {
            const bool illuminate = job->deferred || job->forward ||
                (job->source->flags.bVolumetric && ps_r2_ls_flags.test(R2FLAG_VOLUMETRIC_LIGHTS));
            return !illuminate || job->accumulate;
        });
        for (const auto* job : atlas.lights)
            atlas.work += job->fallback.recording_work + (atlas.accumulate && job->accumulate ? 3u : 0u);
    }
    state->AssignLanes();
    for (size_t i = 0; i < state->atlases.size(); ++i)
    {
        auto& atlas = state->atlases[i];
        if (state->blocks.empty() || !atlas.accumulate || !state->blocks.back().accumulate ||
            state->blocks.back().lane != atlas.lane ||
            state->blocks.back().end - state->blocks.back().first >= 4)
        {
            state->blocks.emplace_back();
            auto& block = state->blocks.back();
            block.first = i; block.end = i;
            block.lane = atlas.lane;
            block.accumulate = atlas.accumulate;
        }
        state->blocks.back().end = i + 1;
    }
    // Keep the original point-then-spot order. One unsafe light retains the
    // whole unshadowed tail on owner, without reordering custom shaders.
    bool safe_unshadowed = true;
    for (bool point : {true, false})
        for (auto* item : point ? state->package->v_point : state->package->v_spot)
        {
            // qkff99 6631dc22: resolve on owner before freezing visibility for the recording lane
            item->vis_update();
            if (!item->vis.visible) continue;
            state->unshadowed.push_back({item, {}, point});
            if (!RImplementation.Target->prepare_light_accumulation(state->unshadowed.back().illumination, *item, point))
                safe_unshadowed = false;
        }
    if (!safe_unshadowed) state->unshadowed.clear();
    if (!state->unshadowed.empty())
    {
        if (state->blocks.empty() || !state->blocks.back().accumulate || state->blocks.back().lane != 0)
        {
            state->blocks.emplace_back();
            auto& block = state->blocks.back();
            block.first = block.end = state->atlases.size();
            block.lane = 0;
            block.accumulate = true;
        }
        state->blocks.back().unshadowed = true;
    }
    // Freeze callbacks/poses in their original order, then prime each distinct
    // pass on its final recording lane. Lane assignment never redoes callbacks.
    for (auto& atlas : state->atlases)
    {
        auto& context = state->workers[atlas.lane].Context();
        for (const auto* job : atlas.lights)
        {
            for (const auto* pass : job->passes) context.Prime(*pass);
            if (atlas.accumulate && job->accumulate)
                for (const auto* pass : job->illumination.passes) context.Prime(*pass);
        }
    }
    for (const auto& item : state->unshadowed)
        for (const auto* pass : item.illumination.passes) state->workers[0].Context().Prime(*pass);
    RImplementation.phase = saved_phase;
    RCache.InvalidateAfterCommandList(); // Owner texture advancement touched SRV caches.
    std::fill(std::begin(state->finished), std::end(state->finished), false);
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
    u32 submitted = 0, skipped_static = 0, skipped_dynamic = 0, fallback_packets = 0, recorded_lights = 0;
    for (auto& block : state->blocks)
    {
        const bool published = state->WaitBlock(block);
        if (!published) state->workers[block.lane].Wait();
        if ((!published || !block.valid) && state->supported)
        {
            Msg("! Local shadows: recording failed; restoring remaining owner submission");
            state->supported = false;
        }
        const bool recorded = state->supported && published && block.valid && block.commands;
        if (recorded)
        {
            PROF_EVENT("Render.LocalShadows.Submit");
            HW.pContext->ExecuteCommandList(block.commands.Get(), FALSE);
            MergeStatistics(block.statistics);
            RCache.InvalidateAfterCommandList();
            ++submitted;
            if (block.accumulate)
            {
                RImplementation.Target->dwLightMarkerID = block.marker;
                RImplementation.stats.l_visible += block.visible;
                recorded_lights += block.visible;
            }
        }
        for (size_t i = block.first; i < block.end; ++i)
        {
            auto& atlas = state->atlases[i];
            if (!recorded) HW.pContext->ClearDepthStencilView(state->maps[atlas.lane].Depth(), D3D11_CLEAR_DEPTH, 1.f, 0);
            RCache.shadow_texture_source = RImplementation.Target->rt_smap_depth->pTexture._get();
            RCache.shadow_texture_view = state->maps[atlas.lane].View();
            for (auto* job : atlas.lights)
            {
                auto& item = *job->source;
                auto& graph = item.GMLight;
                if (recorded && job->cache && job->cache->selected != no_index)
                    job->cache->visibility.pending = job->cache->selected;
                // Spot shadow cache: a build counts once its list ran; without the list the
                // owner below renders every caster again, and the texture holds nothing usable
                if (job->depth == depth_build && job->cache) job->cache->stored.valid = recorded;
                if (recorded && block.accumulate)
                {
                    graph.RGraph.clear_graph<false>(graph.RGraph.mapDynamicPasses, 0);
                    if (job->accumulate) ++RImplementation.stats.ls_shadowed_rendered;
                    ++skipped_static; ++skipped_dynamic;
                    continue;
                }
                if (job->deferred || job->forward)
                {
                    const bool owner_static = !recorded || job->fallback.static_packets;
                    const bool owner_dynamic = !recorded || job->fallback.dynamic_packets;
                    if (owner_static || owner_dynamic || job->grass || job->forward)
                        SetShadowView(item, state->maps[atlas.lane].Depth());
                    graph.shadow_execution = recorded ? 2 : 0;
                    if (owner_static) graph.r_dsgraph_render_static(0, false);
                    else ++skipped_static;
                    if (owner_dynamic) graph.r_dsgraph_render_dynamic(0, true);
                    else
                    {
                        graph.RGraph.clear_graph<false>(graph.RGraph.mapDynamicPasses, 0);
                        ++skipped_dynamic;
                    }
                    fallback_packets += job->fallback.static_packets + job->fallback.dynamic_packets;
                    graph.shadow_execution = 0;
                    if (job->grass)
                    {
                        // Same as the owner path in lights_render.cpp (grass rework, on the owner)
                        RImplementation.Details->fade_distance = -1; // light position drives the fade
                        RImplementation.Details->light_position.set(item.position);
                        RImplementation.Details->hw_Render(&item);
                    }
                    item.X.S.transluent = FALSE;
                    if (job->forward)
                    {
                        item.X.S.transluent = TRUE;
                        RImplementation.Target->phase_smap_spot_tsh(&item);
                        graph.r_dsgraph_render_static(1, false);
                        graph.r_dsgraph_render_dynamic(1, true);
                        graph.r_dsgraph_render_sorted();
                    }
                }
            }
            if (recorded && block.accumulate) continue;
            // Original owner path: finish every atlas before its map is reused.
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
                        RImplementation.Target->set_viewport_size(HW.pContext, float(RenderScreenWidth()) / RImplementation.o.volsize,
                            float(RenderScreenHeight()) / RImplementation.o.volsize);
                    if (ps_pfx_volumetric_mode == 1) RImplementation.Target->accum_volumetric_lv(&item);
                    else RImplementation.Target->accum_volumetric(&item);
                    if (RImplementation.o.ssfx_volumetric)
                        RImplementation.Target->set_viewport_size(HW.pContext, float(RenderScreenWidth()), float(RenderScreenHeight()));
                }
            }
        }
        if (recorded && block.unshadowed)
        {
            for (const auto& item : state->unshadowed)
            {
                if (item.point) ++RImplementation.stats.ls_unshadowed_point_rendered;
                else ++RImplementation.stats.ls_unshadowed_spot_rendered;
            }
            package.v_point.clear();
            package.v_spot.clear();
        }
        block.commands.Reset();
    }
    ++state->stored_stats.frames;
    PROF_TAG("Local shadow atlases", u32(state->atlases.size()));
    PROF_TAG("Local submitted lists", submitted);
    PROF_TAG("Local recorded lights", recorded_lights);
    PROF_TAG("Local skipped static scans", skipped_static);
    PROF_TAG("Local skipped dynamic scans", skipped_dynamic);
    PROF_TAG("Local fallback packets", fallback_packets);
    RCache.shadow_texture_source = nullptr; RCache.shadow_texture_view = nullptr;
    RCache.InvalidateAfterCommandList();
    RImplementation.phase = state->atlases.empty() ? saved_phase : CRender::PHASE_SMAP;
    package.v_shadowed.clear();
    if (recorded_lights) RImplementation.Target->phase_accumulator();
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
    state->blocks.clear(); state->unshadowed.clear();
    state->atlases.clear(); state->lights.clear();
}

void LocalLightShadows::Forget(light* source)
{
    if (std::any_of(state->lights.begin(), state->lights.end(),
        [source](const std::unique_ptr<LightShadowJob>& job) { return job->source == source; }) ||
        std::any_of(state->unshadowed.begin(), state->unshadowed.end(),
            [source](const UnshadowedLight& job) { return job.source == source; })) EndFrame();
    state->caches.erase(source);
}

void LocalLightShadows::Clear()
{
    EndFrame();
    state = std::make_unique<State>();
}

void LocalLightShadows::Stats()
{
    u32 lights = 0, stored = 0;
    for (const auto& cache : state->caches)
    {
        ++lights;
        stored += cache.second->stored.valid ? 1 : 0;
    }
    auto& counters = state->stored_stats;
    const float frames = float(_max(1u, counters.frames));
    Msg("* spot shadow cache: %s, %u still lights, %u with stored depth, %.1f / %d MB",
        ps_r4_spot_shadow_cache ? "on" : "off", lights, stored, float(state->StoredBytes()) / (1024.f * 1024.f),
        ps_r4_spot_shadow_cache_mb);
    Msg("* over %u frames, per frame: %.1f restored, %.2f built, %.1f waiting for a stable transform; %u over budget, %u failures%s",
        counters.frames, float(counters.restores) / frames, float(counters.builds) / frames, float(counters.waiting) / frames,
        counters.over_budget, counters.failures, state->copy.failed ? " (copy shader unavailable)" : "");
    counters = {};
}

void CRender::spot_shadow_cache_stats()
{
    if (local_light_shadows) local_light_shadows->Stats();
    else Msg("* spot shadow cache: the recorded local shadows are off (r4_mt_local_shadows 0, r4_smapvis 0)");
}

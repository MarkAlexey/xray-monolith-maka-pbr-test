#include "stdafx.h"
#include "RenderContext.h"
#include "../xrRender/FSkinned.h"
#include "../xrRenderDX10/StateManager/dx10SamplerStateCache.h"
#include "../xrRenderDX10/StateManager/dx10ShaderResourceStateCache.h"
#include <map>
#include <wrl/client.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <exception>

#undef RCache
#undef StateManager
#undef SSManager
#undef SRVSManager

using Microsoft::WRL::ComPtr;
namespace { std::atomic<u64> next_context_generation{1}; }

struct RenderContext::State
{
    struct ConstantBuffer
    {
        ComPtr<ID3D11Buffer> identity;
        ref_cbuffer storage;
        u64 primed_frame = 0;
    };
    struct InputLayout
    {
        ref_declaration declaration;
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3D11InputLayout> layout;
    };

    u32 id;
    u64 generation = next_context_generation.fetch_add(1);
    u64 prime_generation = 0;
    ComPtr<ID3D11DeviceContext1> context;
    std::map<ID3D11Buffer*, ConstantBuffer> constants;
    std::map<std::pair<SDeclaration*, ID3DBlob*>, InputLayout> layouts;
    struct Texture
    {
        ref_texture owner;
        ComPtr<ID3D11ShaderResourceView> view;
        u64 frame = 0;
    };
    std::map<CTexture*, Texture> textures;
    dx10StateManager states;
    dx10SamplerStateCache samplers;
    dx10ShaderResourceStateCache resources;
    CBackend backend{false};
    Fmatrix camera_view, camera_projection, camera_full, camera_inverse;
    Fvector4 decompression_parameters, decompression_dimensions;

    explicit State(u32 index) : id(index) {}
    ~State()
    {
        _RELEASE(backend.QuadIB);
        _RELEASE(backend.CuboidIB);
    }

    dx10ConstantBuffer* Resolve(dx10ConstantBuffer* source)
    {
        auto& entry = constants[source->GetBuffer()];
        if (!entry.storage)
        {
            entry.identity = source->GetBuffer();
            entry.storage = xr_new<dx10ConstantBuffer>(*source, true);
        }
        return entry.storage._get();
    }
};

namespace
{
thread_local RenderContext::State* current = nullptr;
}

u32 RenderContextId() { return current ? current->id : 0; }
u64 RenderContextGeneration() { return current ? current->generation : 0; }
CBackend& ActiveRenderBackend() { return current ? current->backend : RCache; }
ID3D11DeviceContext1* ActiveDeviceContext() { return current ? current->context.Get() : HW.pContext; }
const Fmatrix& RenderCameraView() { return current ? current->camera_view : Device.mView; }
const Fmatrix& RenderCameraProjection() { return current ? current->camera_projection : Device.mProject; }
const Fmatrix& RenderCameraFullTransform() { return current ? current->camera_full : Device.mFullTransform; }
const Fmatrix& RenderCameraInverseView() { return current ? current->camera_inverse : Device.mInvView; }
bool ContextPositionDecompression(Fvector4& value, bool dimensions)
{
    if (!current) return false;
    value = dimensions ? current->decompression_dimensions : current->decompression_parameters;
    return true;
}
dx10StateManager& ActiveStateManager() { return current ? current->states : StateManager; }
dx10SamplerStateCache& ActiveSamplerManager() { return current ? current->samplers : SSManager; }
dx10ShaderResourceStateCache& ActiveResourceManager() { return current ? current->resources : SRVSManager; }

dx10ConstantBuffer* ContextConstantBuffer(dx10ConstantBuffer* source)
{
    return current ? current->Resolve(source) : source;
}

ID3D11ShaderResourceView* ContextTextureView(CTexture* source)
{
    R_ASSERT(current);
    const auto found = current->textures.find(source);
    R_ASSERT2(found != current->textures.end(), "Recording texture was not prepared on the render owner");
    return found->second.view.Get();
}

ID3D11InputLayout* ContextInputLayout(SDeclaration* declaration, ID3DBlob* signature)
{
    R_ASSERT(current && declaration && signature);
    auto& result = current->layouts[{declaration, signature}];
    if (!result.layout)
    {
        result.declaration = ref_declaration(declaration);
        result.signature = signature;
        R_CHK(HW.pDevice->CreateInputLayout(declaration->dx10_dcl_code.data(),
            u32(declaration->dx10_dcl_code.size() - 1), signature->GetBufferPointer(),
            signature->GetBufferSize(), result.layout.GetAddressOf()));
    }
    return result.layout.Get();
}

RenderContext::RenderContext(u32 id) : state(std::make_unique<State>(id))
{
    R_ASSERT(id && id < RenderContextCount);
    state->backend.QuadIB = nullptr;
    state->backend.CuboidIB = nullptr;
}

RenderContext::~RenderContext() = default;

bool RenderContext::Initialize()
{
    if (FAILED(HW.pDevice->CreateDeferredContext1(0, state->context.GetAddressOf()))) return false;
    Scope scope(*this);
    state->backend.ResetBindings();
    state->backend.Vertex.Create();
    state->backend.Index.Create();
    state->backend.QuadIB = RCache.QuadIB;
    if (state->backend.QuadIB) state->backend.QuadIB->AddRef();
    state->backend.CuboidIB = RCache.CuboidIB;
    if (state->backend.CuboidIB) state->backend.CuboidIB->AddRef();
    return true;
}

void RenderContext::Synchronize()
{
    R_ASSERT(!current);
    ++state->prime_generation;
    state->camera_view = Device.mView;
    state->camera_projection = Device.mProject;
    state->camera_full = Device.mFullTransform;
    state->camera_inverse = Device.mInvView;
    state->decompression_parameters = CalculatePositionDecompression();
    state->decompression_dimensions = CalculatePositionDimensions();
    state->samplers.Synchronize(SSManager);
    state->backend.xforms = RCache.xforms;
    state->backend.xforms.unmap();
    state->backend.object_hemi = RCache.object_hemi;
    state->backend.object_sun = RCache.object_sun;
    CopyMemory(state->backend.object_hemi_cube, RCache.object_hemi_cube, sizeof(RCache.object_hemi_cube));
}

void RenderContext::Prime(const SPass& pass)
{
    R_ASSERT(!current);
    if (pass.constants) for (const auto& source : pass.constants->m_CBTable)
    {
        auto* copy = state->Resolve(source.second._get());
        auto& entry = state->constants[source.second->GetBuffer()];
        if (entry.primed_frame == state->prime_generation) continue;
        copy->CopyDefaults(*source.second);
        entry.primed_frame = state->prime_generation;
    }
    if (pass.T) for (const auto& source : *pass.T)
    {
        auto* texture = source.second._get();
        if (!texture) continue;
        auto& entry = state->textures[texture];
        if (entry.frame == state->prime_generation) continue;
        entry.owner = source.second;
        // Loading, staging conversion and animated texture advancement stay on
        // the owner. Workers bind immutable SRV snapshots, never shared decoders.
        texture->bind(source.first);
        entry.view = texture->get_SRView();
        entry.frame = state->prime_generation;
    }
}

void RenderContext::Begin(u32 phase)
{
    R_ASSERT(current == state.get());
    state->generation = next_context_generation.fetch_add(1);
    state->context->ClearState();
    state->backend.ResetBindings();
    state->backend.recording_phase = phase;
    state->backend.recording_grass = false;
    state->backend.Vertex.Flush();
    state->backend.Index.Flush();
    // Every list must own a DISCARD upload of each dynamic CB it uses. A
    // cache hit must not make an earlier list depend on a later atlas' bytes.
    for (auto& entry : state->constants) entry.second.storage->RequireUpload();
    ZeroMemory(&state->backend.stat, sizeof(state->backend.stat));
}

HRESULT RenderContext::Finish(ID3D11CommandList** commands)
{
    R_ASSERT(current == state.get());
    return state->context->FinishCommandList(FALSE, commands);
}

ID3D11DeviceContext1* RenderContext::DeviceContext() const { return state->context.Get(); }
CBackend& RenderContext::Backend() { return state->backend; }
RenderContext::Scope::Scope(RenderContext& context) : previous(current) { current = context.state.get(); }
RenderContext::Scope::~Scope() { current = previous; }

struct RenderTask::State
{
    std::unique_ptr<RenderContext> context;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable condition;
    std::function<void(RenderContext&)> action;
    bool stop = false, ready = false, done = true, success = true;
};

RenderTask::RenderTask() : state(std::make_unique<State>()) {}
RenderTask::~RenderTask()
{
    if (!state->thread.joinable()) return;
    Wait();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->stop = true;
    }
    state->condition.notify_one();
    state->thread.join();
}

bool RenderTask::Start(u32 id, const char* name)
{
    state->context = std::make_unique<RenderContext>(id);
    if (!state->context->Initialize()) return false;
    try
    {
        state->thread = std::thread([this, name]()
        {
            PROF_THREAD(name);
            FPU::m24r();
            std::unique_lock<std::mutex> lock(state->mutex);
            for (;;)
            {
                state->condition.wait(lock, [this]() { return state->stop || state->ready; });
                if (state->stop) return;
                state->ready = false;
                lock.unlock();
                bool success = true;
                try
                {
                    RenderContext::Scope scope(*state->context);
                    state->action(*state->context);
                }
                catch (...)
                {
                    success = false;
                    ComPtr<ID3D11CommandList> discarded;
                    state->context->DeviceContext()->FinishCommandList(FALSE, discarded.GetAddressOf());
                }
                lock.lock();
                state->success = success;
                state->done = true;
                state->condition.notify_one();
            }
        });
    }
    catch (const std::system_error&) { return false; }
    return true;
}

void RenderTask::Run(std::function<void(RenderContext&)> action)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    R_ASSERT(state->done && state->thread.joinable());
    state->action = std::move(action);
    state->done = false;
    state->ready = true;
    state->condition.notify_one();
}

bool RenderTask::Wait()
{
    if (!state->thread.joinable()) return true;
    std::unique_lock<std::mutex> lock(state->mutex);
    state->condition.wait(lock, [this]() { return state->done; });
    return state->success;
}

RenderContext& RenderTask::Context() { return *state->context; }

bool RenderTask::Ready()
{
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->done;
}

struct ShadowMap::State
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11ShaderResourceView> view;
    bool owned = false;
    ~State() { if (texture && owned) HW.stats_manager.decrement_stats_rtarget(texture.Get()); }
};

ShadowMap::ShadowMap() : state(std::make_unique<State>()) {}
ShadowMap::~ShadowMap() = default;
bool ShadowMap::Initialize(bool original)
{
    const auto& source = RImplementation.Target->rt_smap_depth;
    if (original)
    {
        state->texture = source->pSurface;
        state->depth = source->pZRT;
        state->view = source->pTexture->get_SRView();
        return state->depth && state->view;
    }
    D3D11_TEXTURE2D_DESC description{};
    source->pSurface->GetDesc(&description);
    const HRESULT created = HW.pDevice->CreateTexture2D(&description, nullptr, state->texture.GetAddressOf());
    if (FAILED(created)) return false;
    HW.stats_manager.increment_stats_rtarget(state->texture.Get());
    state->owned = true;
    D3D11_DEPTH_STENCIL_VIEW_DESC depth{};
    source->pZRT->GetDesc(&depth);
    if (FAILED(HW.pDevice->CreateDepthStencilView(state->texture.Get(), &depth, state->depth.GetAddressOf()))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    source->pTexture->get_SRView()->GetDesc(&view);
    return SUCCEEDED(HW.pDevice->CreateShaderResourceView(state->texture.Get(), &view, state->view.GetAddressOf()));
}
bool ShadowMap::InitializeDepth(ID3D11DepthStencilView* depth)
{
    state->texture.Reset();
    state->view.Reset();
    state->owned = false;
    state->depth = depth;
    return depth != nullptr;
}
ID3D11DepthStencilView* ShadowMap::Depth() const { return state->depth.Get(); }
ID3D11ShaderResourceView* ShadowMap::View() const { return state->view.Get(); }

bool ParallelRenderPass(const SPass& pass)
{
    if (!pass.vs || !pass.ps || !pass.vs->recording_safe || !pass.ps->recording_safe)
        return false;
    if ((pass.gs && pass.gs->gs && !pass.gs->recording_safe) ||
        (pass.hs && pass.hs->sh && !pass.hs->recording_safe) ||
        (pass.ds && pass.ds->sh && !pass.ds->recording_safe))
        return false;
    if (pass.constants) for (const auto& constant : pass.constants->table)
        if (constant->handler && !constant->handler->parallel_safe()) return false;
    return true;
}

bool ParallelShadowPass(const R_dsgraph::RenderPacket& packet)
{
    if (!packet.source_pass || !ParallelRenderPass(*packet.source_pass)) return false;
    switch (packet.item.pVisual->Type)
    {
    case MT_NORMAL: case MT_PROGRESSIVE: case MT_TREE_ST: case MT_TREE_PM:
    case MT_SKELETON_GEOMDEF_ST: case MT_SKELETON_GEOMDEF_PM:
        return true;
    default:
        return false;
    }
}

void MergeStatistics(const CBackend::_stats& source)
{
    auto& target = RCache.stat;
    target.calls += source.calls; target.verts += source.verts; target.polys += source.polys;
    target.vs += source.vs; target.ps += source.ps; target.xforms += source.xforms;
    target.target_rt += source.target_rt; target.target_zb += source.target_zb;
#define MERGE_KIND(name) target.r.name.verts += source.r.name.verts; target.r.name.dips += source.r.name.dips
    MERGE_KIND(s_static); MERGE_KIND(s_flora); MERGE_KIND(s_flora_lods); MERGE_KIND(s_details);
    MERGE_KIND(s_ui); MERGE_KIND(s_dynamic); MERGE_KIND(s_dynamic_sw); MERGE_KIND(s_dynamic_inst);
    MERGE_KIND(s_dynamic_1B); MERGE_KIND(s_dynamic_2B); MERGE_KIND(s_dynamic_3B); MERGE_KIND(s_dynamic_4B);
#undef MERGE_KIND
}

ShadowFallback PrepareShadowPackets(CDSGraphManager& graph, RenderContext& context, std::vector<Fmatrix>& worlds,
    bool include_static, std::vector<const SPass*>* passes)
{
    ShadowFallback fallback;
    size_t matrices = 0;
    for (auto& queue : graph.RGraph.mapDynamicPasses[0]) matrices += queue.size();
    worlds.reserve(matrices);
    const Fmatrix* previous_world = nullptr;
    for (auto* queues : {&graph.RGraph.mapStaticPasses, &graph.RGraph.mapDynamicPasses})
    {
        // Keep the immutable packet metadata that produced an early command
        // list. Its static inputs are already frozen and primed for this view.
        if (!include_static && queues == &graph.RGraph.mapStaticPasses) continue;
        for (auto& queue : (*queues)[0])
            for (auto& packet : queue)
            {
                packet.parallel_shadow = ParallelShadowPass(packet);
                if (queues == &graph.RGraph.mapDynamicPasses && packet.item.pMatrix)
                {
                    const auto* source = packet.item.pMatrix;
                    if (!packet.parallel_shadow || source != previous_world || worlds.empty() ||
                        memcmp(source, &worlds.back(), sizeof(Fmatrix)))
                        worlds.push_back(*source);
                    previous_world = packet.parallel_shadow ? source : nullptr;
                    packet.item.pMatrix = &worlds.back();
                }
                packet.shadow_pose.reset();
                if (packet.parallel_shadow)
                {
                    const auto& world = packet.item.pMatrix ? *packet.item.pMatrix : Fidentity;
                    if (packet.item.pVisual->Type == MT_SKELETON_GEOMDEF_ST)
                        packet.shadow_pose = static_cast<CSkeletonX_ST*>(packet.item.pVisual)->SnapshotShadow(world);
                    else if (packet.item.pVisual->Type == MT_SKELETON_GEOMDEF_PM)
                        packet.shadow_pose = static_cast<CSkeletonX_PM*>(packet.item.pVisual)->SnapshotShadow(world);
                    if ((packet.item.pVisual->Type == MT_SKELETON_GEOMDEF_ST || packet.item.pVisual->Type == MT_SKELETON_GEOMDEF_PM) && !packet.shadow_pose)
                        packet.parallel_shadow = false;
                }
                if (auto* object = packet.item.pObject)
                {
                    auto& snapshot = packet.shadow_object;
                    if (auto* ros = static_cast<CROS_impl*>(object->renderable_ROS()))
                    {
                        ros->update_smooth(object);
                        snapshot.hemi = .75f * ros->get_hemi();
                        snapshot.sun = .75f * ros->get_sun();
                        CopyMemory(snapshot.cube, ros->get_hemi_cube(), sizeof(snapshot.cube));
                    }
                    float hotness = object->GetHotness();
                    snapshot.hotness_own = ShaderBus::object_hotness(object->renderable.bus_values, hotness);
                    snapshot.hotness.set(hotness, object->GetTransparency(), 0.f, 0.f);
                    snapshot.glowing.set(sil_glow_color.x, sil_glow_color.y, sil_glow_color.z, object->GetGlowing());
                }
                if (packet.parallel_shadow)
                {
                    if (!packet.local_shadow_hidden)
                    {
                        ++fallback.recording_packets;
                        // Frozen skinning is materially heavier than a static packet.
                        fallback.recording_work += packet.shadow_pose ? 3u : 1u;
                    }
                    if (!passes) context.Prime(*packet.source_pass);
                    // A smapvis query may still bind a hidden candidate's pass.
                    else if (std::find(passes->begin(), passes->end(), packet.source_pass) == passes->end())
                        passes->push_back(packet.source_pass);
                }
                else if (!packet.local_shadow_hidden)
                {
                    if (queues == &graph.RGraph.mapStaticPasses) ++fallback.static_packets;
                    else ++fallback.dynamic_packets;
                }
            }
    }
    return fallback;
}

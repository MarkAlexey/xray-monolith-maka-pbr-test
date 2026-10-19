#pragma once
#include <memory>
#include <functional>
#include <vector>
#include "../xrRender/R_Backend.h"

class CBackend;
class CDSGraphManager;
void PrepareShadowPackets(CDSGraphManager& graph, class RenderContext& context, std::vector<Fmatrix>& worlds,
    bool include_static = true);
void MergeStatistics(const CBackend::_stats& statistics);
class RenderContext
{
public:
    struct State;
private:
    std::unique_ptr<State> state;
public:
    explicit RenderContext(u32 id);
    ~RenderContext();
    bool Initialize();
    void Synchronize(); // Owner-only, before publishing a new frame's jobs.
    void Prime(const SPass& pass);
    void Begin(u32 phase);
    HRESULT Finish(ID3D11CommandList** commands);
    ID3D11DeviceContext1* DeviceContext() const;
    CBackend& Backend();

    class Scope
    {
        State* previous;
    public:
        explicit Scope(RenderContext& context);
        ~Scope();
    };
};

class RenderTask
{
    struct State;
    std::unique_ptr<State> state;
public:
    RenderTask();
    ~RenderTask();
    bool Start(u32 id, const char* name);
    void Run(std::function<void(RenderContext&)> action);
    bool Wait();
    bool Ready();
    RenderContext& Context();
};

class ShadowMap
{
    struct State;
    std::unique_ptr<State> state;
public:
    ShadowMap();
    ~ShadowMap();
    bool Initialize(bool original = false);
    bool InitializeDepth(ID3D11DepthStencilView* depth); // wrap an existing depth view (CSM slice), no SRV
    ID3D11DepthStencilView* Depth() const;
    ID3D11ShaderResourceView* View() const;
};

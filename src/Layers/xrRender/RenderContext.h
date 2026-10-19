#pragma once

#if defined(USE_DX11) && RENDER == R_R4
class CBackend;
class dx10StateManager;
class dx10SamplerStateCache;
class dx10ShaderResourceStateCache;
class dx10ConstantBuffer;
class CTexture;
struct SDeclaration;
struct RenderObjectSnapshot
{
    float hemi = .75f, sun = .75f, cube[6]{};
    Fvector4 hotness{}, glowing{};
    bool hotness_own = false;
};

// Context 0 belongs to the immediate renderer. Recording contexts have private
// backends, CPU constant storage, API buffers, streams and binding caches.
constexpr u32 RenderContextCount = 9;
ECORE_API u32 RenderContextId();
ECORE_API u64 RenderContextGeneration();
ECORE_API CBackend& ActiveRenderBackend();
ECORE_API ID3D11DeviceContext1* ActiveDeviceContext();
ECORE_API dx10StateManager& ActiveStateManager();
ECORE_API dx10SamplerStateCache& ActiveSamplerManager();
ECORE_API dx10ShaderResourceStateCache& ActiveResourceManager();
ECORE_API dx10ConstantBuffer* ContextConstantBuffer(dx10ConstantBuffer* source);
ECORE_API ID3D11InputLayout* ContextInputLayout(SDeclaration* declaration, ID3DBlob* signature);
ECORE_API ID3D11ShaderResourceView* ContextTextureView(CTexture* source);
#else
constexpr u32 RenderContextCount = 1;
IC u32 RenderContextId() { return 0; }
IC u64 RenderContextGeneration() { return 0; }
#endif

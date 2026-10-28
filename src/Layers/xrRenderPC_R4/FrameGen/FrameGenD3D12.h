#pragma once

// Monolith: frame generation bridge (r4_framegen 1).
//
// The game keeps rendering with Direct3D 11. AMD FSR 3.1 frame generation (FidelityFX API,
// amd_fidelityfx_dx12.dll) only runs on Direct3D 12, so presentation moves to a small D3D12
// device on the same GPU:
//
//   D3D11 (game)                         D3D12 (this bridge)
//   scene + post -> backbuffer
//   Capture():  copy backbuffer (hudless), motion vectors, depth -> shared textures
//   UI drawn into the backbuffer
//   Present():  copy backbuffer -> shared texture, signal fence ----> wait fence
//                                                                     copy shared -> private inputs
//                                                                     FSR 3.1 prepare (depth, MVs)
//                                                                     copy frame -> FG swapchain
//                                                                     Present: AMD's swapchain shows
//                                                                     the generated frame, then this one
//   next frame: GPU-waits the D3D12 fence before writing the shared textures again
//
// The D3D11 swapchain stays alive on a hidden window (the engine keeps using it for its back
// buffer, resizes and device-state checks); the frame-generation swapchain owns the game window.
// The UI is found by comparing the hudless copy with the final frame (FSR 3.1's "hudless" mode),
// so interpolated frames keep a sharp HUD.
//
// This file has no engine dependencies (like DlssD3D11): the integration (FrameGenIntegration)
// gathers the inputs. Both libraries are loaded at run time: without amd_fidelityfx_dx12.dll the
// bridge reports why and the game keeps the plain D3D11 path.

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <memory>

namespace framegen
{
struct Inputs
{
    ID3D11Texture2D* hudless = nullptr;         // the back buffer before the UI (display size)
    ID3D11ShaderResourceView* depth = nullptr;  // hardware depth, 0 = near (render size)
    ID3D11Texture2D* motion = nullptr;          // SSS motion vectors, RGBA16F (render size)
    unsigned renderWidth = 0, renderHeight = 0; // size of depth / motion
    float jitterX = 0, jitterY = 0;             // pixels, the upscaler's sample (0 without one)
    float motionScaleX = 0, motionScaleY = 0;   // to current->previous pixels (dlss::MotionScale)
    float cameraNear = 0.1f, cameraFar = 1000.f, fovY = 1.f;
    float frameTimeMs = 16.f;
    float position[3] = {}, up[3] = {}, right[3] = {}, forward[3] = {};
    bool reset = false;                         // camera cut: no interpolation across it
};

class Bridge
{
public:
    using Log = void (*)(const char* message);

    Bridge();
    ~Bridge();
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    // Loads d3d12.dll and amd_fidelityfx_dx12.dll, creates the D3D12 device on the adapter of
    // `device`, its queue and the shared fences. Needs Windows 10 1703+ (ID3D11Device5).
    bool Initialize(ID3D11Device* device, IDXGIFactory2* factory, Log log);
    void Shutdown();
    bool Initialized() const;

    // Frame-generation swapchain for the game window, plus the FSR 3.1 context.
    bool Attach(HWND window, unsigned width, unsigned height, DXGI_FORMAT format, bool tearing);
    void Detach();
    bool Attached() const;

    // D3D11, before the UI is drawn: copies this frame's inputs. False = no generation this frame.
    bool Capture(const Inputs& inputs);
    // D3D11 back buffer with the UI. generate: interpolate (needs Capture this frame).
    // debugFlags: FfxApiDispatchFramegenerationFlags (r4_framegen_debug).
    HRESULT Present(ID3D11Texture2D* backbuffer, bool vsync, bool tearing, bool generate, unsigned debugFlags);

    // Last error, for the log
    const char* Error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
}

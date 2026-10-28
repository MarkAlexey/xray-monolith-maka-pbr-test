#include "stdafx.h"
#include "FrameGenIntegration.h"
#include "FrameGenD3D12.h"
#include "../../../xrEngine/igame_persistent.h"
#include <wrl/client.h>

//////////////////////////////////////////////////////////////////////////
// Monolith: frame generation, engine side
//
// r4_framegen 1 (game restart): AMD FSR 3.1 frame generation through a Direct3D 12
// presentation bridge (FrameGenD3D12.h). Every GPU with Direct3D 12 and Windows 10 1703+;
// needs amd_fidelityfx_dx12.dll next to the exe (the release's frame generation add-on) and
// SSS's motion vectors. One generated frame between every two rendered ones.
//
// What it is fed, captured just before the UI is drawn (Level::OnRender):
//   - the back buffer at that point (scene, post-processing, ReShade; no UI): FSR compares it
//     with the final frame to find the UI, so generated frames keep a sharp HUD
//   - SSS's motion vectors and the scene depth, at the scene's render size (the upscaler's,
//     with its jitter and motion scale, or the display size without one)
//   - the camera (near / far / vertical FOV from the projection, position and axes)
//
// Generation pauses (frames are still presented through the bridge, one per rendered frame) in
// menus, while the game is paused, with MSAA, without SSS motion vectors and while a 3D scope
// (second viewport) is up: the engine then renders the scope view every other frame and the
// main view's motion vectors are not trustworthy. A camera cut (teleport, level change, large
// turn or zoom between two frames, or a paused frame) resets interpolation for one frame.
//
// r4_framegen 0 while running pauses generation; switching the bridge on or off takes a game
// restart (the swapchain is chosen when the device is created). r4_framegen_debug 1 draws
// FSR's tear / reset / pacing markers, 2 its debug view.
//
// Caveats:
//   - Exclusive fullscreen is replaced by a borderless window (AMD's swapchain is windowed).
//   - Input latency rises by about one rendered frame. A frame limiter caps the RENDERED frames;
//     the screen shows twice as many.
//   - Gamma / brightness through the display's gamma ramp needs exclusive fullscreen and is not
//     applied (it was not in borderless mode either).
//   - ReShade hooks the D3D11 swapchain, which no longer reaches the screen: untested.
//////////////////////////////////////////////////////////////////////////

using Microsoft::WRL::ComPtr;

namespace
{
framegen::Bridge bridge;
HWND hiddenWindow = nullptr;
bool attached = false;

bool capturedThisFrame = false;
bool previousFrameCaptured = false;
const char* pauseReason = "not started";

// Camera of the last captured frame, for cut detection
Fvector lastPosition, lastDirection;
float lastFov = 0;

// Shader view of the scene depth, recreated when the texture changes
ComPtr<ID3D11ShaderResourceView> depthView;
ID3D11Texture2D* depthViewSource = nullptr;
UINT depthViewWidth = 0, depthViewHeight = 0;

u32 presentFailures = 0;

// FfxApiDispatchFramegenerationFlags (ffx_framegeneration.h), for r4_framegen_debug
constexpr unsigned FFX_TEAR_LINES = 1u << 0, FFX_RESET_INDICATORS = 1u << 1, FFX_DEBUG_VIEW = 1u << 2,
    FFX_PACING_LINES = 1u << 4;

void LogLine(const char* message) { Msg("%s", message); }

void SetPause(const char* reason)
{
    if (reason == pauseReason) return;
    if (reason) Msg("* [FrameGen] generation paused: %s", reason);
    else Msg("* [FrameGen] generation running");
    pauseReason = reason;
}

ID3D11Texture2D* TextureOf(ID3D11View* view)
{
    if (!view) return nullptr;
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) return nullptr;
    return texture.Get();   // the view keeps it alive
}

ID3D11ShaderResourceView* DepthView(ID3D11Texture2D* texture)
{
    if (!texture) return nullptr;
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    if (depthView && depthViewSource == texture && depthViewWidth == desc.Width && depthViewHeight == desc.Height)
        return depthView.Get();
    depthView.Reset();
    depthViewSource = nullptr;
    if (desc.Format != DXGI_FORMAT_R24G8_TYPELESS || !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) || desc.SampleDesc.Count != 1)
        return nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
    view.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    if (FAILED(HW.pDevice->CreateShaderResourceView(texture, &view, &depthView))) return nullptr;
    depthViewSource = texture;
    depthViewWidth = desc.Width;
    depthViewHeight = desc.Height;
    return depthView.Get();
}
}

namespace framegen
{
bool Requested() { return ps_r4_framegen != 0 && !Core.ParamsData.test(ECoreParams::dxgi_old); }
bool Initialized() { return bridge.Initialized(); }
bool Attached() { return attached; }

HWND Initialize(ID3D11Device* device, IDXGIFactory2* factory)
{
    if (!Requested()) return nullptr;
    if (!bridge.Initialize(device, factory, LogLine))
    {
        Msg("! [FrameGen] OFF: %s. The game presents through Direct3D 11 as usual.", bridge.Error());
        bridge.Shutdown();
        return nullptr;
    }
    // The D3D11 swapchain needs a window of its own: the game window gets AMD's D3D12 swapchain,
    // and DXGI allows one flip-model swapchain per window. It is never shown.
    hiddenWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"X-Ray frame generation",
        WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hiddenWindow)
    {
        Msg("! [FrameGen] OFF: cannot create the helper window (error %u)", unsigned(GetLastError()));
        bridge.Shutdown();
        return nullptr;
    }
    return hiddenWindow;
}

bool Attach(HWND window, unsigned width, unsigned height, DXGI_FORMAT format, bool tearing)
{
    depthView.Reset();
    depthViewSource = nullptr;
    attached = bridge.Attach(window, width, height, format, tearing);
    if (attached)
    {
        Msg("* [FrameGen] ON: FSR 3.1 frame generation, %ux%u (r4_framegen 0 pauses it)", width, height);
        pauseReason = "not started";
        previousFrameCaptured = false;
        presentFailures = 0;
    }
    else
        Msg("! [FrameGen] OFF: %s. Back to Direct3D 11 presentation.", bridge.Error());
    return attached;
}

void Shutdown()
{
    attached = false;
    capturedThisFrame = previousFrameCaptured = false;
    depthView.Reset();
    depthViewSource = nullptr;
    bridge.Shutdown();
    if (hiddenWindow)
    {
        DestroyWindow(hiddenWindow);
        hiddenWindow = nullptr;
    }
}

void CaptureHudless()
{
    capturedThisFrame = false;
    if (!attached || Device.m_SecondViewport.IsSVPFrame()) return;   // scope frames are not presented

    const char* reason = nullptr;
    IMainMenu* menu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : nullptr;
    if (!ps_r4_framegen) reason = "r4_framegen 0";
    else if (menu && menu->IsActive()) reason = "menu";
    else if (Device.Paused()) reason = "game paused";
    else if (Device.m_SecondViewport.IsSVPActive()) reason = "3D scope (second viewport) active";
    else if (RImplementation.o.dx10_msaa) reason = "MSAA is on";
    else if (!RImplementation.o.ssfx_motionvectors || !RImplementation.Target->rt_ssfx_motion_vectors)
        reason = "no SSS motion vectors (Screen Space Shaders' motion vectors are required)";

    ID3D11Texture2D* motion = nullptr;
    ID3D11Texture2D* depth = nullptr;
    ID3D11ShaderResourceView* depthSrv = nullptr;
    ID3D11Texture2D* hudless = nullptr;
    D3D11_TEXTURE2D_DESC motionDesc = {}, depthDesc = {};
    if (!reason)
    {
        motion = static_cast<ID3D11Texture2D*>(RImplementation.Target->rt_ssfx_motion_vectors->pSurface);
        // The scene depth: the upscaler's render-size buffer, or the back buffer's depth
        // (readable while the bridge exists, CHW::UpdateViews)
        depth = dlss::Configured() ? dlss::SceneDepthTexture() : TextureOf(HW.pBaseZB);
        depthSrv = DepthView(depth);
        hudless = TextureOf(HW.pBaseRT);
        if (motion) motion->GetDesc(&motionDesc);
        if (depth) depth->GetDesc(&depthDesc);
        if (!motion || !hudless) reason = "render targets missing";
        else if (!depthSrv) reason = "scene depth is not readable";
        else if (motionDesc.Width != depthDesc.Width || motionDesc.Height != depthDesc.Height)
            reason = "motion vectors and depth differ in size";
    }
    if (reason)
    {
        SetPause(reason);
        previousFrameCaptured = false;
        return;
    }

    framegen::Inputs in;
    in.hudless = hudless;
    in.depth = depthSrv;
    in.motion = motion;
    in.renderWidth = motionDesc.Width;
    in.renderHeight = motionDesc.Height;
    // The upscaler's jitter of this frame (EndScene keeps it as the "previous" one); none without
    const dlss::Offset jitter = dlss::Configured() ? dlss::PreviousRasterJitter() : dlss::Offset{};
    in.jitterX = jitter.x;
    in.jitterY = jitter.y;
    // SSS stores current - previous UV without jitter; FSR wants current -> previous pixels
    const dlss::Offset scale = dlss::MotionScale({motionDesc.Width, motionDesc.Height});
    in.motionScaleX = scale.x;
    in.motionScaleY = scale.y;
    in.cameraNear = -Device.mProject._43 / Device.mProject._33;
    in.cameraFar = -Device.mProject._43 / (Device.mProject._33 - 1.0f);
    in.fovY = 2.0f * atanf(1.0f / Device.mProject._22);
    in.frameTimeMs = clampr(Device.fTimeDelta * 1000.0f, 0.1f, 250.0f);
    const Fvector& p = Device.vCameraPosition;
    const Fvector& u = Device.vCameraTop;
    const Fvector& r = Device.vCameraRight;
    const Fvector& f = Device.vCameraDirection;
    in.position[0] = p.x; in.position[1] = p.y; in.position[2] = p.z;
    in.up[0] = u.x; in.up[1] = u.y; in.up[2] = u.z;
    in.right[0] = r.x; in.right[1] = r.y; in.right[2] = r.z;
    in.forward[0] = f.x; in.forward[1] = f.y; in.forward[2] = f.z;
    // Camera cut: no interpolation across a teleport, a sharp turn or zoom, or a paused frame
    in.reset = !previousFrameCaptured || lastPosition.distance_to_sqr(p) > 5.0f * 5.0f ||
        lastDirection.dotproduct(f) < 0.7f || _abs(lastFov - Device.fFOV) > 5.0f;
    if (!_valid(in.cameraNear) || !_valid(in.cameraFar) || in.cameraNear <= 0 || in.cameraFar <= in.cameraNear)
    {
        SetPause("invalid camera projection");
        previousFrameCaptured = false;
        return;
    }

    capturedThisFrame = bridge.Capture(in);
    if (!capturedThisFrame)
    {
        SetPause(bridge.Error()[0] ? bridge.Error() : "inputs rejected");
        previousFrameCaptured = false;
        return;
    }
    SetPause(nullptr);
    lastPosition = p;
    lastDirection = f;
    lastFov = Device.fFOV;
}

bool Present(bool vsync, bool tearing)
{
    if (!attached) return false;
    ID3D11Texture2D* backbuffer = TextureOf(HW.pBaseRT);
    const unsigned debug = ps_r4_framegen_debug == 1 ? (FFX_TEAR_LINES | FFX_RESET_INDICATORS | FFX_PACING_LINES)
        : ps_r4_framegen_debug == 2 ? FFX_DEBUG_VIEW : 0u;
    const HRESULT hr = bridge.Present(backbuffer, vsync, tearing, ps_r4_framegen != 0 && capturedThisFrame, debug);
    previousFrameCaptured = capturedThisFrame;
    capturedThisFrame = false;
    if (FAILED(hr))
    {
        if (presentFailures < 10 || presentFailures % 600 == 0)
            Msg("! [FrameGen] present failed 0x%08X (%u so far)%s", unsigned(hr), presentFailures + 1,
                hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ? ": the GPU device was lost" : "");
        ++presentFailures;
    }
    return true;
}
}

#include "stdafx.h"
#include "FrameGeneration.h"
#include "../../xrRenderDX10/D3D12PresentBridge.h"
#include "../../../xrEngine/IGame_Persistent.h"
#include "../../../xrEngine/IGame_Level.h"
#include "../../../xrEngine/RendererError.h"

extern u32 ps_r_frame_generation;
extern int ps_r_fg_diagnostics;
extern int ps_framelimiter;
extern bool IsMainMenuActive();

namespace fg
{
namespace
{
bool simulationFrame{}, renderStarted{}, scopeFrame{}, havePrevious{};
// dlss::Active() is only true between BeginScene and EndScene. Decide eligibility
// once at scene capture; the later HUD-less capture and Present reuse it.
bool sceneCaptured{};
Fmatrix previousTransform;
Fvector previousPosition, previousDirection;
float previousFov{};
u32 previousFrame{};
const char* lastReason{};

enum Method : u32 { Off = 0, Dlss = 1, Fsr = 2 };

void Status(const char* reason)
{
    if (reason == lastReason) return;
    lastReason = reason;
    if (ps_r_frame_generation) Msg("[%s FG] %s", ps_r_frame_generation == Fsr ? "FSR" : "DLSS", reason);
}

// The bridge runtime is fixed until vid_restart rebuilds it for the selected method.
bool RuntimeMatchesSetting(const D3D12PresentBridge& bridge)
{
    const PresentRuntime runtime = bridge.Runtime();
    return ps_r_frame_generation == Fsr ? runtime == PresentRuntime::Fsr : runtime == PresentRuntime::Streamline;
}

bool Eligible()
{
    if (!ps_r_frame_generation) { Status("Off"); return false; }
    auto* bridge = HW.PresentationBridge();
    if (!bridge) return false;
    if (!RuntimeMatchesSetting(*bridge))
    {
        Status(ps_r_frame_generation == Dlss && !StreamlineIntegration::CanInitialize() ?
            "DLSS frame generation needs a game restart after FSR frame generation was used." :
            "Selected method is not active: Apply/vid_restart to switch, or it failed to start (see [DX12 bridge] lines).");
        return false;
    }
    if (bridge->GenerationFailed()) { Status("Disabled after a runtime error."); return false; }
    if (!bridge->FrameGenerationReady()) { Status("Unavailable: the runtime did not start. See the log."); return false; }
    if (!Device.b_is_Active || Device.Paused() || !g_pGameLevel || Device.dwPrecacheFrame || IsMainMenuActive())
    { Status("Suspended: menu, loading, pause or inactive window."); return false; }
    if (bridge->VSyncBlocksGeneration())
    { Status("Suspended: this DLSS-G runtime cannot generate frames with VSync on. Turn VSync off."); return false; }
    if (RImplementation.o.dx10_msaa || !RImplementation.o.ssfx_motionvectors || !dlss::Active())
    { Status("Requires an active temporal upscaler and SSS motion vectors, with MSAA off."); return false; }
    // Scopes and thermal vision are post effects over the same main-view inputs. A 3D
    // scope's lens updates at its own rate and may ghost slightly inside the lens.
    Status("Eligible: generating frames.");
    return true;
}

void ToFloats(float (&target)[16], const Fmatrix& source)
{
    static_assert(sizeof(Fmatrix) == sizeof(target), "Fmatrix layout");
    memcpy(target, &source, sizeof(target));
}
}

void BeginSimulation()
{
    auto* bridge = HW.PresentationBridge();
    if (!bridge) return;
    simulationFrame = true;
    renderStarted = false;
    // dwFrame advances in FrameMove, after this call. A 3D-scope (SVP) frame renders
    // the scope view without presenting; it belongs to the next presented frame and
    // must neither sleep in Reflex nor consume a frame token.
    auto& svp = Device.m_SecondViewport;
    scopeFrame = svp.IsSVPActive() && (Device.dwFrame + 1) % svp.GetSVPFrameDelay() == 0;
    if (scopeFrame) return;
    UINT limitUs = 0;
    if (ReflexLimitsFrames())
        limitUs = UINT(1e6f * FrameLimitScale() / float(ps_framelimiter));
    R_CHK(bridge->BeginFrame(limitUs, ps_r_frame_generation != 0));
}

void EndSimulation()
{
    auto* bridge = HW.PresentationBridge();
    if (!bridge || !simulationFrame || renderStarted) return;
    renderStarted = true;
    if (scopeFrame) return;
    if (auto* runtime = bridge->GetStreamline())
    {
        runtime->Marker(sl::PCLMarker::eSimulationEnd);
        runtime->Marker(sl::PCLMarker::eRenderSubmitStart);
    }
}

void BeginRender()
{
    // Loading-screen draws can bypass on_idle's simulation path. They get their
    // own token and paired markers, but never reuse an old scene capture.
    if (!simulationFrame) BeginSimulation();
    EndSimulation();
}

void EndPresent()
{
    simulationFrame = renderStarted = scopeFrame = false;
    auto* bridge = HW.PresentationBridge();
    // A runtime error turned FG off (the game keeps running). Reflect that in the
    // setting so the menu shows Off, and let the player turn it on again to retry.
    static u32 disabledNoticeUntil = 0;
    if (ps_r_frame_generation && bridge && bridge->GenerationFailed())
    {
        Msg("! [FG] Frame generation was turned off after an error (see above). It can be turned on again in Video settings.");
        ps_r_frame_generation = Off;
        bridge->ClearGenerationFailure();
        disabledNoticeUntil = Device.dwTimeGlobal + 8000;
    }
    if (!ps_r_frame_generation)
        SetFrameGenerationError(Device.dwTimeGlobal < disabledNoticeUntil ?
            "Frame generation was turned off after an error. You can turn it on again in Video settings." : nullptr);
    else if (!bridge || bridge->GenerationFailed() || !bridge->FrameGenerationReady())
        SetFrameGenerationError("Frame generation is unavailable. See the game log.");
    else if (!RuntimeMatchesSetting(*bridge))
        SetFrameGenerationError(ps_r_frame_generation == Dlss && !StreamlineIntegration::CanInitialize() ?
            "DLSS frame generation needs a game restart after using FSR frame generation." :
            "Selected frame generation method is not active. Apply again or see the game log.");
    else if (g_pGameLevel && !IsMainMenuActive() && !dlss::Configured())
        SetFrameGenerationError("Frame generation needs DLSS or FSR upscaling enabled.");
    else SetFrameGenerationError(nullptr);
}

void CaptureScene()
{
    auto* bridge = HW.PresentationBridge();
    sceneCaptured = false;
    // The scope view has its own camera; leave the main view's history untouched.
    if (Device.m_SecondViewport.IsSVPFrame()) return;
    if (!bridge || !Eligible() || !bridge->NeedsFGInputs()) { havePrevious = false; return; }
    // Main frames are not consecutive while a 3D scope renders on alternate frames.
    const u32 maxGap = Device.m_SecondViewport.IsSVPActive() ? Device.m_SecondViewport.GetSVPFrameDelay() : 1;
    const bool cut = !havePrevious || Device.dwFrame <= previousFrame || Device.dwFrame - previousFrame > maxGap ||
        previousPosition.distance_to_sqr(Device.vCameraPosition) > 64.f ||
        previousDirection.dotproduct(Device.vCameraDirection) < .25f || _abs(previousFov - Device.fFOV) > 10.f;
    Fmatrix inverseProjection, inverseCurrent, clipToPrevious, previousToClip;
    // Projective matrices: Fmatrix::invert is a 4x3 affine inverse, so use the full 4x4 one.
    D3DXMatrixInverse((D3DXMATRIX*)&inverseProjection, nullptr, (D3DXMATRIX*)&Device.mProject);
    D3DXMatrixInverse((D3DXMATRIX*)&inverseCurrent, nullptr, (D3DXMATRIX*)&Device.mFullTransform);
    clipToPrevious.mul(cut ? Device.mFullTransform : previousTransform, inverseCurrent);
    D3DXMatrixInverse((D3DXMATRIX*)&previousToClip, nullptr, (D3DXMATRIX*)&clipToPrevious);
    FrameGenerationInputs inputs;
    ToFloats(inputs.viewToClip, Device.mProject);
    ToFloats(inputs.clipToView, inverseProjection);
    ToFloats(inputs.clipToPrevClip, clipToPrevious);
    ToFloats(inputs.prevClipToClip, previousToClip);
    const auto jitter = dlss::RasterJitter();
    inputs.jitterX = jitter.x;
    inputs.jitterY = jitter.y;
    // SSS stores current UV minus previous UV (unlike OGSR's NDC vectors).
    inputs.motionToUvX = inputs.motionToUvY = -1.f;
    const Fvector* vectors[]{&Device.vCameraPosition, &Device.vCameraTop, &Device.vCameraRight, &Device.vCameraDirection};
    float* targets[]{inputs.cameraPosition, inputs.cameraUp, inputs.cameraRight, inputs.cameraForward};
    for (int i = 0; i < 4; ++i) { targets[i][0] = vectors[i]->x; targets[i][1] = vectors[i]->y; targets[i][2] = vectors[i]->z; }
    inputs.cameraNear = -Device.mProject._43 / Device.mProject._33;
    inputs.cameraFar = -Device.mProject._43 / (Device.mProject._33 - 1.f);
    inputs.verticalFov = 2.f * atanf(1.f / Device.mProject._22);
    inputs.aspectRatio = Device.mProject._22 / Device.mProject._11;
    inputs.frameTimeDelta = Device.fTimeDelta * 1000.f;
    inputs.reset = cut;
    const HRESULT result = bridge->CaptureScene(dlss::SceneDepthTexture(), RImplementation.Target->rt_ssfx_motion_vectors->pSurface, inputs);
    if (FAILED(result)) { R_CHK(bridge->FailGeneration(bridge->LastFailure())); havePrevious = false; return; }
    if (ps_r_fg_diagnostics && (cut || Device.dwFrame % 240 == 0))
        Msg("[FG DIAG] Capture engineFrame=%u previous=%u cut=%u scope=%u render=%ux%u display=%ux%u",Device.dwFrame,previousFrame,unsigned(cut),
            unsigned(Device.m_SecondViewport.IsSVPActive()),dlss::RenderWidth(),dlss::RenderHeight(),Device.dwWidth,Device.dwHeight);
    previousFrame = Device.dwFrame;
    previousTransform = Device.mFullTransform;
    previousPosition = Device.vCameraPosition;
    previousDirection = Device.vCameraDirection;
    previousFov = Device.fFOV;
    havePrevious = true;
    sceneCaptured = true;
}

void CaptureHudless()
{
    auto* bridge = HW.PresentationBridge();
    if (!bridge) return;
    const HRESULT result = bridge->CaptureHudless(sceneCaptured);
    sceneCaptured = false;
    if (FAILED(result)) R_CHK(bridge->FailGeneration(bridge->LastFailure()));
}

void Reset()
{
    havePrevious = simulationFrame = renderStarted = scopeFrame = sceneCaptured = false;
    lastReason = nullptr;
    if (auto* bridge = HW.PresentationBridge()) bridge->ResetHistory();
}

bool ReflexLimitsFrames()
{
    auto* bridge = HW.PresentationBridge();
    // Menus and pause keep the engine's own eco limiter.
    return ps_framelimiter > 0 && bridge && bridge->GetStreamline() && !bridge->GenerationFailed() &&
        !Device.Paused() && !IsMainMenuActive();
}

float FrameLimitScale()
{
    auto* bridge = HW.PresentationBridge();
    return bridge && bridge->GenerationActive() ? 2.f : 1.f;
}
}

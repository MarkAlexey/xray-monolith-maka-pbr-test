#pragma once
#include "DlssTemporal.h"
#include "../../xrRender/RenderDimensions.h"

namespace dlss
{
// Shared temporal scene integration (historical DLSS name). The selected backend
// is latched at target creation; console/UI changes only apply on vid_restart.
void InitializeTargets();
void ReleaseTargets();
void ShutdownDevice();
void BeginScene();
void EndScene(bool invalidateHistory = false);
bool Active();
bool Configured();
unsigned RenderWidth();
unsigned RenderHeight();
Offset RasterJitter();
Offset PreviousRasterJitter();
ID3DDepthStencilView* SceneDepth();
ID3D11Texture2D* SceneDepthTexture();
ID3D11Texture2D* Output();
// reactive: optional R8 mask of pixels that should follow this frame (DLSS bias current colour,
// FSR 3 reactive), r4_rendertarget_phase_reactive.cpp
bool Evaluate(ID3D11Texture2D* color, ID3D11Texture2D* motion, ID3D11Texture2D* reactive = nullptr);

// Video-menu guards. Ids match the r_upscaler / r_frame_generation tokens.
// The first DLSS query probes NGX once per device; call on the render thread.
bool UpscalerSupported(unsigned id);
bool FrameGenerationSupported(unsigned id);
// "dlss=1 fsr3=1 fsr_dx12=3.1.5 fg_dlss=1 fg_fsr=3.1.6" (fg_dlss: 0/1/restart).
void SupportSummary(char* text, unsigned size);
}

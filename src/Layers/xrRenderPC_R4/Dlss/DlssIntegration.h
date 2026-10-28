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
// true while the PiP scope viewport (TargetSVP) is the active target; the upscaler is bypassed
bool ScopePass();
Offset PreviousRasterJitter();
ID3DDepthStencilView* SceneDepth();
ID3D11Texture2D* SceneDepthTexture();
ID3D11Texture2D* Output();
// reactive: optional R8 mask of pixels that should follow this frame (DLSS bias current colour,
// FSR 3 reactive), r4_rendertarget_phase_reactive.cpp
bool Evaluate(ID3D11Texture2D* color, ID3D11Texture2D* motion, ID3D11Texture2D* reactive = nullptr);
}

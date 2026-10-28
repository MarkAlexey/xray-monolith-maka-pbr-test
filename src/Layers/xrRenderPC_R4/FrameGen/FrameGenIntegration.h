#pragma once

// Monolith: frame generation (r4_framegen 1), engine side. See FrameGenD3D12.h for how the
// bridge works and FrameGenIntegration.cpp for what the game feeds it.
//
// Lifetime (dx10HW.cpp):
//   CreateDevice  Initialize() after the D3D11 device: returns a hidden window for the D3D11
//                 swapchain, or nullptr = frame generation off, plain D3D11 presentation
//   Reset         Attach() after the back buffer exists (also on every resize / vid_restart);
//                 false = the caller goes back to a D3D11 swapchain on the game window
//   DestroyDevice Shutdown()
// Per frame:
//   Level::OnRender, before the UI   Render->FrameGenCaptureHudless() -> CaptureHudless()
//   dxRenderDeviceRender::End        Present(): false = not attached, present through D3D11

namespace framegen
{
bool Requested();
HWND Initialize(ID3D11Device* device, IDXGIFactory2* factory);
bool Initialized();
bool Attach(HWND window, unsigned width, unsigned height, DXGI_FORMAT format, bool tearing);
bool Attached();
void Shutdown();
void CaptureHudless();
bool Present(bool vsync, bool tearing);
}

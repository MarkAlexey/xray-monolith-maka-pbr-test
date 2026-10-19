#pragma once

// Device dimensions stay at display resolution (UI, input, swapchain). Only the
// DX11 scene changes resolution. Other renderers retain their existing behavior.
#if defined(USE_DX11)
unsigned RenderScreenWidth();
unsigned RenderScreenHeight();
#else
inline unsigned RenderScreenWidth() { return Device.dwWidth; }
inline unsigned RenderScreenHeight() { return Device.dwHeight; }
#endif

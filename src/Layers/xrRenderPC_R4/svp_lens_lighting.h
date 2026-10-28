#pragma once

class CResourceManager;
class IRenderable;
class light_Package;

// Renderer inputs shared by HUD glass and PiP objective-lens shaders.
namespace SvpLensLighting
{
void RegisterConstants(CResourceManager& resources);
void CaptureSceneLights(const light_Package& normal, const light_Package& pending);
void BindObjectLighting(IRenderable* owner);
}

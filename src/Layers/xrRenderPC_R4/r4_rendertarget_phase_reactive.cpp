#include "stdafx.h"
#include "Dlss/DlssIntegration.h"

// Monolith: reactive mask for DLSS / FSR 3.
//
// Particles, smoke, sparks, rain and glass are drawn after the deferred scene without depth or
// motion vectors of their own, so a temporal upscaler reprojects whatever is behind them and
// smears them. Both upscalers accept a mask of such pixels and then lean on this frame's colour
// there: DLSS as pInBiasCurrentColorMask, FSR 3 as its reactive mask.
//
// IX-Ray (develop 0ba2fbf9) makes every translucent forward shader write its coverage into an
// extra target. Here those shaders come from the installed mods (SSS, Atmospherics, particles),
// so instead the mask is derived from what those passes changed, like FSR 3's own reactive-mask
// generator:
//   phase_reactive_snapshot()  after water, before rain:   rt_Generic_0 -> rt_upscaler_opaque
//   phase_reactive_mask()      after forward rendering:    difference -> rt_upscaler_reactive
// (upscaler_reactive.ps). Nothing in any shader of the mods changes.
//
// Console (no restart): r__upscaler_reactive 0/1, r__upscaler_reactive_scale (difference ->
// mask), r__upscaler_reactive_cutoff (smaller differences are 0), r__upscaler_reactive_max
// (cap; FSR recommends staying below 1), r__upscaler_reactive_debug 1 (mask in magenta).
//
// Known limitations:
//   - Only with an upscaler active and without MSAA (neither upscaler runs with MSAA).
//   - Anything that changes the colour between the two points is marked: besides particles and
//     rain, also lens flares or HUD effects drawn in the forward pass.
//   - Water is drawn before the snapshot, so it is not marked (it is a surface with depth).
//   - Volumetric light (combined after forward rendering) is not marked.
//   - One full-screen copy and one full-screen pass at render resolution per frame.

bool CRenderTarget::reactive_active() const
{
	return ps_r__upscaler_reactive != 0 && dlss::Configured() && !RImplementation.o.dx10_msaa && s_upscaler_reactive &&
		rt_upscaler_reactive && rt_upscaler_opaque;
}

void CRenderTarget::phase_reactive_snapshot()
{
	reactive_snapshot_frame = u32(-1);
	if (!reactive_active())
		return;
	PROF_EVENT("phase_reactive_snapshot");
	HW.pContext->CopyResource(rt_upscaler_opaque->pTexture->surface_get(), rt_Generic_0->pTexture->surface_get());
	reactive_snapshot_frame = Device.dwFrame;
}

void CRenderTarget::phase_reactive_mask()
{
	reactive_valid_frame = u32(-1);
	if (!reactive_active() || reactive_snapshot_frame != Device.dwFrame)
		return;

	PIX_EVENT(phase_reactive_mask);
	PROF_EVENT("phase_reactive_mask");
	const u32 C = color_rgba(0, 0, 0, 255);

	u_setrt(rt_upscaler_reactive, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	RCache.set_Element(s_upscaler_reactive->E[0]);
	draw_fullscreen_quad(C);
	reactive_valid_frame = Device.dwFrame;

	if (ps_r__upscaler_reactive_debug && s_upscaler_reactive->E[1])
	{
		u_setrt(rt_Generic_0, 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);
		RCache.set_Element(s_upscaler_reactive->E[1]);
		draw_fullscreen_quad(C);
	}

	// Back to the targets forward rendering used, for what follows in phase_combine()
	u_setrt(rt_Generic_0, rt_Heat, rt_ssfx_motion_vectors, dlss::SceneDepth());
}

ID3D11Texture2D* CRenderTarget::reactive_texture() const
{
	return (reactive_valid_frame == Device.dwFrame && rt_upscaler_reactive) ? rt_upscaler_reactive->pSurface : nullptr;
}

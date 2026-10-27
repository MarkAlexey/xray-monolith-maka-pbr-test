#include "stdafx.h"

// Monolith: IX-Ray SSLR (screen-space local reflections), compute part.
//
// Ported from IX-Ray 1.6's CRenderTarget::phase_sslr() (LVutner / IX-Ray team), adapted to
// Monolith's raw D3D11 calls instead of IX-Ray's RHI. Runs inside phase_combine() before
// combine_1, so combine_1 (and Enhanced Shaders' hmodel through the compatibility patch)
// can read the result as s_refl.
//
// Passes (8x8 thread groups, full resolution):
//   r4_sslr_hiz 1 only: Hi-Z pyramid of this frame's depth (-1/z, min of 2x2 per level):
//     copy     s_position -> mip 0;  reduce   mip n - 1 -> mip n (one dispatch per level)
//   0 trace     G-buffer + rt_sslr_color, rt_sslr_pos_old (last main-view frame)
//                                                           -> rt_sslr, rt_sslr_data
//   1 resolve   rt_sslr + rt_sslr_data                      -> rt_sslr_temp
//   2 temporal  rt_sslr_temp + rt_sslr_old (history), rt_sslr_data, rt_sslr_pos_old
//                                                           -> rt_sslr (premultiplied)
//   then rt_sslr -> rt_sslr_old and rt_Position -> rt_sslr_pos_old for the next frame;
//   rt_sslr_color is refreshed in phase_combine() after combine_1.
//
// Known limitations (also documented in sslr_common.h):
//   - Disabled with MSAA (the G-buffer is multisampled there; the shaders read it with Load
//     on sample 0 only, which would alias at edges). RImplementation.o.sslr is false then.
//   - Not run for the second viewport (scope picture-in-picture). Its camera differs, so the
//     main view's result would land in the wrong places: binder_sslr_params (r4.cpp) sets the
//     intensity to 0 on those frames and the scope picture keeps cubemap reflections only.
//   - IX-Ray's offscreen cubemap reflections (VSLR) are a separate pass
//     (r4_rendertarget_phase_vslr.cpp) read by the consumer; misses fall back to the
//     cubemap reflection in the consumer.

// Hi-Z pyramid (r4_sslr_hiz, Stachowiak 2015 / IX-Ray develop 454d9ecd): R32F, full mip
// chain at scene resolution, one UAV per mip for the reduce passes, all mips readable by the
// trace as s_hiz. About 1.33 x 4 bytes per scene pixel (~6 MB at 1720x720, ~44 MB at 4K).
void CRenderTarget::create_sslr_hiz(u32 w, u32 h)
{
	destroy_sslr_hiz();

	u32 mips = 1;
	while ((_max(w, h) >> mips) > 0 && mips < SSLR_HIZ_MAX_MIPS)
		++mips;

	D3D11_TEXTURE2D_DESC td = {};
	td.Width = w;
	td.Height = h;
	td.MipLevels = mips;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R32_FLOAT;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	if (FAILED(HW.pDevice->CreateTexture2D(&td, nullptr, &t_sslr_hiz_surf)))
	{
		Msg("! IX-Ray SSLR: cannot create the Hi-Z pyramid, linear march instead");
		destroy_sslr_hiz();
		return;
	}
	sslr_hiz_mips = mips;
	for (u32 m = 0; m < mips; ++m)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
		uv.Format = DXGI_FORMAT_R32_FLOAT;
		uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uv.Texture2D.MipSlice = m;
		if (FAILED(HW.pDevice->CreateUnorderedAccessView(t_sslr_hiz_surf, &uv, &sslr_hiz_uav[m])))
		{
			Msg("! IX-Ray SSLR: cannot create a Hi-Z pyramid view, linear march instead");
			destroy_sslr_hiz();
			return;
		}
	}
	// CTexture::surface_set creates the shader view over every mip (dx10SH_Texture.cpp)
	t_sslr_hiz.create(r2_RT_sslr_hiz);
	t_sslr_hiz->surface_set(t_sslr_hiz_surf);
	Msg("- IX-Ray SSLR: Hi-Z pyramid %ux%u, %u mips", w, h, mips);
}

void CRenderTarget::destroy_sslr_hiz()
{
	if (t_sslr_hiz)
		t_sslr_hiz->surface_set(nullptr);
	t_sslr_hiz.destroy();
	for (auto& uav : sslr_hiz_uav)
		_RELEASE(uav);
	sslr_hiz_mips = 0;
	_RELEASE(t_sslr_hiz_surf);
}

static void sslr_bind_pass(ShaderElement* S)
{
	SPass& P = *(S->passes[0]);
	RCache.set_States(P.state);
	RCache.set_Constants(P.constants);
	RCache.set_Textures(P.T);
	RCache.set_CS(P.cs);
}

void CRenderTarget::phase_sslr()
{
	if (!RImplementation.o.sslr || !s_sslr || !rt_sslr || Device.m_SecondViewport.IsSVPFrame())
		return;

	PIX_EVENT(phase_sslr);
	PROF_EVENT("phase_sslr");

	const u32 w = RenderScreenWidth(); // scene resolution (DLSS / FSR3), the size of rt_sslr*
	const u32 h = RenderScreenHeight();
	const UINT groupsX = (w + 7u) / 8u;
	const UINT groupsY = (h + 7u) / 8u;

	// History is invalid on the first frame after the targets were (re)created (game start,
	// vid_restart, resolution change: the CRenderTarget and this member are new) or after a
	// gap of several frames (menu, loading). A gap of 2 is normal: with a scope's second
	// viewport active, main-view frames alternate with second-viewport frames.
	if (sslr_history_frame == u32(-1) || Device.dwFrame - sslr_history_frame > 2)
	{
		FLOAT zero[4] = { 0.f, 0.f, 0.f, 0.f };
		HW.pContext->ClearRenderTargetView(rt_sslr_old->pRT, zero);
		// Depth 0 reads as sky: every previous-frame visibility test fails, so neither the
		// colour copy nor the history is trusted until a real frame has been recorded
		HW.pContext->ClearRenderTargetView(rt_sslr_pos_old->pRT, zero);
	}
	sslr_history_frame = Device.dwFrame;

	const Fvector4 screen = { float(w), float(h), 1.f / float(w), 1.f / float(h) };

	// A UAV cannot be bound while the same texture is an output-merger target or SRV
	ID3D11RenderTargetView* oldrtv[8] = {};
	ID3D11DepthStencilView* olddsv = nullptr;
	ID3D11RenderTargetView* nullrtv[8] = {};
	HW.pContext->OMGetRenderTargets(8, oldrtv, &olddsv);
	HW.pContext->OMSetRenderTargets(8, nullrtv, nullptr);

	ID3D11UnorderedAccessView* nulluav[2] = { nullptr, nullptr };
	UINT counts[2] = { 0, 0 };

	auto dispatch = [&](u32 element, ID3D11UnorderedAccessView* const* uavs, UINT nuav)
	{
		sslr_bind_pass(&*(s_sslr->E[element]));
		RCache.set_c("m_previous", Matrix_previous);
		RCache.set_c("sslr_screen", screen);
		// sslr_params is bound by binder_sslr_params (r4.cpp)

		HW.pContext->CSSetUnorderedAccessViews(0, nuav, uavs, counts);
		RCache.Compute(groupsX, groupsY, 1);
		HW.pContext->CSSetUnorderedAccessViews(0, nuav, nulluav, counts);
		// Through the state caches: the next pass binds the same G-buffer textures, and a raw
		// unbind would make RCache skip them (R_Backend.h, unbind_CS_textures)
		RCache.unbind_CS_textures();
	};

	// Hi-Z pyramid of this frame's depth, before the trace reads it: level 0 from s_position,
	// then each level the min of the 2x2 below (sslr_hiz_copy.cs, sslr_hiz_reduce.cs)
	if (RImplementation.o.sslr_hiz && t_sslr_hiz_surf && s_sslr->E[3] && s_sslr->E[4])
	{
		PIX_EVENT(sslr_hiz);
		PROF_EVENT("phase_sslr_hiz");
		{
			ID3D11UnorderedAccessView* uav[1] = { sslr_hiz_uav[0] };
			dispatch(3, uav, 1);
		}
		D3D11_TEXTURE2D_DESC hd;
		t_sslr_hiz_surf->GetDesc(&hd);	// the pyramid's own size (it was made at scene resolution)
		sslr_bind_pass(&*(s_sslr->E[4]));
		for (u32 m = 1; m < sslr_hiz_mips; ++m)
		{
			const u32 mw = _max(hd.Width >> m, 1u), mh = _max(hd.Height >> m, 1u);
			ID3D11UnorderedAccessView* uav[2] = { sslr_hiz_uav[m - 1], sslr_hiz_uav[m] };
			HW.pContext->CSSetUnorderedAccessViews(0, 2, uav, counts);
			RCache.Compute((mw + 7u) / 8u, (mh + 7u) / 8u, 1);
		}
		HW.pContext->CSSetUnorderedAccessViews(0, 2, nulluav, counts);
		RCache.unbind_CS_textures();
	}

	{
		PIX_EVENT(sslr_render);
		ID3D11UnorderedAccessView* uav[2] = { rt_sslr->pUAView, rt_sslr_data->pUAView };
		dispatch(0, uav, 2);
	}
	{
		PIX_EVENT(sslr_filter);
		ID3D11UnorderedAccessView* uav[1] = { rt_sslr_temp->pUAView };
		dispatch(1, uav, 1);
	}
	{
		PIX_EVENT(sslr_temporal);
		ID3D11UnorderedAccessView* uav[1] = { rt_sslr->pUAView };
		dispatch(2, uav, 1);
	}

	// Keep this frame's result and G-buffer positions as next frame's history (IX-Ray copies
	// the result too; the positions are for the visibility tests, sslr_was_visible())
	HW.pContext->CopyResource(rt_sslr_old->pSurface, rt_sslr->pSurface);
	HW.pContext->CopyResource(rt_sslr_pos_old->pSurface, rt_Position->pSurface);

	// RCache tracks the compute shader it bound; unbind so later draws do not inherit it
	RCache.set_CS(nullptr);

	HW.pContext->OMSetRenderTargets(8, oldrtv, olddsv);
	for (auto& rtv : oldrtv)
		_RELEASE(rtv);
	_RELEASE(olddsv);
}

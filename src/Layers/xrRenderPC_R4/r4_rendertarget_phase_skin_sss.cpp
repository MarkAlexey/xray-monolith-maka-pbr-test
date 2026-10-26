#include "stdafx.h"

// Monolith: screen-space skin subsurface scattering for first-person PBR models (hands).
//
// Jorge Jimenez's separable SSS (Jimenez et al. 2015, "Separable Subsurface Scattering"); the
// shader side and its caveats are documented in gamedata\shaders\r3\skin_sss.h.
//
// Runs after all lights and the volumetric blur, before phase_combine():
//   0 horizontal  light accumulator (skin pixels: diffuse irradiance, specular marked in alpha)
//                 + s_position + s_surface                     -> rt_skin_sss_tmp (RGBA16F)
//   1 vertical    rt_skin_sss_tmp + s_surface                  -> rt_skin_sss (R11G11B10F)
// combine_1 reads rt_skin_sss as s_skin_sss for skin pixels only.
//
// Cost: two full-screen passes. Pixels that are not skin return after one or two loads; skin
// pixels read SKIN_SSS_TAPS (skin_sss.h, default 17) neighbours per pass. r4_skin_sss_timer 1
// logs the GPU time of both passes (D3D11 timestamp queries, read three frames later so the
// CPU never waits for the GPU).
//
// Known limitations:
//   - Off with MSAA (RImplementation.o.skin_sss is false there): the G-buffer and accumulator
//     are multisampled and the light shaders do not split skin pixels.
//   - Console r4_skin_sss needs a game restart (it changes the light and combine shaders); width,
//     strength and debug views apply at once.

// GPU timer: QUERIES frames in flight, each a disjoint query around two timestamps
struct skin_sss_timer
{
	static const u32 QUERIES = 4;
	ID3D11Query* disjoint[QUERIES] = {};
	ID3D11Query* t_begin[QUERIES] = {};
	ID3D11Query* t_end[QUERIES] = {};
	bool pending[QUERIES] = {};
	u32 slot = 0;
	double sum_ms = 0.0;
	u32 samples = 0;
	bool failed = false;

	bool create()
	{
		D3D11_QUERY_DESC dj = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
		D3D11_QUERY_DESC ts = { D3D11_QUERY_TIMESTAMP, 0 };
		for (u32 i = 0; i < QUERIES; ++i)
		{
			if (FAILED(HW.pDevice->CreateQuery(&dj, &disjoint[i])) || FAILED(HW.pDevice->CreateQuery(&ts, &t_begin[i])) ||
				FAILED(HW.pDevice->CreateQuery(&ts, &t_end[i])))
			{
				release();
				failed = true;
				Msg("! Skin SSS: GPU timestamp queries unavailable, r4_skin_sss_timer ignored");
				return false;
			}
		}
		return true;
	}

	void release()
	{
		for (u32 i = 0; i < QUERIES; ++i)
		{
			_RELEASE(disjoint[i]);
			_RELEASE(t_begin[i]);
			_RELEASE(t_end[i]);
			pending[i] = false;
		}
	}

	// Reads every finished measurement without waiting; unfinished ones stay pending
	void collect()
	{
		for (u32 i = 0; i < QUERIES; ++i)
		{
			if (!pending[i] || i == slot)
				continue;
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
			UINT64 a = 0, b = 0;
			if (HW.pContext->GetData(disjoint[i], &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
				HW.pContext->GetData(t_begin[i], &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
				HW.pContext->GetData(t_end[i], &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			pending[i] = false;
			if (dj.Disjoint || dj.Frequency == 0 || b < a)
				continue;
			sum_ms += double(b - a) * 1000.0 / double(dj.Frequency);
			++samples;
		}
		if (samples >= 120)
		{
			Msg("* Skin SSS: %.3f ms GPU (both blur passes, average of %u frames, %ux%u, %s)", sum_ms / double(samples),
				samples, RenderScreenWidth(), RenderScreenHeight(), ps_r4_skin_sss_debug ? "debug view on" : "debug off");
			sum_ms = 0.0;
			samples = 0;
		}
	}

	// begin() / end() around the measured passes. A slot still pending (GPU more than QUERIES
	// frames behind) is skipped for this frame instead of being reused.
	bool active = false;
	void begin()
	{
		active = false;
		if (failed || pending[slot])
			return;
		HW.pContext->Begin(disjoint[slot]);
		HW.pContext->End(t_begin[slot]);
		active = true;
	}
	void end()
	{
		if (!active)
			return;
		HW.pContext->End(t_end[slot]);
		HW.pContext->End(disjoint[slot]);
		pending[slot] = true;
		slot = (slot + 1) % QUERIES;
		collect();
	}
};

void CRenderTarget::release_skin_sss_timer()
{
	if (skin_sss_gpu_timer)
	{
		skin_sss_gpu_timer->release();
		xr_delete(skin_sss_gpu_timer);
	}
}

void CRenderTarget::phase_skin_sss()
{
	if (!RImplementation.o.skin_sss || !s_skin_sss || !rt_skin_sss || !rt_skin_sss_tmp)
		return;

	PIX_EVENT(phase_skin_sss);
	PROF_EVENT("phase_skin_sss");

	// GPU timer, created on first use and dropped again when switched off
	skin_sss_timer* timer = nullptr;
	if (ps_r4_skin_sss_timer)
	{
		if (!skin_sss_gpu_timer)
		{
			skin_sss_gpu_timer = xr_new<skin_sss_timer>();
			skin_sss_gpu_timer->create();
		}
		timer = skin_sss_gpu_timer;
	}
	else if (skin_sss_gpu_timer)
		release_skin_sss_timer();
	if (timer)
		timer->begin();

	const u32 C = color_rgba(0, 0, 0, 255);

	// 0: horizontal. Every pixel is written (0 where there is no skin), so no clear is needed.
	{
		PIX_EVENT(skin_sss_blur_h);
		u_setrt(rt_skin_sss_tmp, 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);
		RCache.set_Element(s_skin_sss->E[0]);
		draw_fullscreen_quad(C);
	}

	// 1: vertical
	{
		PIX_EVENT(skin_sss_blur_v);
		u_setrt(rt_skin_sss, 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);
		RCache.set_Element(s_skin_sss->E[1]);
		draw_fullscreen_quad(C);
	}

	if (timer)
		timer->end();
}

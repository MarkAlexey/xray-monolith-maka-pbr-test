#pragma once
// Full-screen quad from an immutable vertex buffer, after IX-Ray 1eea3317 "Optimize fullscreen
// rendering geometry" (LVutner / ForserX): the post-process passes used to write the same four
// vertices into the dynamic vertex buffer on every draw.
//
// The quad is exactly what those passes wrote: (0,h) (0,0) (w,h) (w,0) in pixels, z = EPS_S,
// w = 1, UV 0..1, drawn as FVF::F_TL with RCache.QuadIB. w/h is the screen size the passes use
// (CRenderTarget::draw_fullscreen_quad passes it: Device.dwWidth/dwHeight, or on the DLSS branch
// RenderScreenWidth/Height, which is the render resolution inside the upscaled scene). The vertex
// colour is part of the vertex (stub_notransform_t passes it on). So there is one buffer per
// colour and size (at most MaxQuads; when they run out, all are dropped and rebuilt as needed).
//
// Caveats
//  - Only for passes that draw that exact quad. Passes with a reduced size, UV offsets or a
//    different z keep filling the dynamic buffer themselves.
//  - DX9 (R2) passes use half-texel UVs that depend on the size, and R2 is not what PBR runs on:
//    there the quad is still written into the dynamic buffer every draw, as before.
//  - A new size (resolution or upscaler quality change) simply adds buffers; the render target
//    and its quads are recreated on vid_restart anyway.
//  - Owner thread only: the recorded (qkff99 MT) light passes draw their own quads.
#include "FVF.h"
#if defined(USE_DX10) || defined(USE_DX11)
#	include "../xrRenderDX10/dx10BufferUtils.h"
#endif

class CFullscreenQuad
{
	static constexpr u32 MaxQuads = 8;

	struct Quad
	{
		u32 color = 0, width = 0, height = 0;
		ID3DVertexBuffer* vb = nullptr;
		ref_geom geom;
	};

	Quad quads[MaxQuads];
	u32 count = 0;

	static void Fill(FVF::TL* v, float w, float h, u32 color, const Fvector2& p0, const Fvector2& p1)
	{
		v[0].set(0, h, EPS_S, 1.f, color, p0.x, p1.y);
		v[1].set(0, 0, EPS_S, 1.f, color, p0.x, p0.y);
		v[2].set(w, h, EPS_S, 1.f, color, p1.x, p1.y);
		v[3].set(w, 0, EPS_S, 1.f, color, p1.x, p0.y);
	}

	// The dynamic buffer path (DX9, and the fallback)
	static void DrawDynamic(u32 color, ref_geom& dynamic, float w, float h, const Fvector2& p0, const Fvector2& p1)
	{
		u32 offset = 0;
		FVF::TL* v = (FVF::TL*)RCache.Vertex.Lock(4, dynamic->vb_stride, offset);
		Fill(v, w, h, color, p0, p1);
		RCache.Vertex.Unlock(4, dynamic->vb_stride);
		RCache.set_Geometry(dynamic);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	}

public:
	~CFullscreenQuad() { Destroy(); }

	void Destroy()
	{
		for (u32 i = 0; i < count; ++i)
		{
			quads[i].geom.destroy();
			_RELEASE(quads[i].vb);
		}
		count = 0;
	}

	// Draws the quad with the element (and constants) already set. dynamic = g_combine.
	void Draw(u32 color, ref_geom& dynamic, u32 width, u32 height)
	{
		const float w = float(width), h = float(height);
#if defined(USE_DX10) || defined(USE_DX11)
		const Fvector2 p0 = { 0.f, 0.f }, p1 = { 1.f, 1.f };
		Quad* quad = nullptr;
		for (u32 i = 0; i < count && !quad; ++i)
			if (quads[i].color == color && quads[i].width == width && quads[i].height == height)
				quad = &quads[i];
		if (!quad && count == MaxQuads)
			Destroy();
		if (!quad)
		{
			FVF::TL verts[4];
			Fill(verts, w, h, color, p0, p1);
			Quad& q = quads[count];
			if (SUCCEEDED(dx10BufferUtils::CreateVertexBuffer(&q.vb, verts, sizeof(verts), true)) && q.vb)
			{
				q.color = color;
				q.width = width;
				q.height = height;
				q.geom.create(FVF::F_TL, q.vb, RCache.QuadIB);
				quad = &quads[count++];
			}
		}
		if (quad)
		{
			RCache.set_Geometry(quad->geom);
			RCache.Render(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
			return;
		}
#else
		const Fvector2 p0 = { 0.5f / w, 0.5f / h }, p1 = { (w + 0.5f) / w, (h + 0.5f) / h };
#endif
		DrawDynamic(color, dynamic, w, h, p0, p1);
	}
};

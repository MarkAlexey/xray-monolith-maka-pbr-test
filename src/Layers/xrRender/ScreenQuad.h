#pragma once

//////////////////////////////////////////////////////////////////////////////////////////
// Monolith: screen quads from static vertex buffers.
//
// Idea from IX-Ray 1eea3317 ("Optimize fullscreen rendering geometry", LVutner), which keeps
// one prebuilt quad for its screen post-process pass. Here the post-process, SSS and bloom
// passes each rebuilt the same 4-vertex screen quad in the dynamic vertex buffer every draw
// (Map / write / Unmap, a few dozen times per frame). CScreenQuads keeps the quads a pass
// asks for again and again in small immutable vertex buffers and binds those instead.
//
// Use (CRenderTarget members, see r4_rendertarget.h):
//     quad_fill(x0, y0, x1, y1, u0, v0, u1, v1, z, w, color, Offset);
//     ...
//     RCache.set_Geometry(quad_geom());
//     RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
// The vertices are exactly the ones the passes wrote before, in the same order
// ((x0,y1) (x0,y0) (x1,y1) (x1,y0), drawn with RCache.QuadIB), so the picture cannot change.
//
// When a static quad is used:
//   - only on DX11 (R4) and with r__static_screen_quad 1; R2 / R3 always take the dynamic path;
//   - only for a quad whose exact vertices were already asked for on an EARLIER frame: values
//     that change every frame (jitter, animated offsets) never get a buffer of their own;
//   - at most SQ_MAX_STATIC different quads per render target. Past that, the dynamic path.
// Everything else goes through the dynamic buffer exactly as before.
//
// Caveats
//   - The cache lives in the render target, which is rebuilt on vid_restart / resolution
//     change, so quads sized from the old resolution do not linger.
//   - Matching is bit-exact on the 10 floats and the colour. A pass that computes its size
//     slightly differently each frame (float noise) simply stays on the dynamic path.
//   - Before the buffers are released the dynamic quad geometry is bound again, so the
//     backend's cached vertex buffer is never one of them (a later buffer allocated at the
//     same address could otherwise be mistaken for one that is already bound).
//////////////////////////////////////////////////////////////////////////////////////////

#include "FVF.h"
#ifdef USE_DX11
#	include "../xrRenderDX10/dx10BufferUtils.h"
#endif

// 1 = reuse static quads (DX11), 0 = always fill the dynamic buffer (the old behaviour)
extern ECORE_API int ps_r__static_screen_quad;

class CScreenQuads
{
public:
	// Different static quads kept per render target
	static constexpr u32 SQ_MAX_STATIC = 48;
	// Quads seen once and waiting for a repeat on a later frame
	static constexpr u32 SQ_MAX_PENDING = 64;

	CScreenQuads() = default;
	CScreenQuads(const CScreenQuads&) = delete;
	CScreenQuads& operator=(const CScreenQuads&) = delete;
	~CScreenQuads() { destroy(); }

	// Writes (or finds) the quad and remembers which geometry to bind. Offset is the base
	// vertex for RCache.Render (0 for a static quad).
	void fill(ref_geom& dynamic_geom, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
	          float v1, float z, float w, u32 color, u32& Offset)
	{
#ifdef USE_DX11
		if (ps_r__static_screen_quad)
		{
			key k;
			k.set(x0, y0, x1, y1, u0, v0, u1, v1, z, w, color);
			for (u32 i = 0; i < m_count; ++i)
			{
				if (m_static[i].k == k)
				{
					m_last = &m_static[i].geom;
					Offset = 0;
					return;
				}
			}
			m_dynamic = &dynamic_geom;
			if (m_count < SQ_MAX_STATIC && seen_before(k) && create_static(k, dynamic_geom))
			{
				m_last = &m_static[m_count - 1].geom;
				Offset = 0;
				return;
			}
		}
#endif
		m_dynamic = &dynamic_geom;
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, dynamic_geom->vb_stride, Offset);
		pv->set(x0, y1, z, w, color, u0, v1); pv++;
		pv->set(x0, y0, z, w, color, u0, v0); pv++;
		pv->set(x1, y1, z, w, color, u1, v1); pv++;
		pv->set(x1, y0, z, w, color, u1, v0); pv++;
		RCache.Vertex.Unlock(4, dynamic_geom->vb_stride);
		m_last = &dynamic_geom;
	}

	// Geometry of the last fill()
	ref_geom& geom()
	{
		VERIFY(m_last);
		return *m_last;
	}

	void destroy()
	{
#ifdef USE_DX11
		if (m_count)
		{
			// Rebind the dynamic quad geometry first: the backend caches the bound vertex buffer
			if (m_dynamic && *m_dynamic)
				RCache.set_Geometry(*m_dynamic);
			for (u32 i = 0; i < m_count; ++i)
			{
				m_static[i].geom.destroy();
				_RELEASE(m_static[i].vb);
			}
		}
		m_count = 0;
		m_pending_count = 0;
#endif
		m_last = nullptr;
		m_dynamic = nullptr;
	}

private:
	struct key
	{
		float f[10];
		u32 c;

		void set(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float z, float w,
		         u32 color)
		{
			f[0] = x0; f[1] = y0; f[2] = x1; f[3] = y1;
			f[4] = u0; f[5] = v0; f[6] = u1; f[7] = v1;
			f[8] = z; f[9] = w;
			c = color;
		}

		// Bit-exact: the static buffer holds exactly what the dynamic path would write
		bool operator==(const key& o) const { return 0 == memcmp(this, &o, sizeof(key)); }
	};

	ref_geom* m_last = nullptr;
	ref_geom* m_dynamic = nullptr; // the owner's dynamic quad geometry (outlives this object)

#ifdef USE_DX11
	struct entry
	{
		key k;
		ID3DVertexBuffer* vb = nullptr;
		ref_geom geom;
	};

	struct pending
	{
		key k;
		u32 frame;
	};

	entry m_static[SQ_MAX_STATIC];
	u32 m_count = 0;
	pending m_pending[SQ_MAX_PENDING];
	u32 m_pending_count = 0;
	u32 m_pending_next = 0;

	// True when the quad was already requested on an earlier frame
	bool seen_before(const key& k)
	{
		const u32 frame = Device.dwFrame;
		for (u32 i = 0; i < m_pending_count; ++i)
		{
			if (m_pending[i].k == k)
			{
				if (m_pending[i].frame != frame)
				{
					m_pending[i] = m_pending[--m_pending_count];
					return true;
				}
				return false;
			}
		}
		// Ring: quads that never repeat are pushed out by newer ones
		pending& p = m_pending_count < SQ_MAX_PENDING ? m_pending[m_pending_count++] : m_pending[m_pending_next++ % SQ_MAX_PENDING];
		p.k = k;
		p.frame = frame;
		return false;
	}

	bool create_static(const key& k, ref_geom& dynamic_geom)
	{
		FVF::TL verts[4];
		verts[0].set(k.f[0], k.f[3], k.f[8], k.f[9], k.c, k.f[4], k.f[7]);
		verts[1].set(k.f[0], k.f[1], k.f[8], k.f[9], k.c, k.f[4], k.f[5]);
		verts[2].set(k.f[2], k.f[3], k.f[8], k.f[9], k.c, k.f[6], k.f[7]);
		verts[3].set(k.f[2], k.f[1], k.f[8], k.f[9], k.c, k.f[6], k.f[5]);

		entry& e = m_static[m_count];
		if (FAILED(dx10BufferUtils::CreateVertexBuffer(&e.vb, verts, sizeof(verts), true)) || !e.vb)
		{
			e.vb = nullptr;
			return false;
		}
		e.k = k;
		// Same layout and index buffer as the dynamic quad geometry (FVF::F_TL + QuadIB)
		e.geom.create(FVF::F_TL, e.vb, RCache.QuadIB);
		VERIFY(e.geom->vb_stride == dynamic_geom->vb_stride);
		++m_count;
		return true;
	}
#endif
};

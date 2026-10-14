// FVisual.h: interface for the FVisual class.
//
//////////////////////////////////////////////////////////////////////
#ifndef FVisualH
#define FVisualH
#pragma once

#ifdef _EDITOR
#	include "fbasicvisual.h"
#else
#	include "fbasicvisual.h"
#endif

#include <memory>
class Fvisual : public dxRender_Visual, public IRender_Mesh
{
public:
	IRender_Mesh* m_fast;
#if defined(USE_DX11) && RENDER == R_R4
    bool immutable_level_geometry = false;
    struct StaticBatch
    {
        ref_geom geometry;
        ID3DVertexBuffer* vertices = nullptr;
        ID3DIndexBuffer* indices = nullptr;
        u32 base_vertex = 0, vertex_count = 0;
        ~StaticBatch();
    };
    std::shared_ptr<StaticBatch> static_batch;
    ref_geom static_source_geometry;
    u32 static_source_base = 0, static_source_vertices = 0, static_source_first = 0, static_source_count = 0;
    u32 static_batch_first_index = 0;
    xr_vector<u16> static_source_indices;
    static void BuildStaticBatches(const xr_vector<dxRender_Visual*>& visuals);
    static bool StaticPassSafe(const SPass& pass);
    bool StaticBatchValid() const;
#endif
public:
	virtual void Render(float LOD); // LOD - Level Of Detail  [0.0f - min, 1.0f - max], Ignored ?
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();

	Fvisual();
	virtual ~Fvisual();
};

#endif

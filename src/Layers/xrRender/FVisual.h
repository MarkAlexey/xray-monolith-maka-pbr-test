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
#if defined(USE_DX11) && RENDER == R_R4
#include "ShadowBatchPolicy.h"
#endif
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
    struct ShadowMember
    {
        ref_geom source_geometry;
        ref_shader source_shader;
        ref_selement source_element;
        ID3DIndexBuffer* source_indices = nullptr; // Identity only; the level owns the immutable source IB.
        u32 base_vertex, vertex_count, first_index, index_count;
    };
    struct ShadowBatch : StaticBatch
    {
        ref_pass pass;
        xr_vector<ShadowMember> members;
        u32 index_count = 0, original_vertices = 0;
        u32 vertex_revision = 0, pixel_revision = 0;
        aoe_render::ShadowBatchSelection selection;
        bool binding_valid = false;
    };
    std::shared_ptr<ShadowBatch> shadow_batch;
    u32 shadow_batch_member = 0;
    xr_vector<u16> shadow_source_indices; // Load-only, released after group creation.
    static bool ShadowBatchPassSafe(const SPass& pass);
    static void BuildShadowBatches(const xr_vector<dxRender_Visual*>& visuals);
    bool ShadowBatchMemberValid() const;

    bool DrawShadowBatch(u32 ticket);
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

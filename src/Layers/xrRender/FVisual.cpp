// FVisual.cpp: implementation of the FVisual class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(default:4995)

#include "../../xrEngine/fmesh.h"
#include "fvisual.h"
#if defined(USE_DX11) && RENDER == R_R4
#include "StaticDrawPacketPolicy.h"
#endif

#include "../xrRenderDX10/dx10BufferUtils.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Fvisual::Fvisual() : dxRender_Visual()
{
	m_fast = 0;
}

Fvisual::~Fvisual()
{
	HW.stats_manager.decrement_stats_vb(p_rm_Vertices);
	HW.stats_manager.decrement_stats_ib(p_rm_Indices);
	xr_delete(m_fast);
}

void Fvisual::Release()
{
#if defined(USE_DX11) && RENDER == R_R4
    static_batch.reset();
    static_source_geometry.destroy();
    static_source_indices.clear_and_free();
    immutable_level_geometry = false;
#endif
	dxRender_Visual::Release();
}

void Fvisual::Load(const char* N, IReader* data, u32 dwFlags)
{
	dxRender_Visual::Load(N, data, dwFlags);

	D3DVERTEXELEMENT9 dcl [MAX_FVF_DECL_SIZE];
	D3DVERTEXELEMENT9* vFormat = 0;
	dwPrimitives = 0;
	BOOL loaded_v = false;

	if (data->find_chunk(OGF_GCONTAINER))
	{
#ifndef _EDITOR
		// verts
		u32 ID = data->r_u32();
		vBase = data->r_u32();
		vCount = data->r_u32();

		VERIFY(NULL==p_rm_Vertices);

		p_rm_Vertices = RImplementation.getVB(ID);
		p_rm_Vertices->AddRef();
		vFormat = RImplementation.getVB_Format(ID);
		loaded_v = true;

		// indices
		ID = data->r_u32();
		iBase = data->r_u32();
		iCount = data->r_u32();
		dwPrimitives = iCount / 3;

		VERIFY(NULL==p_rm_Indices);
		p_rm_Indices = RImplementation.getIB(ID);
		p_rm_Indices->AddRef();
#if defined(USE_DX11) && RENDER == R_R4
        if (Type == MT_NORMAL && ID < RImplementation.levelIBData.size())
        {
            const auto& indices = RImplementation.levelIBData[ID];
            if (iBase <= indices.size() && iCount <= indices.size() - iBase)
                static_source_indices.assign(indices.begin() + iBase, indices.begin() + iBase + iCount);
        }
#endif
#endif
#if (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
		// check for fast-vertices
		if (data->find_chunk(OGF_FASTPATH))
		{
			destructor<IReader> geomdef(data->open_chunk(OGF_FASTPATH));
			destructor<IReader> def(geomdef().open_chunk(OGF_GCONTAINER));

			// we have fast-mesh
			m_fast = xr_new<IRender_Mesh>();

			// verts
			D3DVERTEXELEMENT9* fmt = 0;
			ID = def().r_u32();
			m_fast->vBase = def().r_u32();
			m_fast->vCount = def().r_u32();

			VERIFY(NULL==m_fast->p_rm_Vertices);
			m_fast->p_rm_Vertices = RImplementation.getVB(ID, true);
			m_fast->p_rm_Vertices->AddRef();
			fmt = RImplementation.getVB_Format(ID, true);

			// indices
			ID = def().r_u32();
			m_fast->iBase = def().r_u32();
			m_fast->iCount = def().r_u32();
			m_fast->dwPrimitives = m_fast->iCount / 3;

			VERIFY(NULL==m_fast->p_rm_Indices);
			m_fast->p_rm_Indices = RImplementation.getIB(ID, true);
			m_fast->p_rm_Indices->AddRef();

			// geom
			m_fast->rm_geom.create(fmt, m_fast->p_rm_Vertices, m_fast->p_rm_Indices);
		}
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
	}

	// read vertices
	if (!loaded_v && (dwFlags & VLOAD_NOVERTICES) == 0)
	{
		if (data->find_chunk(OGF_VCONTAINER))
		{
			R_ASSERT2(0, "pls notify andy about this.");
#ifndef _EDITOR
			u32 ID = data->r_u32();
			vBase = data->r_u32();
			vCount = data->r_u32();
			VERIFY(NULL==p_rm_Vertices);
			p_rm_Vertices = RImplementation.getVB(ID);
			p_rm_Vertices->AddRef();
			vFormat = RImplementation.getVB_Format(ID);
#endif
		}
		else
		{
			R_ASSERT(data->find_chunk(OGF_VERTICES));
			vBase = 0;
			u32 fvf = data->r_u32();
			CHK_DX(D3DXDeclaratorFromFVF(fvf,dcl));
			vFormat = dcl;
			vCount = data->r_u32();
			u32 vStride = D3DXGetFVFVertexSize(fvf);

#if defined(USE_DX10) || defined(USE_DX11)
			VERIFY(NULL==p_rm_Vertices);
			R_CHK(dx10BufferUtils::CreateVertexBuffer(&p_rm_Vertices, data->pointer(), vCount*vStride));
			HW.stats_manager.increment_stats_vb(p_rm_Vertices);
#else	//	USE_DX10
			BOOL bSoft = HW.Caps.geometry.bSoftware;
			u32 dwUsage = D3DUSAGE_WRITEONLY | (bSoft ? D3DUSAGE_SOFTWAREPROCESSING : 0);
			BYTE* bytes = 0;
			VERIFY(NULL==p_rm_Vertices);
			R_CHK(HW.pDevice->CreateVertexBuffer (vCount*vStride,dwUsage,0,D3DPOOL_MANAGED,&p_rm_Vertices,0));
			HW.stats_manager.increment_stats_vb(p_rm_Vertices);
			R_CHK(p_rm_Vertices->Lock(0,0,(void**)&bytes,0));
			CopyMemory(bytes, data->pointer(), vCount*vStride);
			p_rm_Vertices->Unlock();
#endif	//	USE_DX10
		}
	}

	// indices
	if (!loaded_v)
	{
		dwPrimitives = 0;
		if (data->find_chunk(OGF_ICONTAINER))
		{
			R_ASSERT2(0, "pls notify andy about this.");
#ifndef _EDITOR
			u32 ID = data->r_u32();
			iBase = data->r_u32();
			iCount = data->r_u32();
			dwPrimitives = iCount / 3;
			VERIFY(NULL==p_rm_Indices);
			p_rm_Indices = RImplementation.getIB(ID);
			p_rm_Indices->AddRef();
#endif
		}
		else
		{
			R_ASSERT(data->find_chunk(OGF_INDICES));
			iBase = 0;
			iCount = data->r_u32();
			dwPrimitives = iCount / 3;

#if defined(USE_DX10) || defined(USE_DX11)
			//BOOL	bSoft		= HW.Caps.geometry.bSoftware || (dwFlags&VLOAD_FORCESOFTWARE);
			//u32		dwUsage		= /*D3DUSAGE_WRITEONLY |*/ (bSoft?D3DUSAGE_SOFTWAREPROCESSING:0);	// indices are read in model-wallmarks code
			//BYTE*	bytes		= 0;

			//VERIFY				(NULL==p_rm_Indices);
			//R_CHK				(HW.pDevice->CreateIndexBuffer(iCount*2,dwUsage,D3DFMT_INDEX16,D3DPOOL_MANAGED,&p_rm_Indices,0));
			//R_CHK				(p_rm_Indices->Lock(0,0,(void**)&bytes,0));
			//CopyMemory		(bytes, data->pointer(), iCount*2);

			VERIFY(NULL==p_rm_Indices);
			R_CHK(dx10BufferUtils::CreateIndexBuffer(&p_rm_Indices, data->pointer(), iCount*2));
			HW.stats_manager.increment_stats_ib(p_rm_Indices);
#else	//	USE_DX10
			BOOL bSoft = HW.Caps.geometry.bSoftware;
			u32 dwUsage = /*D3DUSAGE_WRITEONLY |*/ (bSoft ? D3DUSAGE_SOFTWAREPROCESSING : 0);
			// indices are read in model-wallmarks code
			BYTE* bytes = 0;

			VERIFY(NULL==p_rm_Indices);
			R_CHK(HW.pDevice->CreateIndexBuffer(iCount*2,dwUsage,D3DFMT_INDEX16,D3DPOOL_MANAGED,&p_rm_Indices,0));
			HW.stats_manager.increment_stats_ib(p_rm_Indices);
			R_CHK(p_rm_Indices->Lock(0,0,(void**)&bytes,0));
			CopyMemory(bytes, data->pointer(), iCount*2);
			p_rm_Indices->Unlock();
#endif	//	USE_DX10
		}
	}

	if (dwFlags & VLOAD_NOVERTICES)
		return;
	else
		rm_geom.create(vFormat, p_rm_Vertices, p_rm_Indices);
}

void Fvisual::Render(float)
{
	PROF_EVENT("Fvisual::Render");
#if (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
	if (m_fast && RImplementation.phase == CRender::PHASE_SMAP && !RCache.is_TessEnabled())
	{
		RCache.set_Geometry(m_fast->rm_geom);
		RCache.Render(D3DPT_TRIANGLELIST, m_fast->vBase, 0, m_fast->vCount, m_fast->iBase, m_fast->dwPrimitives);
		RCache.stat.r.s_static.add(m_fast->vCount);
	}
	else
	{
		RCache.set_Geometry(rm_geom);
		RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, iBase, dwPrimitives);
		RCache.stat.r.s_static.add(vCount);
	}
#else // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
	RCache.set_Geometry(rm_geom);
	RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, iBase, dwPrimitives);
	RCache.stat.r.s_static.add(vCount);
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
}

#define PCOPY(a)	a = pFrom->a

void Fvisual::Copy(dxRender_Visual* pSrc)
{
	dxRender_Visual::Copy(pSrc);

	Fvisual* pFrom = fast_dynamic_cast<Fvisual*>(pSrc);

	PCOPY(rm_geom);

	PCOPY(p_rm_Vertices);
	if (p_rm_Vertices) p_rm_Vertices->AddRef();
	PCOPY(vBase);
	PCOPY(vCount);

	PCOPY(p_rm_Indices);
	if (p_rm_Indices) p_rm_Indices->AddRef();
	PCOPY(iBase);
	PCOPY(iCount);
	PCOPY(dwPrimitives);

	PCOPY(m_fast);
}

#if defined(USE_DX11) && RENDER == R_R4
bool Fvisual::StaticPassSafe(const SPass& pass)
{
    return pass.vs && pass.ps && pass.state && pass.state->state &&
        pass.vs->static_draw_safe && pass.ps->static_draw_safe && pass.state->state->StaticPacketSafe() &&
        !(pass.gs && pass.gs->gs) && !(pass.hs && pass.hs->sh) && !(pass.ds && pass.ds->sh) && !(pass.cs && pass.cs->sh);
}

bool Fvisual::StaticBatchValid() const
{
    return immutable_level_geometry && static_batch && rm_geom == static_source_geometry &&
        rm_geom && rm_geom->vb == static_batch->vertices && rm_geom->dcl == static_batch->geometry->dcl &&
        rm_geom->vb_stride == static_batch->geometry->vb_stride && vBase == static_source_base &&
        vCount == static_source_vertices && iBase == static_source_first && iCount == static_source_count &&
        dwPrimitives == iCount / 3;
}
Fvisual::StaticBatch::~StaticBatch()
{
    geometry.destroy();
    if (indices) HW.stats_manager.decrement_stats_ib(indices);
    _RELEASE(indices);
    _RELEASE(vertices);
}

void Fvisual::BuildStaticBatches(const xr_vector<dxRender_Visual*>& visuals)
{
    PROF_EVENT("Render.StaticBatch.Build.Level");
    R_ASSERT(GetCurrentThreadId() == Device.mmThreadID);
    struct Candidate { Fvisual* visual; int cell[3]; };
    xr_vector<Candidate> candidates;
    for (auto* visual : visuals)
    {
        if (visual->Type != MT_NORMAL) continue;
        auto* mesh = static_cast<Fvisual*>(visual);
        mesh->static_batch.reset();
        if (mesh->static_source_indices.empty() || !mesh->rm_geom || !mesh->shader || !mesh->immutable_level_geometry) continue;
        bool useful = false, compatible = true;
        for (u32 element_id = 0; element_id < 2; ++element_id)
        {
            const auto* element = mesh->shader->E[element_id]._get();
            if (!element || element->flags.iPriority / 2 != 0 || element->flags.bStrictB2F ||
                element->flags.bLandscape || element->flags.isWater || element->flags.bWmark)
            {
                if (element) compatible = false;
                continue;
            }
            for (const auto& pass : element->passes)
                if (pass && Fvisual::StaticPassSafe(*pass)) useful = true;
        }
        if (!useful || !compatible || !mesh->vCount || !mesh->iCount || mesh->iCount % 3 ||
            mesh->static_source_indices.size() != mesh->iCount || mesh->vBase > UINT32_MAX - mesh->vCount) continue;
        bool valid = true;
        for (u16 index : mesh->static_source_indices) if (index >= mesh->vCount) { valid = false; break; }
        if (!valid) continue;
        const auto& p = mesh->vis.sphere.P;
        if (!_valid(p) || _abs(p.x) > 1000000.f || _abs(p.y) > 1000000.f || _abs(p.z) > 1000000.f) continue;
        candidates.push_back({mesh, {iFloor(p.x / 16.f), iFloor(p.y / 16.f), iFloor(p.z / 16.f)}});
    }
    auto same_group = [](const Candidate& a, const Candidate& b)
    {
        return a.visual->shader == b.visual->shader && a.visual->rm_geom->vb == b.visual->rm_geom->vb &&
            a.visual->rm_geom->dcl == b.visual->rm_geom->dcl && a.visual->rm_geom->vb_stride == b.visual->rm_geom->vb_stride &&
            a.cell[0] == b.cell[0] && a.cell[1] == b.cell[1] && a.cell[2] == b.cell[2];
    };
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
    {
        const void* ka[] = {a.visual->shader._get(), a.visual->rm_geom->vb, a.visual->rm_geom->dcl._get()};
        const void* kb[] = {b.visual->shader._get(), b.visual->rm_geom->vb, b.visual->rm_geom->dcl._get()};
        for (unsigned i = 0; i < 3; ++i) if (ka[i] != kb[i]) return std::less<const void*>()(ka[i], kb[i]);
        if (a.visual->rm_geom->vb_stride != b.visual->rm_geom->vb_stride)
            return a.visual->rm_geom->vb_stride < b.visual->rm_geom->vb_stride;
        for (unsigned i = 0; i < 3; ++i) if (a.cell[i] != b.cell[i]) return a.cell[i] < b.cell[i];
        return a.visual->vBase < b.visual->vBase;
    });
    u32 batches = 0, members = 0, index_bytes = 0;
    for (size_t begin = 0; begin < candidates.size();)
    {
        auto* first = candidates[begin].visual;
        const u32 base = first->vBase;
        u32 vertex_end = base, count = 0;
        size_t end = begin;
        // Bounds are geometry/index limits, never a per-frame work allowance.
        while (end < candidates.size() && end - begin < 64 && same_group(candidates[begin], candidates[end]))
        {
            auto* mesh = candidates[end].visual;
            const u32 next_end = _max(vertex_end, mesh->vBase + mesh->vCount);
            if (!aoe_render::static_vertex_span(base, next_end) || mesh->iCount > 65536 - count) break;
            vertex_end = next_end; count += mesh->iCount; ++end;
        }
        if (end - begin > 1 && count * sizeof(u16) <= 64u * 1024u * 1024u - index_bytes)
        {
            xr_vector<u16> indices;
            indices.reserve(count);
            for (size_t i = begin; i < end; ++i)
            {
                auto* mesh = candidates[i].visual;
                for (u16 index : mesh->static_source_indices) indices.push_back(u16(index + mesh->vBase - base));
            }
            auto batch = std::make_shared<StaticBatch>();
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = count * sizeof(u16);
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = indices.data();
            const HRESULT result = HW.pDevice->CreateBuffer(&description, &data, &batch->indices);
            if (SUCCEEDED(result))
            {
                HW.stats_manager.increment_stats_ib(batch->indices);
                batch->vertices = first->rm_geom->vb;
                batch->vertices->AddRef();
                batch->base_vertex = base; batch->vertex_count = vertex_end - base;
                batch->geometry.create(first->rm_geom->dcl->dcl_code.data(), batch->vertices, batch->indices);
                u32 offset = 0;
                for (size_t i = begin; i < end; ++i)
                {
                    auto* mesh = candidates[i].visual;
                    mesh->static_batch = batch;
                    mesh->static_source_geometry = mesh->rm_geom;
                    mesh->static_source_base = mesh->vBase;
                    mesh->static_source_vertices = mesh->vCount;
                    mesh->static_source_first = mesh->iBase;
                    mesh->static_source_count = mesh->iCount;
                    mesh->static_batch_first_index = offset;
                    offset += mesh->iCount;
                }
                ++batches; members += u32(end - begin); index_bytes += count * sizeof(u16);
            }
            else
            {
                _RELEASE(batch->indices); // No stats were registered on the failed allocation.
                Msg("! Static batch index allocation failed (%08x); keeping original meshes", u32(result));
            }
        }
        begin = end > begin ? end : begin + 1;
    }
    for (auto* visual : visuals)
        if (visual->Type == MT_NORMAL) static_cast<Fvisual*>(visual)->static_source_indices.clear_and_free();
    PROF_TAG("Groups", batches);
    PROF_TAG("Members", members);
    PROF_TAG("Index bytes", index_bytes);
    Msg("* Static batches: %u groups, %u meshes, %u KB indices", batches, members, index_bytes / 1024);
}

#endif

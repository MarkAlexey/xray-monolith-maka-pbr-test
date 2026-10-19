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
extern bool IsNativeShadowMatrix(R_constant* constant);
extern bool NativeShadowHandlers(const R_constant_table* table);
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
    shadow_batch.reset();
    shadow_source_indices.clear_and_free();
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
#if defined(USE_DX11) && RENDER == R_R4
            if (Type == MT_NORMAL && ID < RImplementation.levelShadowIBData.size())
            {
                const auto& indices = RImplementation.levelShadowIBData[ID];
                if (m_fast->iBase <= indices.size() && m_fast->iCount <= indices.size() - m_fast->iBase)
                    shadow_source_indices.assign(indices.begin() + m_fast->iBase, indices.begin() + m_fast->iBase + m_fast->iCount);
            }
#endif

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
    // Level loading owns the visuals before publication, including PPL callers.
    // These builders create device resources; they do not use the immediate context.
    R_ASSERT(!RImplementation.b_loaded);
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

#if defined(USE_DX11) && RENDER == R_R4
bool Fvisual::ShadowBatchPassSafe(const SPass& pass)
{
    return pass.vs && pass.ps && pass.state && pass.state->state && pass.constants &&
        pass.vs->shadow_depth_cache_safe && pass.ps->shadow_batch_safe && NativeShadowHandlers(pass.constants._get()) &&
        pass.state->state->ShadowBatchSafe() && IsNativeShadowMatrix(pass.constants->get("m_WVP")) &&
        !(pass.gs && pass.gs->gs) && !(pass.hs && pass.hs->sh) && !(pass.ds && pass.ds->sh) && !(pass.cs && pass.cs->sh);
}

namespace
{
bool shadow_element_safe(const ShaderElement* element)
{
    return element && element->passes.size() == 1 && element->flags.iPriority / 2 == 0 &&
        !element->flags.bStrictB2F && !element->flags.bEmissive && !element->flags.bDistort &&
        !element->flags.bWmark && !element->flags.isWater && !element->flags.iScopeLense;
}
}

bool Fvisual::ShadowBatchMemberValid() const
{
    if (!immutable_level_geometry || !shadow_batch || !m_fast || !shader || shadow_batch_member >= shadow_batch->members.size()) return false;
    const auto& member = shadow_batch->members[shadow_batch_member];
    const auto* element = shader->E[SE_R2_SHADOW]._get();
    return shader == member.source_shader && element == member.source_element._get() && shadow_element_safe(element) &&
        element->passes[0] == shadow_batch->pass && m_fast->rm_geom == member.source_geometry &&
        m_fast->rm_geom->vb == shadow_batch->vertices && m_fast->rm_geom->ib == member.source_indices &&
        m_fast->rm_geom->dcl == shadow_batch->geometry->dcl && m_fast->rm_geom->vb_stride == sizeof(Fvector) &&
        m_fast->vBase == member.base_vertex && m_fast->vCount == member.vertex_count &&
        m_fast->iBase == member.first_index && m_fast->iCount == member.index_count && m_fast->dwPrimitives == member.index_count / 3;
}

void Fvisual::BuildShadowBatches(const xr_vector<dxRender_Visual*>& visuals)
{
    PROF_EVENT("Shadow.Batch.Build.Level");
    R_ASSERT(!RImplementation.b_loaded);
    struct Candidate { Fvisual* mesh; SPass* pass; int cell[3]; };
    xr_vector<Candidate> candidates;
    for (auto* visual : visuals)
    {
        if (visual->Type != MT_NORMAL) continue;
        auto* mesh = static_cast<Fvisual*>(visual); mesh->shadow_batch.reset();
        if (!mesh->immutable_level_geometry || !mesh->m_fast || !mesh->shader ||
            mesh->shadow_source_indices.empty() || mesh->shadow_source_indices.size() > aoe_render::shadow_batch_indices) continue;
        auto* element = mesh->shader->E[SE_R2_SHADOW]._get();
        if (!shadow_element_safe(element) || !element->passes[0] || !ShadowBatchPassSafe(*element->passes[0])) continue;
        const auto& fast = *mesh->m_fast;
        if (!fast.rm_geom || !fast.rm_geom->dcl || !fast.p_rm_Vertices || !fast.p_rm_Indices ||
            fast.rm_geom->vb != fast.p_rm_Vertices || fast.rm_geom->ib != fast.p_rm_Indices ||
            !fast.vCount || !fast.iCount || fast.iCount % 3 || fast.dwPrimitives != fast.iCount / 3 ||
            fast.iCount != mesh->shadow_source_indices.size()) continue;
        const auto& decl = fast.rm_geom->dcl->dcl_code;
        if (decl.size() != 2 || decl[0].Stream != 0 || decl[0].Offset != 0 || decl[0].Type != D3DDECLTYPE_FLOAT3 ||
            decl[0].Method != D3DDECLMETHOD_DEFAULT || decl[0].Usage != D3DDECLUSAGE_POSITION || decl[0].UsageIndex != 0 ||
            decl[1].Stream != 0xff || fast.rm_geom->vb_stride != sizeof(Fvector)) continue;
        D3D11_BUFFER_DESC buffer{}; fast.p_rm_Vertices->GetDesc(&buffer);
        const u32 vertices = buffer.ByteWidth / sizeof(Fvector);
        if (fast.vBase > vertices || fast.vCount > vertices - fast.vBase) continue;
        bool valid = true;
        for (u16 index : mesh->shadow_source_indices) if (index >= fast.vCount) { valid = false; break; }
        const auto& p = mesh->vis.sphere.P;
        if (!valid || !_valid(p) || _abs(p.x)>1000000.f || _abs(p.y)>1000000.f || _abs(p.z)>1000000.f) continue;
        candidates.push_back({mesh,element->passes[0]._get(),{iFloor(p.x/16.f),iFloor(p.y/16.f),iFloor(p.z/16.f)}});
    }
    auto same = [](const Candidate& a,const Candidate& b)
    {
        return a.pass == b.pass && a.mesh->m_fast->rm_geom->vb == b.mesh->m_fast->rm_geom->vb &&
            a.mesh->m_fast->rm_geom->dcl == b.mesh->m_fast->rm_geom->dcl &&
            a.cell[0]==b.cell[0] && a.cell[1]==b.cell[1] && a.cell[2]==b.cell[2];
    };
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b)
    {
        const void* ka[]={a.pass,a.mesh->m_fast->rm_geom->vb,a.mesh->m_fast->rm_geom->dcl._get()};
        const void* kb[]={b.pass,b.mesh->m_fast->rm_geom->vb,b.mesh->m_fast->rm_geom->dcl._get()};
        for(u32 i=0;i<3;++i) if(ka[i]!=kb[i]) return std::less<const void*>()(ka[i],kb[i]);
        for(u32 i=0;i<3;++i) if(a.cell[i]!=b.cell[i]) return a.cell[i]<b.cell[i];
        return a.mesh->m_fast->vBase < b.mesh->m_fast->vBase;
    });
    u32 groups=0,members=0,bytes=0;
    for(size_t begin=0;begin<candidates.size();)
    {
        const auto& first=candidates[begin];
        const u32 base=first.mesh->m_fast->vBase;
        u32 vertex_end=base,count=0; size_t end=begin;
        while(end<candidates.size() && end-begin<aoe_render::shadow_batch_members && same(first,candidates[end]))
        {
            const auto& fast=*candidates[end].mesh->m_fast;
            const u32 next_end=_max(vertex_end,fast.vBase+fast.vCount);
            if(!aoe_render::static_vertex_span(base,next_end) || fast.iCount>aoe_render::shadow_batch_indices-count) break;
            vertex_end=next_end;count+=fast.iCount;++end;
        }
        if(end-begin>1 && count*sizeof(u16)<=aoe_render::shadow_batch_bytes-bytes)
        {
            xr_vector<u16> indices;indices.reserve(count);
            bool valid=true;
            for(size_t i=begin;i<end && valid;++i)
            {
                const auto* mesh=candidates[i].mesh;
                for(u16 index:mesh->shadow_source_indices)
                {
                    u16 rebased;
                    if(!aoe_render::rebase_shadow_index(index,mesh->m_fast->vBase,mesh->m_fast->vCount,base,rebased)) {valid=false;break;}
                    indices.push_back(rebased);
                }
            }
            if(valid && indices.size()==count)
            {
                auto batch=std::make_shared<ShadowBatch>();
                D3D11_BUFFER_DESC desc{};desc.ByteWidth=count*sizeof(u16);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
                D3D11_SUBRESOURCE_DATA source{};source.pSysMem=indices.data();
                if(SUCCEEDED(HW.pDevice->CreateBuffer(&desc,&source,&batch->indices)))
                {
                    HW.stats_manager.increment_stats_ib(batch->indices);
                    batch->vertices=first.mesh->m_fast->p_rm_Vertices;batch->vertices->AddRef();
                    batch->geometry.create(first.mesh->m_fast->rm_geom->dcl->dcl_code.data(),batch->vertices,batch->indices);
                    batch->base_vertex=base;batch->vertex_count=vertex_end-base;batch->index_count=count;
                    batch->pass=first.mesh->shader->E[SE_R2_SHADOW]->passes[0];
                    batch->vertex_revision=batch->pass->vs->shadow_shader_revision;batch->pixel_revision=batch->pass->ps->shadow_shader_revision;
                    batch->members.reserve(end-begin);
                    for(size_t i=begin;i<end;++i)
                    {
                        auto* mesh=candidates[i].mesh;const auto& fast=*mesh->m_fast;
                        ShadowMember member;member.source_geometry=fast.rm_geom;member.source_shader=mesh->shader;
                        member.source_indices=fast.rm_geom->ib;
                        member.source_element=mesh->shader->E[SE_R2_SHADOW];
                        member.base_vertex=fast.vBase;member.vertex_count=fast.vCount;member.first_index=fast.iBase;member.index_count=fast.iCount;
                        batch->members.push_back(member);batch->original_vertices+=fast.vCount;
                    }
                    // Publish only after GPU storage and ALL member records are complete.
                    for(size_t i=begin;i<end;++i)
                    {
                        candidates[i].mesh->shadow_batch=batch;candidates[i].mesh->shadow_batch_member=u32(i-begin);
                    }
                    ++groups;members+=u32(end-begin);bytes+=desc.ByteWidth;
                }
                else { _RELEASE(batch->indices); }
            }
        }
        begin=end>begin?end:begin+1;
    }
    for(auto* visual:visuals) if(visual->Type==MT_NORMAL) static_cast<Fvisual*>(visual)->shadow_source_indices.clear_and_free();
    Msg("* Shadow batches: %u groups, %u meshes, %u KB indices (%u candidates)",groups,members,bytes/1024,u32(candidates.size()));
}

bool Fvisual::DrawShadowBatch(u32 ticket)
{
    if (RImplementation.phase != CRender::PHASE_SMAP || RImplementation.o.Tshadows ||
        !ticket || !shadow_batch || !ShadowBatchMemberValid()) return false;
    auto& batch = *shadow_batch;
    if (batch.selection.frame != Device.dwFrame || batch.selection.ticket != ticket ||
        !batch.binding_valid || !batch.selection.complete(u32(batch.members.size())) ||
        !RCache.matches_shadow_batch_pass(*batch.pass) || !StateManager.ShadowBatchStateSafe()) return false;
    if (batch.selection.emitted) return true;
    PROF_EVENT("Render.Shadow.StaticBatch");
    PROF_TAG("Members", u32(batch.members.size()));
    batch.selection.emitted = true;
    RCache.set_Geometry(batch.geometry);
    RCache.Render(D3DPT_TRIANGLELIST, batch.base_vertex, 0, batch.vertex_count, 0, batch.index_count / 3);
    RCache.stat.r.s_static.add(batch.original_vertices);
    return true;
}
#endif

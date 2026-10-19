#include "stdafx.h"
#pragma hdrstop

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/igame_level.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/fmesh.h"

#include "../../build_config_defines.h"

#include "ftreevisual.h"
#if defined(USE_DX11) && RENDER == R_R4
#include "TreeShadowPolicy.h"
#include "ShadowBatchPolicy.h"
#include "StaticDrawPacketPolicy.h"
#include <map>
extern bool NativeShadowHandlers(const R_constant_table*);
#endif

shared_str m_xform;
shared_str m_xform_v;
shared_str c_consts;
shared_str c_wave;
shared_str c_wind;
shared_str c_c_bias;
shared_str c_c_scale;
shared_str c_c_sun;

shared_str c_prev_wave;
shared_str c_prev_wind;

shared_str c_c_PrevBendersPos;
shared_str c_c_BendersPos;
shared_str c_c_BendersSetup;

#if defined(USE_DX11) && RENDER == R_R4
struct TreeShadowStorage
{
    ID3DIndexBuffer* buffer = nullptr;
    ~TreeShadowStorage()
    {
        if (buffer) HW.stats_manager.decrement_stats_ib(buffer);
        _RELEASE(buffer);
    }
};

struct TreeShadowMesh
{
    ref_geom original, geometry;
    ID3DVertexBuffer* vertices = nullptr;
    std::shared_ptr<TreeShadowStorage> storage;
    std::array<u32, 7> key{}; // VB, IB, base, vertices, first index, index count, input mask.
    u32 base = 0, count = 0, vertex_count = 0, first_index = 0;
    ~TreeShadowMesh()
    {
        geometry.destroy(); original.destroy();
        storage.reset(); _RELEASE(vertices);
    }
};

struct TreeShadowBatch
{
    struct Member { std::shared_ptr<TreeShadowMesh> mesh; ref_shader shader; };
    xr_vector<Member> members;
    std::shared_ptr<TreeShadowMesh> geometry;
    ref_pass pass;
    Fmatrix world;
    std::array<u8, 40> lighting{};
    u32 vertex_revision = 0, pixel_revision = 0, original_vertices = 0;
    aoe_render::ShadowBatchSelection selection;
    bool valid = false;
};

namespace
{
struct TreeShadowStats { u32 indexed = 0, groups = 0, saved = 0, partial = 0; } tree_shadow_stats;
constexpr u32 tree_shadow_budget = 64u * 1024u * 1024u;

SPass* tree_shadow_pass(const ref_shader& shader)
{
    if (!shader || !shader->E[SE_R2_SHADOW]) return nullptr;
    const auto& element = *shader->E[SE_R2_SHADOW];
    if (element.passes.size() != 1 || element.flags.bStrictB2F || element.flags.bDistort ||
        element.flags.bEmissive || element.flags.bWmark || element.flags.isWater || element.flags.iScopeLense) return nullptr;
    auto* pass = element.passes[0]._get();
    if (!pass || !pass->vs || !pass->ps || !pass->vs->tree_shadow_inputs || !pass->ps->static_draw_safe ||
        (pass->gs && pass->gs->gs) || (pass->hs && pass->hs->sh) || (pass->ds && pass->ds->sh) || (pass->cs && pass->cs->sh)) return nullptr;
    return pass;
}

bool tree_batch_pass(const SPass& pass)
{
    return pass.vs && pass.ps && pass.vs->tree_shadow_batch_safe && pass.ps->shadow_batch_safe && NativeShadowHandlers(pass.constants._get()) &&
        pass.state && pass.state->state && pass.state->state->ShadowBatchSafe();
}

bool stage_tree_indices(const std::shared_ptr<TreeShadowMesh>& mesh, const std::vector<u16>& indices,
    const ref_geom& source, std::vector<u16>& packed, std::vector<std::shared_ptr<TreeShadowMesh>>& uploads)
{
    if (indices.empty() || indices.size() > tree_shadow_budget / sizeof(u16) - packed.size()) return false;
    mesh->first_index = u32(packed.size()); mesh->original = source;
    packed.insert(packed.end(), indices.begin(), indices.end());
    uploads.push_back(mesh);
    return true;
}
}

void FTreeVisual::BuildShadowGeometry(const xr_vector<dxRender_Visual*>& visuals)
{
    PROF_EVENT("Trees.Shadow.Build.Level");
    // The serialized level loader may run on PPL before b_loaded is published.
    R_ASSERT(!RImplementation.b_loaded);
    struct Entry { std::shared_ptr<TreeShadowMesh> mesh; std::vector<u16> indices; };
    std::map<std::array<u32, 7>, Entry> cache;
    std::vector<u16> packed;
    std::vector<std::shared_ptr<TreeShadowMesh>> uploads;
    xr_vector<FTreeVisual*> candidates;
    u32 scratch_bytes = 0, optimized = 0, groups = 0;
    for (auto* visual : visuals)
    {
        if (visual->Type != MT_TREE_ST) continue;
        auto& tree = *static_cast<FTreeVisual*>(visual);
        tree.shadow_batch.reset(); tree.shadow_mesh.reset(); tree.shadow_ticket = 0;
        auto* pass = tree_shadow_pass(tree.shader);
        if (!pass || !tree.rm_geom || tree.rm_geom->vb_stride != 32 ||
            tree.load_vb >= RImplementation.levelTreeVBData.size() || tree.load_ib >= RImplementation.levelIBData.size() ||
            tree.rm_geom->vb != tree.p_rm_Vertices || tree.rm_geom->ib != tree.p_rm_Indices) continue;
        const auto& vertices = RImplementation.levelTreeVBData[tree.load_vb];
        const auto& source = RImplementation.levelIBData[tree.load_ib];
        if (!tree.vCount || tree.vCount > 65536 || tree.vBase > vertices.size() || tree.vCount > vertices.size() - tree.vBase ||
            tree.iBase > source.size() || tree.iCount > source.size() - tree.iBase || !tree.iCount ||
            tree.iCount > tree_shadow_budget / sizeof(u16) || tree.iCount % 3 || tree.dwPrimitives != tree.iCount / 3) continue;
        const std::array<u32, 7> key{tree.load_vb, tree.load_ib, tree.vBase, tree.vCount, tree.iBase, tree.iCount, pass->vs->tree_shadow_inputs};
        auto inserted = cache.emplace(key, Entry{});
        auto& entry = inserted.first->second;
        if (inserted.second)
        {
            if (tree.iCount > (tree_shadow_budget - scratch_bytes) / sizeof(u16)) continue;
            scratch_bytes += tree.iCount * sizeof(u16);
            u32 unique;
            if (!aoe_render::tree_shadow_indices(vertices.data() + tree.vBase, tree.vCount,
                source.data() + tree.iBase, tree.iCount, (key[6] & 2) != 0, entry.indices, unique)) continue;
            auto mesh = std::make_shared<TreeShadowMesh>();
            mesh->key = key; mesh->original = tree.rm_geom; mesh->base = tree.vBase;
            mesh->count = tree.iCount; mesh->vertex_count = tree.vCount;
            if (memcmp(entry.indices.data(), source.data() + tree.iBase, tree.iCount * sizeof(u16)) &&
                stage_tree_indices(mesh, entry.indices, tree.rm_geom, packed, uploads)) ++optimized;
            entry.mesh = std::move(mesh);
        }
        if (!entry.mesh) continue;
        tree.shadow_mesh = entry.mesh;
        if (tree_batch_pass(*pass)) candidates.push_back(&tree);
    }
    // Parts of one exact instance only: no world-space baking or cross-material merge.
    auto same = [](const FTreeVisual* a, const FTreeVisual* b)
    {
        return tree_shadow_pass(a->shader) == tree_shadow_pass(b->shader) && a->p_rm_Vertices == b->p_rm_Vertices &&
            a->rm_geom->dcl == b->rm_geom->dcl && !memcmp(&a->xform, &b->xform, sizeof(Fmatrix)) &&
            !memcmp(&a->c_scale, &b->c_scale, sizeof(_5color)) && !memcmp(&a->c_bias, &b->c_bias, sizeof(_5color));
    };
    std::sort(candidates.begin(), candidates.end(), [](const FTreeVisual* a, const FTreeVisual* b)
    {
        const void* ka[] = {tree_shadow_pass(a->shader), a->p_rm_Vertices, a->rm_geom->dcl._get()};
        const void* kb[] = {tree_shadow_pass(b->shader), b->p_rm_Vertices, b->rm_geom->dcl._get()};
        for (u32 i = 0; i < 3; ++i) if (ka[i] != kb[i]) return std::less<const void*>()(ka[i], kb[i]);
        int c = memcmp(&a->xform, &b->xform, sizeof(Fmatrix)); if (c) return c < 0;
        c = memcmp(&a->c_scale, &b->c_scale, sizeof(_5color)); if (c) return c < 0;
        c = memcmp(&a->c_bias, &b->c_bias, sizeof(_5color)); if (c) return c < 0;
        return a->vBase < b->vBase;
    });
    std::map<std::vector<std::array<u32, 7>>, std::shared_ptr<TreeShadowMesh>> combined;
    for (size_t begin = 0; begin < candidates.size();)
    {
        auto* first = candidates[begin]; size_t end = begin;
        u32 vertex_end = first->vBase, count = 0;
        while (end < candidates.size() && end - begin < aoe_render::shadow_batch_members && same(first, candidates[end]))
        {
            auto* tree = candidates[end]; const u32 next = _max(vertex_end, tree->vBase + tree->vCount);
            if (!aoe_render::static_vertex_span(first->vBase, next) || tree->iCount > aoe_render::shadow_batch_indices - count) break;
            vertex_end = next; count += tree->iCount; ++end;
        }
        if (end - begin > 1)
        {
            std::vector<std::array<u32, 7>> key;
            for (size_t i = begin; i < end; ++i) key.push_back(candidates[i]->shadow_mesh->key);
            auto found = combined.emplace(key, nullptr);
            if (found.second)
            {
                std::vector<u16> indices; indices.reserve(count); bool valid = true;
                for (size_t i = begin; i < end && valid; ++i)
                {
                    auto* tree = candidates[i];
                    for (u16 index : cache.at(tree->shadow_mesh->key).indices)
                    {
                        u16 rebased;
                        if (!aoe_render::rebase_shadow_index(index, tree->vBase, tree->vCount, first->vBase, rebased)) { valid = false; break; }
                        indices.push_back(rebased);
                    }
                }
                auto mesh = std::make_shared<TreeShadowMesh>();
                mesh->base = first->vBase; mesh->vertex_count = vertex_end - first->vBase; mesh->count = count;
                if (valid && indices.size() == count && stage_tree_indices(mesh, indices, first->rm_geom, packed, uploads)) found.first->second = mesh;
            }
            if (found.first->second)
            {
                auto batch = std::make_shared<TreeShadowBatch>(); batch->geometry = found.first->second;
                batch->pass = first->shader->E[SE_R2_SHADOW]->passes[0]; batch->world = first->xform;
                static_assert(sizeof(_5color) == 20, "Tree lighting snapshot layout");
                CopyMemory(batch->lighting.data(), &first->c_scale, 20); CopyMemory(batch->lighting.data() + 20, &first->c_bias, 20);
                batch->vertex_revision = batch->pass->vs->shadow_shader_revision; batch->pixel_revision = batch->pass->ps->shadow_shader_revision;
                for (size_t i = begin; i < end; ++i)
                {
                    auto* tree = candidates[i]; batch->members.push_back({tree->shadow_mesh, tree->shader});
                    batch->original_vertices += tree->vCount;
                }
                for (size_t i = begin; i < end; ++i) { candidates[i]->shadow_batch = batch; candidates[i]->shadow_member = u32(i - begin); }
                ++groups;
            }
        }
        begin = end > begin ? end : begin + 1;
    }
    // One immutable IB for the entire level: no per-mesh IB binding churn. Each
    // record owns its exact start index; this never touches the shared index stream.
    if (!packed.empty())
    {
        auto storage = std::make_shared<TreeShadowStorage>();
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth = u32(packed.size() * sizeof(u16));
        desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = packed.data();
        if (FAILED(HW.pDevice->CreateBuffer(&desc, &data, &storage->buffer)))
        {
            _RELEASE(storage->buffer);
            for (auto* visual : visuals) if (visual->Type == MT_TREE_ST)
            {
                auto* tree = static_cast<FTreeVisual*>(visual); tree->shadow_batch.reset(); tree->shadow_mesh.reset();
            }
            Msg("! Tree shadow index allocation failed; retaining original geometry");
            return;
        }
        HW.stats_manager.increment_stats_ib(storage->buffer);
        for (auto& mesh : uploads)
        {
            mesh->vertices = mesh->original->vb; mesh->vertices->AddRef();
            mesh->geometry.create(mesh->original->dcl->dcl_code.data(), mesh->vertices, storage->buffer);
            mesh->storage = storage;
        }
    }
    Msg("* Tree shadows: %u optimized meshes, %u instance groups, %u shared group ranges, %u KB indices",
        optimized, groups, u32(std::count_if(combined.begin(), combined.end(), [](const auto& e) { return e.second != nullptr; })), u32(packed.size() * sizeof(u16) / 1024));
}

bool FTreeVisual::ShadowGeometryValid(bool bound) const
{
    if (!shadow_mesh || Type != MT_TREE_ST || shadow_mesh->original != rm_geom ||
        p_rm_Vertices != rm_geom->vb || p_rm_Indices != rm_geom->ib) return false;
    const auto& key = shadow_mesh->key;
    auto* pass = tree_shadow_pass(shader);
    return vBase == key[2] && vCount == key[3] && iBase == key[4] && iCount == key[5] && dwPrimitives == iCount / 3 &&
        pass && (pass->vs->tree_shadow_inputs & key[6]) == pass->vs->tree_shadow_inputs &&
        (!bound || RCache.matches_shadow_batch_pass(*pass));
}

void FTreeVisual::SelectShadowBatch(u32 ticket)
{
    shadow_ticket = 0;
    if (!shadow_batch || shadow_member >= shadow_batch->members.size()) return;
    auto& batch = *shadow_batch;
    if (batch.selection.begin(Device.dwFrame, ticket))
        batch.valid = tree_batch_pass(*batch.pass) && batch.vertex_revision == batch.pass->vs->shadow_shader_revision &&
            batch.pixel_revision == batch.pass->ps->shadow_shader_revision;
    const auto& member = batch.members[shadow_member];
    if (!batch.valid || member.mesh != shadow_mesh || member.shader != shader || !ShadowGeometryValid(false) ||
        tree_shadow_pass(shader) != batch.pass._get() || memcmp(&xform, &batch.world, sizeof(Fmatrix)) ||
        memcmp(&c_scale, batch.lighting.data(), 20) || memcmp(&c_bias, batch.lighting.data() + 20, 20)) return;
    batch.selection.add(shadow_member, u32(batch.members.size()));
    shadow_ticket = ticket; shadow_frame = Device.dwFrame;
}

bool FTreeVisual::DrawShadowBatch(float lod, u32 ticket)
{
    if (RImplementation.phase != CRender::PHASE_SMAP || !shadow_batch ||
        RImplementation.o.Tshadows || !ticket || shadow_ticket != ticket || shadow_frame != Device.dwFrame) return false;
    auto& batch = *shadow_batch;
    if (batch.selection.frame != Device.dwFrame || batch.selection.ticket != shadow_ticket || !batch.valid ||
        !batch.geometry || !batch.geometry->storage || !ShadowGeometryValid() ||
        !RCache.matches_shadow_batch_pass(*batch.pass) || !StateManager.ShadowBatchStateSafe()) return false;
    if (!batch.selection.complete(u32(batch.members.size()))) { ++tree_shadow_stats.partial; return false; }
    if (batch.selection.emitted) return true;
    batch.selection.emitted = true;
    PROF_EVENT("Render.Shadow.TreeGroup");
    FTreeVisual::Render(lod);
    auto& mesh = *batch.geometry;
    RCache.set_Geometry(mesh.geometry);
    RCache.Render(D3DPT_TRIANGLELIST, mesh.base, 0, mesh.vertex_count, mesh.first_index, mesh.count / 3);
    RCache.stat.r.s_flora.add(batch.original_vertices);
    ++tree_shadow_stats.groups; tree_shadow_stats.saved += u32(batch.members.size()) - 1;
    return true;
}

bool FTreeVisual::DrawShadowGeometry()
{
    if (RImplementation.phase != CRender::PHASE_SMAP || RCache.is_TessEnabled() ||
        !shadow_mesh || !shadow_mesh->storage || !ShadowGeometryValid()) return false;
    PROF_EVENT("Render.Shadow.TreeSharedIndices");
    RCache.set_Geometry(shadow_mesh->geometry);
    RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, shadow_mesh->first_index, dwPrimitives);
    RCache.stat.r.s_flora.add(vCount);
    ++tree_shadow_stats.indexed;
    return true;
}

void FTreeVisual::ResetShadowStats() { tree_shadow_stats = {}; }
void FTreeVisual::ProfileShadowStats()
{
    PROF_TAG("Trees.Shadow.IndexedDraws", tree_shadow_stats.indexed);
    PROF_TAG("Trees.Shadow.Groups", tree_shadow_stats.groups);
    PROF_TAG("Trees.Shadow.SavedDraws", tree_shadow_stats.saved);
    PROF_TAG("Trees.Shadow.PartialDraws", tree_shadow_stats.partial);
}
#endif

FTreeVisual::FTreeVisual(void)
{
}

FTreeVisual::~FTreeVisual(void)
{
}

void FTreeVisual::Release()
{
#if RENDER != R_R1
    prepared_valid = false;
#if defined(USE_DX11) && RENDER == R_R4
    shadow_batch.reset();
    shadow_mesh.reset();
    shadow_ticket = 0;
#endif
#endif
	dxRender_Visual::Release();
}

void FTreeVisual::Load(const char* N, IReader* data, u32 dwFlags)
{
#if RENDER != R_R1
    prepared_valid = false;
#if defined(USE_DX11) && RENDER == R_R4
    shadow_batch.reset();
    shadow_mesh.reset();
    shadow_ticket = 0;
#endif
#endif
	dxRender_Visual::Load(N, data, dwFlags);

	D3DVERTEXELEMENT9* vFormat = NULL;

	// read vertices
	R_ASSERT(data->find_chunk(OGF_GCONTAINER));
	{
		// verts
		u32 ID = data->r_u32();
#if defined(USE_DX11) && RENDER == R_R4
        load_vb = ID;
#endif
		vBase = data->r_u32();
		vCount = data->r_u32();
		vFormat = RImplementation.getVB_Format(ID);

		VERIFY(NULL==p_rm_Vertices);

		p_rm_Vertices = RImplementation.getVB(ID);
		p_rm_Vertices->AddRef();

		// indices
		dwPrimitives = 0;
		ID = data->r_u32();
#if defined(USE_DX11) && RENDER == R_R4
        load_ib = ID;
#endif
		iBase = data->r_u32();
		iCount = data->r_u32();
		dwPrimitives = iCount / 3;

		VERIFY(NULL==p_rm_Indices);
		p_rm_Indices = RImplementation.getIB(ID);
		p_rm_Indices->AddRef();
	}

	// load tree-def
	R_ASSERT(data->find_chunk(OGF_TREEDEF2));
	{
		data->r(&xform, sizeof(xform));
		data->r(&c_scale, sizeof(c_scale));
		c_scale.rgb.mul(.5f);
		c_scale.hemi *= .5f;
		c_scale.sun *= .5f;
		data->r(&c_bias, sizeof(c_bias));
		c_bias.rgb.mul(.5f);
		c_bias.hemi *= .5f;
		c_bias.sun *= .5f;
		//Msg				("hemi[%f / %f], sun[%f / %f]",c_scale.hemi,c_bias.hemi,c_scale.sun,c_bias.sun);
	}

	// Geom
	rm_geom.create(vFormat, p_rm_Vertices, p_rm_Indices);

	// Get constants
	m_xform = "m_xform";
	m_xform_v = "m_xform_v";
	c_consts = "consts";
	c_wave = "wave";
	c_wind = "wind";
	c_c_bias = "c_bias";
	c_c_scale = "c_scale";
	c_c_sun = "c_sun";

	c_prev_wave = "prev_wave";
	c_prev_wind = "prev_wind";

	c_c_PrevBendersPos = "benders_prevpos";
	c_c_BendersPos = "benders_pos";
	c_c_BendersSetup = "benders_setup";
}

struct FTreeVisual_setup
{
	u32 dwFrame;
	float scale;
	Fvector4 wave;
	Fvector4 wind;

	FTreeVisual_setup()
	{
		dwFrame = 0;
	}

	void calculate()
	{
		dwFrame = Device.dwFrame;

		// Calc wind-vector3, scale
		float tm_rot = PI_MUL_2 * Device.fTimeGlobal / ps_r__Tree_w_rot;

#ifdef TREE_WIND_EFFECT
		CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
		float fValue = E.m_fTreeAmplitudeIntensity;
		wind.set(_sin(tm_rot), 0, _cos(tm_rot), 0);
		wind.normalize();
#if RENDER!=R_R1
		wind.mul(fValue); // dir1*amplitude
#else // R1
		wind.mul(ps_r__Tree_w_amp); // dir1*amplitude
#endif //-RENDER!=R_R1
#else //!TREE_WIND_EFFECT
		wind.set				(_sin(tm_rot),0,_cos(tm_rot),0);	wind.normalize	();	wind.mul(ps_r__Tree_w_amp);	// dir1*amplitude
#endif //-TREE_WIND_EFFECT
		scale = 1.f / float(FTreeVisual_quant);

		// setup constants
		wave.set(ps_r__Tree_Wave.x, ps_r__Tree_Wave.y, ps_r__Tree_Wave.z,
		         Device.fTimeGlobal * ps_r__Tree_w_speed); // wave
		wave.div(PI_MUL_2);
	}
};

void FTreeVisual::Render(float LOD)
{
	PROF_EVENT("FTreeVisual::Render");
	static FTreeVisual_setup tvs, prev_tvs;
	if (tvs.dwFrame != Device.dwFrame)
	{
		prev_tvs = tvs; // Save previous frame calculations
		tvs.calculate();
	}
	// setup constants
#if RENDER!=R_R1
	const Fmatrix& view = RCache.get_xform_view();
    const bool matched = prepared_valid && !memcmp(&prepared_view, &view, sizeof(Fmatrix)) &&
        !memcmp(&prepared_world, &xform, sizeof(Fmatrix));
    if (!matched)
    {
        PROF_EVENT("Render.Trees.PrepareViewMatrix");
        Fmatrix combined;
        combined.mul_43(view, xform);
        PublishPreparedView(view, xform, combined);
    }
    RCache.tree.set_m_xform_v(prepared_combined);
#endif
	float s = ps_r__Tree_SBC;
	RCache.tree.set_m_xform(xform); // matrix
	RCache.tree.set_consts(tvs.scale, tvs.scale, 0, 0); // consts/scale
	RCache.tree.set_wave(tvs.wave); // wave
	RCache.tree.set_wind(tvs.wind); // wind

	RCache.set_c(c_prev_wave, prev_tvs.wave);
	RCache.set_c(c_prev_wind, prev_tvs.wind);

#if RENDER!=R_R1
	s *= 1.3333f;
	RCache.tree.set_c_scale(s * c_scale.rgb.x, s * c_scale.rgb.y, s * c_scale.rgb.z, s * c_scale.hemi); // scale
	RCache.tree.set_c_bias(s * c_bias.rgb.x, s * c_bias.rgb.y, s * c_bias.rgb.z, s * c_bias.hemi); // bias
#else
	CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
	RCache.tree.set_c_scale(s * c_scale.rgb.x, s * c_scale.rgb.y, s * c_scale.rgb.z, s * c_scale.hemi); // scale
	RCache.tree.set_c_bias(s * c_bias.rgb.x + desc.ambient.x, s * c_bias.rgb.y + desc.ambient.y,
	                       s * c_bias.rgb.z + desc.ambient.z, s * c_bias.hemi); // bias
#endif
	RCache.tree.set_c_sun(s * c_scale.sun, s * c_bias.sun, 0, 0); // sun

#if RENDER==R_R4 || RENDER==R_R3

	if (ps_ssfx_grass_interactive.y > 0)
	{
		// Inter grass Settings
		RCache.set_c(c_c_BendersSetup, ps_ssfx_int_grass_params_1);

		// Grass benders data ( Player + Characters )
		IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
		Fvector4 player_pos = { 0, 0, 0, 0 };
		int BendersQty = _min(16, ps_ssfx_grass_interactive.y + 1);

		// Add Player?
		if (ps_ssfx_grass_interactive.x > 0)
		{
			player_pos.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);
		}

		Fvector4* c_grass;
		{
			void* GrassData;
			RCache.get_ConstantDirect(c_c_BendersPos, BendersQty * sizeof(Fvector4) * 2, &GrassData, 0, 0);

			c_grass = (Fvector4*)GrassData;
		}
		VERIFY(c_grass);

		if (c_grass)
		{
			c_grass[0].set(player_pos);
			c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

			for (int Bend = 1; Bend < BendersQty; Bend++)
			{
				c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y, GData.pos[Bend].z, GData.radius_curr[Bend]);
				c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y, GData.dir[Bend].z, GData.str[Bend]);
			}
		}

		Fvector4* c_prevgrass;
		{
			void* prevGrassData;
			RCache.get_ConstantDirect(c_c_PrevBendersPos, BendersQty * sizeof(Fvector4) * 2, &prevGrassData, 0, 0);

			c_prevgrass = (Fvector4*)prevGrassData;
		}
		VERIFY(c_prevgrass);

		if (c_prevgrass)
		{
			for (int Bend = 0; Bend < BendersQty; Bend++)
			{
				c_prevgrass[Bend].set(GData.prev_pos[Bend]);
				c_prevgrass[Bend + 16].set(GData.prev_dir[Bend]);
			}
		}
	}
#endif
}

#define PCOPY(a)	a = pFrom->a

void FTreeVisual::Copy(dxRender_Visual* pSrc)
{
#if RENDER != R_R1
    prepared_valid = false;
#if defined(USE_DX11) && RENDER == R_R4
    shadow_batch.reset();
    shadow_mesh.reset();
    shadow_ticket = 0;
#endif
#endif
	dxRender_Visual::Copy(pSrc);

	FTreeVisual* pFrom = fast_dynamic_cast<FTreeVisual*>(pSrc);

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

	PCOPY(xform);
	PCOPY(c_scale);
	PCOPY(c_bias);
}

//-----------------------------------------------------------------------------------
// Stripified Tree
//-----------------------------------------------------------------------------------
FTreeVisual_ST::FTreeVisual_ST(void)
{
}

FTreeVisual_ST::~FTreeVisual_ST(void)
{
}

void FTreeVisual_ST::Release()
{
	inherited::Release();
}

void FTreeVisual_ST::Load(const char* N, IReader* data, u32 dwFlags)
{
	inherited::Load(N, data, dwFlags);
}

void FTreeVisual_ST::Render(float LOD)
{
	inherited::Render(LOD);
#if defined(USE_DX11) && RENDER == R_R4
    if (DrawShadowGeometry()) return;
#endif
	RCache.set_Geometry(rm_geom);
	RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, iBase, dwPrimitives);
	RCache.stat.r.s_flora.add(vCount);
}

void FTreeVisual_ST::Copy(dxRender_Visual* pSrc)
{
	inherited::Copy(pSrc);
}

//-----------------------------------------------------------------------------------
// Progressive Tree
//-----------------------------------------------------------------------------------
FTreeVisual_PM::FTreeVisual_PM(void)
{
	pSWI = 0;
	last_lod = 0;
}

FTreeVisual_PM::~FTreeVisual_PM(void)
{
}

void FTreeVisual_PM::Release()
{
	inherited::Release();
}

void FTreeVisual_PM::Load(const char* N, IReader* data, u32 dwFlags)
{
	inherited::Load(N, data, dwFlags);
	R_ASSERT(data->find_chunk(OGF_SWICONTAINER));
	{
		u32 ID = data->r_u32();
		pSWI = RImplementation.getSWI(ID);
	}
}

void FTreeVisual_PM::Render(float LOD)
{
	inherited::Render(LOD);
	int lod_id = last_lod;
	if (LOD >= 0.f)
	{
		lod_id = iFloor((1.f - LOD) * float(pSWI->count - 1) + 0.5f);
		last_lod = lod_id;
	}
	VERIFY(lod_id>=0 && lod_id<int(pSWI->count));
	FSlideWindow& SW = pSWI->sw[lod_id];
	RCache.set_Geometry(rm_geom);
	RCache.Render(D3DPT_TRIANGLELIST, vBase, 0, SW.num_verts, iBase + SW.offset, SW.num_tris);
	RCache.stat.r.s_flora.add(SW.num_verts);
}

void FTreeVisual_PM::Copy(dxRender_Visual* pSrc)
{
	inherited::Copy(pSrc);
	FTreeVisual_PM* pFrom = fast_dynamic_cast<FTreeVisual_PM*>(pSrc);
	PCOPY(pSWI);
}

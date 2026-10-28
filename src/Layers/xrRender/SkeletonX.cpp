// SkeletonX.cpp: implementation of the CSkeletonX class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#pragma warning(disable:4995)
#include <d3dx9.h>
#pragma warning(default:4995)

#ifndef _EDITOR
#include	"../../xrEngine/Render.h"
#else
	#include "../../Include/xrAPI/xrAPI.h"
#endif

#include "SkeletonX.h"
#include "SkeletonCustom.h"
#include "../../xrServerEntities/smart_cast.h"
#include "../../xrEngine/fmesh.h"
#include "../../xrCPU_Pipe/xrCPU_Pipe.h"

shared_str s_bones_array_const;
shared_str s_bones_array_prev_const;

#if defined(USE_DX11) && RENDER == R_R4
std::shared_ptr<const SkeletonRenderSnapshot> CSkeletonX::SnapshotShadow(const Fmatrix& world)
{
    xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
    // Shadow maps take no motion vectors: the snapshot only reads last frame's main-view history.
    // The G-buffer pass owns advancing it (per viewport, keyed on Device.dwViewport, _Render), so
    // a shadow recorded before the G-buffer must not step it (that zeroed skinned motion vectors).
    (void)world;
    Fmatrix single = Fidentity, previous = Fidentity;
    if (RImplementation.o.ssfx_motionvectors) previous = Parent->Matrix_Prev[0];
    if (RenderMode == RM_SINGLE)
    {
        single = Parent->LL_GetTransform_R(u16(RMS_boneid));
        if (RImplementation.o.ssfx_motionvectors)
            previous.mul_43(Parent->Matrix_Prev[0], Parent->LL_GetBoneInstance(u16(RMS_boneid)).mRenderTransform_prev[0]);
    }
    const u32 count = RenderMode == RM_SINGLE ? 0 : RenderMode == RM_SKINNING_SOFT ? Parent->LL_BoneCount() : RMS_bonecount;
    const Fvector4* rows = count ? Parent->RenderPalette(count, false) : nullptr;
    if (count && !rows) return nullptr;
    if (shadow_snapshot && shadow_snapshot->frame == Device.dwFrame && shadow_snapshot->mode == RenderMode &&
        !memcmp(&shadow_snapshot->single, &single, sizeof(single)) &&
        !memcmp(&shadow_snapshot->previous_world, &previous, sizeof(previous)) &&
        shadow_snapshot->rows.size() == count * 3 &&
        (!count || !memcmp(shadow_snapshot->rows.data(), rows, count * 3 * sizeof(Fvector4))))
        return shadow_snapshot;
    auto result = std::make_shared<SkeletonRenderSnapshot>();
    result->frame = Device.dwFrame; result->mode = RenderMode;
    result->single = single; result->previous_world = previous;
    if (count) result->rows.assign(rows, rows + count * 3);
    if (RenderMode == RM_SKINNING_SOFT)
    {
        result->soft_transforms.reserve(count);
        for (u32 i = 0; i < count; ++i) result->soft_transforms.push_back(Parent->LL_GetTransform_R(u16(i)));
    }
    shadow_snapshot = result;
    return result;
}

void CSkeletonX::RenderSnapshot(const SkeletonRenderSnapshot& pose, ref_geom& geometry,
    u32 vertices, u32 offset, u32 primitives)
{
    PROF_EVENT("Render.Skinning.FrozenShadow");
    if (RImplementation.o.ssfx_motionvectors)
    {
        Fmatrix view, projection;
        view.mul_43(RCache.xforms.m_v_prev[0], pose.previous_world);
        projection.mul(RCache.xforms.m_p_prev[0], view);
        RCache.set_c("m_wvp_prev", projection);
    }
    RCache.stat.r.s_dynamic.add(vertices);
    if (pose.mode == RM_SINGLE)
    {
        Fmatrix world;
        world.mul_43(RCache.xforms.m_w, pose.single);
        RCache.set_xform_world(world);
        RCache.set_Geometry(geometry);
        RCache.Render(D3DPT_TRIANGLELIST, 0, 0, vertices, offset, primitives);
        RCache.stat.r.s_dynamic_inst.add(vertices);
        return;
    }
    if (pose.mode == RM_SKINNING_SOFT)
    {
        auto& stream = RCache.Vertex;
        auto& cache = context_soft_cache[RenderContextId()];
        if (cache.generation != RenderContextGeneration())
        {
            cache = {}; cache.generation = RenderContextGeneration();
        }
        if (cache.discard != stream.DiscardID() || cache.count != vertices || cache.pose != &pose)
        {
            auto* destination = static_cast<vertRender*>(stream.Lock(vertices, geometry->vb_stride, cache.offset));
            xr_vector<CBoneInstance> bones(pose.soft_transforms.size());
            for (size_t i = 0; i < bones.size(); ++i) bones[i].mRenderTransform = pose.soft_transforms[i];
            if (*Vertices1W) PSGP.skin1W(destination, *Vertices1W, vertices, bones.data());
            else if (*Vertices2W) PSGP.skin2W(destination, *Vertices2W, vertices, bones.data());
            else if (*Vertices3W) PSGP.skin3W(destination, *Vertices3W, vertices, bones.data());
            else if (*Vertices4W) PSGP.skin4W(destination, *Vertices4W, vertices, bones.data());
            else R_ASSERT2(false, "Unsupported software skinning format");
            stream.Unlock(vertices, geometry->vb_stride);
            cache.discard = stream.DiscardID(); cache.count = vertices; cache.pose = &pose;
        }
        RCache.set_Geometry(geometry);
        RCache.set_Vertices(stream.Buffer(), geometry->vb_stride);
        RCache.Render(D3DPT_TRIANGLELIST, cache.offset, 0, vertices, offset, primitives);
        RCache.stat.r.s_dynamic_sw.add(vertices);
        return;
    }
    auto* array = RCache.get_c(s_bones_array_const);
    if (!RCache.set_vertex_array_block(array, pose.rows.data(), u32(pose.rows.size())))
        for (u32 row = 0; row < pose.rows.size(); ++row) RCache.set_ca(array, row, pose.rows[row]);
    RCache.set_Geometry(geometry);
    RCache.Render(D3DPT_TRIANGLELIST, 0, 0, vertices, offset, primitives);
    if (pose.mode == RM_SKINNING_1B) RCache.stat.r.s_dynamic_1B.add(vertices);
    else if (pose.mode == RM_SKINNING_2B) RCache.stat.r.s_dynamic_2B.add(vertices);
    else if (pose.mode == RM_SKINNING_3B) RCache.stat.r.s_dynamic_3B.add(vertices);
    else if (pose.mode == RM_SKINNING_4B) RCache.stat.r.s_dynamic_4B.add(vertices);
}
#endif

#if RENDER == R_R4
struct SSvpHudBoneSnapshot
{
	CKinematics* parent = nullptr;
	xr_vector<CBoneInstance> bones;
	u64 visible = 0;
};

static xr_vector<SSvpHudBoneSnapshot> s_svp_hud_bones;
static u32 s_svp_hud_bone_frame = u32(-1);
bool g_svp_hud_frozen_pass = false;
bool g_svp_hud_history_write = false;

static SSvpHudBoneSnapshot* svp_hud_bones_of(CKinematics* parent)
{
	for (auto& snapshot : s_svp_hud_bones)
		if (snapshot.parent == parent)
			return &snapshot;
	return nullptr;
}
#endif

void svp_hud_bone_snapshot_begin()
{
#if RENDER == R_R4
	s_svp_hud_bones.clear();
	s_svp_hud_bone_frame = Device.dwFrame;
#endif
}

bool CSkeletonX::SVP_CaptureBoneSnapshot()
{
#if RENDER == R_R4
	if (!Parent)
		return false;
	xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
	if (s_svp_hud_bone_frame != Device.dwFrame)
		svp_hud_bone_snapshot_begin();
	SSvpHudBoneSnapshot* snapshot = svp_hud_bones_of(Parent);
	if (!snapshot)
	{
		s_svp_hud_bones.emplace_back();
		snapshot = &s_svp_hud_bones.back();
		snapshot->parent = Parent;
		snapshot->bones.assign(Parent->bone_instances,
			Parent->bone_instances + Parent->LL_BoneCount());
		snapshot->visible = Parent->LL_GetBonesVisible();
	}
	return snapshot->bones.size() == Parent->LL_BoneCount();
#else
	return false;
#endif
}

bool CSkeletonX::SVP_BoneSnapshotReady() const
{
#if RENDER == R_R4
	SSvpHudBoneSnapshot* snapshot = svp_hud_bones_of(Parent);
	return Parent && s_svp_hud_bone_frame == Device.dwFrame && snapshot
		&& snapshot->bones.size() == Parent->LL_BoneCount();
#else
	return false;
#endif
}

bool CSkeletonX::SVP_RigidBoneSnapshotXform(Fmatrix& out)
{
#if RENDER == R_R4
	if (RenderMode != RM_SINGLE)
		return false;
	return SVP_BoneSnapshotXform((u16)RMS_boneid, out);
#else
	return false;
#endif
}

bool CSkeletonX::SVP_BoneSnapshotXform(Fmatrix& out)
{
#if RENDER == R_R4
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return false;
	return SVP_BoneSnapshotXform(bone, out);
#else
	return false;
#endif
}

bool CSkeletonX::SVP_BoneSnapshotXform(u16 bone, Fmatrix& out)
{
#if RENDER == R_R4
	SSvpHudBoneSnapshot* snapshot = svp_hud_bones_of(Parent);
	if (!Parent || s_svp_hud_bone_frame != Device.dwFrame || !snapshot
		|| bone >= snapshot->bones.size())
		return false;
	out = snapshot->bones[bone].mRenderTransform;
	return true;
#else
	return false;
#endif
}

bool CSkeletonX::SVP_BoneSnapshotXform(const IKinematics* owner, u16 bone, Fmatrix& out)
{
#if RENDER == R_R4
	if (static_cast<const IKinematics*>(Parent) != owner)
		return false;
	return SVP_BoneSnapshotXform(bone, out);
#else
	return false;
#endif
}

bool CSkeletonX::SVP_BoneSnapshotVisible(BOOL& visible)
{
#if RENDER == R_R4
	SSvpHudBoneSnapshot* snapshot = svp_hud_bones_of(Parent);
	if (!Parent || s_svp_hud_bone_frame != Device.dwFrame || !snapshot)
		return false;
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return false;
	visible = (snapshot->visible & (u64(1) << bone)) != 0;
	return true;
#else
	return false;
#endif
}

//////////////////////////////////////////////////////////////////////
// Body Part
//////////////////////////////////////////////////////////////////////
void CSkeletonX::AfterLoad(CKinematics* parent, u16 child_idx)
{
	SetParent(parent);
	ChildIDX = child_idx;
}

void CSkeletonX::_Copy(CSkeletonX* B)
{
#if defined(USE_DX11) && RENDER == R_R4
    shadow_snapshot.reset();
#endif
	Parent = NULL;
	ChildIDX = B->ChildIDX;
	scope_surface_cached = B->scope_surface_cached;
	scope_surface_frames = B->scope_surface_frames;
	Vertices1W = B->Vertices1W;
	Vertices2W = B->Vertices2W;
	Vertices3W = B->Vertices3W;
	Vertices4W = B->Vertices4W;
	BonesUsed = B->BonesUsed;

	// caution - overlapped (union)
	cache_DiscardID = B->cache_DiscardID;
	cache_vCount = B->cache_vCount;
	cache_vOffset = B->cache_vOffset;
	RenderMode = B->RenderMode;
	RMS_boneid = B->RMS_boneid;
	RMS_bonecount = B->RMS_bonecount;

#if defined(USE_DX10) || defined(USE_DX11)
	m_Indices = B->m_Indices;
#endif	//	USE_DX10
}

//////////////////////////////////////////////////////////////////////
void CSkeletonX::_Render(ref_geom& hGeom, u32 vCount, u32 iOffset, u32 pCount)
{
    // Preserve upstream hiding of opaque parts sharing a kept glass bone.
    // The bone itself stays animated, so the glass does not collapse or freeze.
    if (Parent && Parent->PhysicalScopeGlassMask
        && (ScopeGlassBoneMask() & Parent->PhysicalScopeGlassMask) && !IsPhysicalScopeGlass()) return;
#if defined(USE_DX11) && RENDER == R_R4
    if (RenderContextId() && RCache.skeleton_snapshot)
    {
        RenderSnapshot(*RCache.skeleton_snapshot, hGeom, vCount, iOffset, pCount);
        return;
    }
#endif
	xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
	PROF_EVENT("CSkeletonX::_Render");
	Fmatrix p_WV, p_WVP;
	CBoneInstance* render_bones = Parent->bone_instances;

#if RENDER == R_R4
	const bool frozen_pose = g_svp_hud_frozen_pass;
	const bool suppress_history = frozen_pose && !g_svp_hud_history_write;
	if (frozen_pose && s_svp_hud_bone_frame == Device.dwFrame)
	{
		SSvpHudBoneSnapshot* snapshot = svp_hud_bones_of(Parent);
		if (snapshot && snapshot->bones.size() == Parent->LL_BoneCount())
			render_bones = snapshot->bones.data();
	}
	BindScopeSurfaceFrames(render_bones);
#else
	const bool suppress_history = false;
#endif

#ifdef USE_DX11 //
	if (RImplementation.o.ssfx_motionvectors)
	{
		const bool svp = Device.m_SecondViewport.IsSVPFrame(); // pip this viewport's prev-matrix slot
		if (!suppress_history && Device.dwViewport > Parent->CurrentFrame)
		{
			// Save current frame
			Parent->CurrentFrame = Device.dwViewport;

			// Save prev m_W and save current m_W for the next frame
			Parent->Matrix_Prev[svp].set(Parent->Matrix_Temp[svp]);
			Parent->Matrix_Temp[svp].set(RCache.xforms.m_w);

			// Save bone matrix to use in the next frame
			for (u16 b = 0; b < Parent->LL_BoneCount(); b++)
			{
				CBoneInstance& Bone = Parent->LL_GetBoneInstance(b);
				Bone.mRenderTransform_prev[svp].set(Bone.mRenderTransform_temp[svp]);
				Bone.mRenderTransform_temp[svp].set(render_bones[b].mRenderTransform);
			}
		}

		// Build previous WV & WVP
		if (RenderMode == 1)
		{
			// RM_SINGLE
			Fmatrix Bone_Prev;
			Bone_Prev.mul_43(Parent->Matrix_Prev[svp], Parent->LL_GetBoneInstance(u16(RMS_boneid)).mRenderTransform_prev[svp]);
			p_WV.mul_43(RCache.xforms.m_v_prev[svp], Bone_Prev);
			p_WVP.mul(RCache.xforms.m_p_prev[svp], p_WV);
		}
		else
		{
			// RM_SKINNING_1B ~ RM_SKINNING_4B
			p_WV.mul_43(RCache.xforms.m_v_prev[svp], Parent->Matrix_Prev[svp]);
			p_WVP.mul(RCache.xforms.m_p_prev[svp], p_WV);
		}

		RCache.set_c("m_wvp_prev", p_WVP); // Apply prev matrix
	}
#endif

	RCache.stat.r.s_dynamic.add(vCount);
	switch (RenderMode)
	{
	case RM_SKINNING_SOFT:
		_Render_soft(hGeom, vCount, iOffset, pCount, render_bones);
		RCache.stat.r.s_dynamic_sw.add(vCount);
		break;
	case RM_SINGLE:
		{
			PROF_EVENT("RM_SINGLE");
			Fmatrix W;
			W.mul_43(RCache.xforms.m_w, render_bones[u16(RMS_boneid)].mRenderTransform);
			RCache.set_xform_world(W);
			//
			RCache.set_Geometry(hGeom);
			RCache.Render(D3DPT_TRIANGLELIST, 0, 0, vCount, iOffset, pCount);
			RCache.stat.r.s_dynamic_inst.add(vCount);
		}
		break;
	case RM_SKINNING_1B:
	case RM_SKINNING_2B:
	case RM_SKINNING_3B:
	case RM_SKINNING_4B:
		{
			PROF_EVENT("RM_SKINNING");
			// Transfer matrices ( current and previous )
			R_constant* array = RCache.get_c(s_bones_array_const);
			R_constant* array_prev = RCache.RenderPhase(RImplementation.phase) == RImplementation.PHASE_NORMAL ? RCache.get_c(s_bones_array_prev_const) : array;

			{
				PROF_EVENT("SEND_MATRICES");
				u32 count = RMS_bonecount;
#if defined(USE_DX11) && RENDER == R_R4
                // the cached palette mirrors bone_instances; a PiP frozen HUD pose (render_bones points
                // at the latched snapshot) uploads through the per-bone loop below
                const bool live_pose = render_bones == Parent->bone_instances;
                const u32 viewport = Device.m_SecondViewport.IsSVPFrame() ? 1 : 0;
                const Fvector4* current_rows = live_pose ? Parent->RenderPalette(count, false) : nullptr;
                const bool previous_needed = RCache.RenderPhase(RImplementation.phase) == RImplementation.PHASE_NORMAL && RImplementation.o.ssfx_motionvectors;
                const Fvector4* previous_rows = live_pose && previous_needed ? Parent->RenderPalette(count, true, viewport) : nullptr;
                if (current_rows && (!previous_needed || previous_rows))
                {
                    const bool current_block = RCache.set_vertex_array_block(array, current_rows, count * 3);
                    const bool previous_block = !previous_rows || RCache.set_vertex_array_block(array_prev, previous_rows, count * 3);
                    if (!current_block || !previous_block)
                    {
                        for (u32 row = 0; row < count * 3; ++row)
                        {
                            if (!current_block) RCache.set_ca(array, row, current_rows[row]);
                            if (!previous_block) RCache.set_ca(array_prev, row, previous_rows[row]);
                        }
                    }
                }
                else
#endif
                for (u32 mid = 0; mid < count; mid++)
				{
					Fmatrix& M = render_bones[u16(mid)].mRenderTransform;
					u32 id = mid * 3;
					RCache.set_ca(&*array, id + 0, M._11, M._21, M._31, M._41);
					RCache.set_ca(&*array, id + 1, M._12, M._22, M._32, M._42);
					RCache.set_ca(&*array, id + 2, M._13, M._23, M._33, M._43);

#ifdef USE_DX11
					if (RCache.RenderPhase(RImplementation.phase) == RImplementation.PHASE_NORMAL)
					{
						if (RImplementation.o.ssfx_motionvectors)
						{
							// Save previous transform
							Fmatrix& Mprev = Parent->LL_GetBoneInstance(u16(mid)).mRenderTransform_prev[Device.m_SecondViewport.IsSVPFrame()];
							RCache.set_ca(&*array_prev, id + 0, Mprev._11, Mprev._21, Mprev._31, Mprev._41);
							RCache.set_ca(&*array_prev, id + 1, Mprev._12, Mprev._22, Mprev._32, Mprev._42);
							RCache.set_ca(&*array_prev, id + 2, Mprev._13, Mprev._23, Mprev._33, Mprev._43);
						}
					}
#endif
				}
			}

			// render
			RCache.set_Geometry(hGeom);
			RCache.Render(D3DPT_TRIANGLELIST, 0, 0, vCount, iOffset, pCount);
			if (RM_SKINNING_1B == RenderMode)
				RCache.stat.r.s_dynamic_1B.add(vCount);
			else if (RM_SKINNING_2B == RenderMode)
				RCache.stat.r.s_dynamic_2B.add(vCount);
			else if (RM_SKINNING_3B == RenderMode)
				RCache.stat.r.s_dynamic_3B.add(vCount);
			else if (RM_SKINNING_4B == RenderMode)
				RCache.stat.r.s_dynamic_4B.add(vCount);
		}
		break;
	}
}

void CSkeletonX::_Render_soft(ref_geom& hGeom, u32 vCount, u32 iOffset, u32 pCount, CBoneInstance* bones)
{
	PROF_EVENT("CSkeletonX::_Render_soft");
	u32 vOffset = cache_vOffset;

	_VertexStream& _VS = RCache.Vertex;
    u32* discard = &cache_DiscardID;
    u32* count = &cache_vCount;
    u32* offset = &cache_vOffset;
#if defined(USE_DX11) && RENDER == R_R4
    if (RenderContextId())
    {
        auto& cache = context_soft_cache[RenderContextId()];
        if (cache.generation != RenderContextGeneration())
        {
            cache = {};
            cache.generation = RenderContextGeneration();
        }
        discard = &cache.discard;
        count = &cache.count;
        offset = &cache.offset;
        vOffset = *offset;
    }
#endif
	if (*discard != _VS.DiscardID() || vCount != *count)
	{
		vertRender* Dest = (vertRender*)_VS.Lock(vCount, hGeom->vb_stride, vOffset);
        *discard = _VS.DiscardID();
        *count = vCount;
        *offset = vOffset;

        if (!RenderContextId()) RDEVICE.Statistic->RenderDUMP_SKIN.Begin();
		if (*Vertices1W)
		{
			PSGP.skin1W(
				Dest, // dest
				*Vertices1W, // source
				vCount, // count
				bones
			);
		}
		else if (*Vertices2W)
		{
			PSGP.skin2W(
				Dest, // dest
				*Vertices2W, // source
				vCount, // count
				bones
			);
		}
		else if (*Vertices3W)
		{
			PSGP.skin3W(
				Dest, // dest
				*Vertices3W, // source
				vCount, // count
				bones
			);
		}
		else if (*Vertices4W)
		{
			PSGP.skin4W(
				Dest, // dest
				*Vertices4W, // source
				vCount, // count
				bones
			);
		}
		else
			R_ASSERT2(0, "unsupported soft rendering");

        if (!RenderContextId()) RDEVICE.Statistic->RenderDUMP_SKIN.End();
		_VS.Unlock(vCount, hGeom->vb_stride);
	}

	RCache.set_Geometry(hGeom);
#if defined(USE_DX11) && RENDER == R_R4
    if (RenderContextId()) RCache.set_Vertices(_VS.Buffer(), hGeom->vb_stride);
#endif
	RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, vCount, iOffset, pCount);
}

void CSkeletonX::_Load(const char* N, IReader* data, u32& dwVertCount)
{
	s_bones_array_const = "sbones_array";
	s_bones_array_prev_const = "sbones_array_prev";
	xr_vector<u16> bids;

	// Load vertices
	R_ASSERT(data->find_chunk(OGF_VERTICES));

	//u16			hw_bones_cnt		= u16((HW.Caps.geometry.dwRegisters-22)/3);
	//	Igor: some shaders in r1 need more free constant registers
	u16 hw_bones_cnt = u16((HW.Caps.geometry.dwRegisters - 22 - 3) / 3);

#if RENDER == R_R1
	if (ps_r1_SoftwareSkinning == 1)
		hw_bones_cnt = 0;
#endif // RENDER == R_R1

	u16 sw_bones_cnt = 0;
#ifdef _EDITOR
	hw_bones_cnt					= 0;
#endif

	u32 dwVertType, size, it, crc;
	dwVertType = data->r_u32();
	dwVertCount = data->r_u32();

	RenderMode = RM_SKINNING_SOFT;
	Engine.External.SetSkinningMode();

	switch (dwVertType)
	{
	case OGF_VERTEXFORMAT_FVF_1L: // 1-Link
	case 1:
		{
			size = dwVertCount * sizeof(vertBoned1W);
			vertBoned1W* pVO = (vertBoned1W*)data->pointer();

			for (it = 0; it < dwVertCount; ++it)
			{
				const vertBoned1W& VB = pVO[it];
				u16 mid = (u16)VB.matrix;

				if (bids.end() == std::find(bids.begin(), bids.end(), mid))
					bids.push_back(mid);

				sw_bones_cnt = _max(sw_bones_cnt, mid);
			}
#ifdef _EDITOR
			// software
			crc						= crc32	(data->pointer(),size);
			Vertices1W.create		(crc,dwVertCount,(vertBoned1W*)data->pointer());
#else
			if (1 == bids.size())
			{
				// HW- single bone
				RenderMode = RM_SINGLE;
				RMS_boneid = *bids.begin();
				Engine.External.SetSkinningMode(0);
			}
			else if (sw_bones_cnt <= hw_bones_cnt)
			{
				// HW- one weight
				RenderMode = RM_SKINNING_1B;
				RMS_bonecount = sw_bones_cnt + 1;
				Engine.External.SetSkinningMode(1);
			}
			else
			{
				// software
				crc = crc32(data->pointer(), size);
				Vertices1W.create(crc, dwVertCount, (vertBoned1W*)data->pointer());
				Engine.External.SetSkinningMode();
			}
#endif
		}
		break;
	case OGF_VERTEXFORMAT_FVF_2L: // 2-Link
	case 2:
		{
			size = dwVertCount * sizeof(vertBoned2W);
			vertBoned2W* pVO = (vertBoned2W*)data->pointer();

			for (it = 0; it < dwVertCount; ++it)
			{
				const vertBoned2W& VB = pVO[it];
				sw_bones_cnt = _max(sw_bones_cnt, VB.matrix0);
				sw_bones_cnt = _max(sw_bones_cnt, VB.matrix1);

				if (bids.end() == std::find(bids.begin(), bids.end(), VB.matrix0))
					bids.push_back(VB.matrix0);

				if (bids.end() == std::find(bids.begin(), bids.end(), VB.matrix1))
					bids.push_back(VB.matrix1);
			}
			//.			R_ASSERT(sw_bones_cnt<=hw_bones_cnt);
			if (sw_bones_cnt <= hw_bones_cnt)
			{
				// HW- two weights
				RenderMode = RM_SKINNING_2B;
				RMS_bonecount = sw_bones_cnt + 1;
				Engine.External.SetSkinningMode(2);
			}
			else
			{
				// software
				crc = crc32(data->pointer(), size);
				Vertices2W.create(crc, dwVertCount, (vertBoned2W*)data->pointer());
				Engine.External.SetSkinningMode();
			}
		}
		break;
	case OGF_VERTEXFORMAT_FVF_3L: // 3-Link
	case 3:
		{
			size = dwVertCount * sizeof(vertBoned3W);
			vertBoned3W* pVO = (vertBoned3W*)data->pointer();

			for (it = 0; it < dwVertCount; ++it)
			{
				const vertBoned3W& VB = pVO[it];
				for (int i = 0; i < 3; ++i)
				{
					sw_bones_cnt = _max(sw_bones_cnt, VB.m[i]);

					if (bids.end() == std::find(bids.begin(), bids.end(), VB.m[i]))
						bids.push_back(VB.m[i]);
				}
			}
			//.			R_ASSERT(sw_bones_cnt<=hw_bones_cnt);
			if ((sw_bones_cnt <= hw_bones_cnt))
			{
				RenderMode = RM_SKINNING_3B;
				RMS_bonecount = sw_bones_cnt + 1;
				Engine.External.SetSkinningMode(3);
			}
			else
			{
				crc = crc32(data->pointer(), size);
				Vertices3W.create(crc, dwVertCount, (vertBoned3W*)data->pointer());
				Engine.External.SetSkinningMode();
			}
		}
		break;
	case OGF_VERTEXFORMAT_FVF_4L: // 4-Link
	case 4:
		{
			size = dwVertCount * sizeof(vertBoned4W);
			vertBoned4W* pVO = (vertBoned4W*)data->pointer();

			for (it = 0; it < dwVertCount; ++it)
			{
				const vertBoned4W& VB = pVO[it];

				for (int i = 0; i < 4; ++i)
				{
					sw_bones_cnt = _max(sw_bones_cnt, VB.m[i]);

					if (bids.end() == std::find(bids.begin(), bids.end(), VB.m[i]))
						bids.push_back(VB.m[i]);
				}
			}
			//.			R_ASSERT(sw_bones_cnt<=hw_bones_cnt);
			if (sw_bones_cnt <= hw_bones_cnt)
			{
				RenderMode = RM_SKINNING_4B;
				RMS_bonecount = sw_bones_cnt + 1;
				Engine.External.SetSkinningMode(4);
			}
			else
			{
				crc = crc32(data->pointer(), size);
				Vertices4W.create(crc, dwVertCount, (vertBoned4W*)data->pointer());
				Engine.External.SetSkinningMode();
			}
		}
		break;
	default:
		Debug.fatal(DEBUG_INFO, "Invalid vertex type in skinned model '%s'", N);
		break;
	}
#ifdef _EDITOR
	if (bids.size()>0)	
#else
	if (bids.size()>0)		//--DSR-- SilencerOverheat (1 -> 0). Why was 1 tho?
#endif
	{
		crc = crc32(&*bids.begin(), bids.size() * sizeof(u16));
		BonesUsed.create(crc, bids.size(), &*bids.begin());
	}
}

void CSkeletonX::BindScopeSurfaceFrames(CBoneInstance* render_bones)
{
#if RENDER == R_R4
    if (!RCache.get_c("scope_glass_surface_control")) return;
    // Always reset on the actual draw. No ADS packet or previous gun can supply
    // a stale hip-fire frame, and unsupported assets retain native normals.
    RCache.set_c("scope_glass_surface_control", 0.f, 0.f, 0.f, 0.f);
    if (!Parent || !render_bones || !IsPhysicalScopeGlass()) return;
    if (!scope_surface_cached)
    {
        scope_surface_cached = true;
        std::vector<scope_surface_vertex> vertices;
        auto add = [&](const Fvector& p,const Fvector& n,const Fvector& t,unsigned bone) {
            vertices.push_back({{p.x,p.y,p.z},{n.x,n.y,n.z},{t.x,t.y,t.z},bone});
        };
        if (*Vertices1W)
            for (u32 i=0;i<Vertices1W.size();++i) { const auto& v=Vertices1W[i]; add(v.P,v.N,v.T,v.matrix); }
        else if (*Vertices2W)
            for (u32 i=0;i<Vertices2W.size();++i) { const auto& v=Vertices2W[i]; if(v.matrix0!=v.matrix1)return; add(v.P,v.N,v.T,v.matrix0); }
        else if (*Vertices3W)
            for (u32 i=0;i<Vertices3W.size();++i) { const auto& v=Vertices3W[i]; if(v.m[0]!=v.m[1]||v.m[0]!=v.m[2])return; add(v.P,v.N,v.T,v.m[0]); }
        else if (*Vertices4W)
            for (u32 i=0;i<Vertices4W.size();++i) { const auto& v=Vertices4W[i]; if(v.m[0]!=v.m[1]||v.m[0]!=v.m[2]||v.m[0]!=v.m[3])return; add(v.P,v.N,v.T,v.m[0]); }
        std::vector<unsigned> indices;
        if (*m_Indices) for(u32 i=0;i<m_Indices.size();++i)indices.push_back(m_Indices[i]);
        scope_surface_frames = scope_surface_fit(vertices,indices);
        extern int ps_r__svp_cop_diag;
        if(ps_r__svp_cop_diag)
            Msg("[Physical glass surface] frames=%u vertices=%u triangles=%u singleBone=%u",
                unsigned(scope_surface_frames.size()),unsigned(vertices.size()),unsigned(indices.size()/3),
                scope_surface_frames.empty()?unsigned(-1):scope_surface_frames[0].bone);
    }
    static LPCSTR centers[4]={"scope_glass_surface_center0","scope_glass_surface_center1","scope_glass_surface_center2","scope_glass_surface_center3"};
    static LPCSTR axes[4]={"scope_glass_surface_axis0","scope_glass_surface_axis1","scope_glass_surface_axis2","scope_glass_surface_axis3"};
    static LPCSTR rights[4]={"scope_glass_surface_right0","scope_glass_surface_right1","scope_glass_surface_right2","scope_glass_surface_right3"};
    static LPCSTR ups[4]={"scope_glass_surface_up0","scope_glass_surface_up1","scope_glass_surface_up2","scope_glass_surface_up3"};
    unsigned count=0;
    for(const auto& frame:scope_surface_frames)
    {
        if(frame.bone>=Parent->LL_BoneCount())return;
        Fmatrix world; world.mul_43(RCache.xforms.m_w,render_bones[frame.bone].mRenderTransform);
        auto direction=[&](const svp_v3& value){Fvector p,r;p.set(value.x,value.y,value.z);world.transform_dir(r,p);return r;};
        Fvector c,local;local.set(frame.center.x,frame.center.y,frame.center.z);world.transform_tiny(c,local);
        Fvector n=direction(frame.axis),t=direction(frame.right),b=direction(frame.up);
        const float ns=n.magnitude(),ts=t.magnitude(),bs=b.magnitude();
        // Existing HUD skinning assumes rigid/uniform transforms. Do not
        // invent inverse-transpose/radius behavior for deformed model scales.
        if(ns<EPS||ts<EPS||bs<EPS||_abs(ts/bs-1.f)>.01f||_abs(ns/ts-1.f)>.01f)return;
        n.div(ns);t.div(ts);b.div(bs);
        RCache.set_c(centers[count],c.x,c.y,c.z,frame.radius*ts);
        RCache.set_c(axes[count],n.x,n.y,n.z,frame.halfDepth*ns);
        RCache.set_c(rights[count],t.x,t.y,t.z,0.f);
        RCache.set_c(ups[count],b.x,b.y,b.z,0.f);
        ++count;
    }
    RCache.set_c("scope_glass_surface_control",float(count),0.f,0.f,0.f);
#endif
}

u64 CSkeletonX::ScopeGlassBoneMask()
{
    if (RM_SINGLE == RenderMode) return u64(1) << RMS_boneid;
    u64 mask = 0;
    for (u32 i = 0; i < BonesUsed.size(); ++i) mask |= u64(1) << BonesUsed[i];
    return mask;
}

bool CSkeletonX::IsPhysicalScopeGlass()
{
#if RENDER == R_R4
    auto* visual = smart_cast<dxRender_Visual*>(this);
    if (visual && visual->shader._get())
        for (const auto& element : visual->shader->E)
            if (element._get())
                for (const auto& pass : element->passes)
                    if (pass->constants._get() && pass->constants->get("scope_reference_physical_glass"))
                        return true;
#endif
    return false;
}

BOOL CSkeletonX::has_visible_bones()
{
	if (RM_SINGLE == RenderMode)
	{
		return Parent->LL_GetBoneVisible((u16)RMS_boneid);
	}

	for (u32 it = 0; it < BonesUsed.size(); it++)
		if (Parent->LL_GetBoneVisible(BonesUsed[it]))
		{
			return TRUE;
		}
	return FALSE;
}


void get_pos_bones(const vertBoned1W& v, Fvector& p, CKinematics* Parent)
{
	const Fmatrix& xform = Parent->LL_GetBoneInstance((u16)v.matrix).mRenderTransform;
	xform.transform_tiny(p, v.P);
}

void get_pos_bones(const vertBoned2W& vert, Fvector& p, CKinematics* Parent)
{
	Fvector P0, P1;

	Fmatrix& xform0 = Parent->LL_GetBoneInstance(vert.matrix0).mRenderTransform;
	Fmatrix& xform1 = Parent->LL_GetBoneInstance(vert.matrix1).mRenderTransform;
	xform0.transform_tiny(P0, vert.P);
	xform1.transform_tiny(P1, vert.P);
	p.lerp(P0, P1, vert.w);
}

void get_pos_bones(const vertBoned3W& vert, Fvector& p, CKinematics* Parent)
{
	Fmatrix& M0 = Parent->LL_GetBoneInstance(vert.m[0]).mRenderTransform;
	Fmatrix& M1 = Parent->LL_GetBoneInstance(vert.m[1]).mRenderTransform;
	Fmatrix& M2 = Parent->LL_GetBoneInstance(vert.m[2]).mRenderTransform;

	Fvector P0, P1, P2;
	M0.transform_tiny(P0, vert.P);
	P0.mul(vert.w[0]);
	M1.transform_tiny(P1, vert.P);
	P1.mul(vert.w[1]);
	M2.transform_tiny(P2, vert.P);
	P2.mul(1.0f - vert.w[0] - vert.w[1]);

	p = P0;
	p.add(P1);
	p.add(P2);
}

void get_pos_bones(const vertBoned4W& vert, Fvector& p, CKinematics* Parent)
{
	Fmatrix& M0 = Parent->LL_GetBoneInstance(vert.m[0]).mRenderTransform;
	Fmatrix& M1 = Parent->LL_GetBoneInstance(vert.m[1]).mRenderTransform;
	Fmatrix& M2 = Parent->LL_GetBoneInstance(vert.m[2]).mRenderTransform;
	Fmatrix& M3 = Parent->LL_GetBoneInstance(vert.m[3]).mRenderTransform;

	Fvector P0, P1, P2, P3;
	M0.transform_tiny(P0, vert.P);
	P0.mul(vert.w[0]);
	M1.transform_tiny(P1, vert.P);
	P1.mul(vert.w[1]);
	M2.transform_tiny(P2, vert.P);
	P2.mul(vert.w[2]);
	M3.transform_tiny(P3, vert.P);
	P3.mul(1.0f - vert.w[0] - vert.w[1] - vert.w[2]);

	p = P0;
	p.add(P1);
	p.add(P2);
	p.add(P3);
}


//-----------------------------------------------------------------------------------------------------
// Wallmarks
//-----------------------------------------------------------------------------------------------------
#include "cl_intersect.h"

BOOL CSkeletonX::_PickBoneSoft1W(IKinematics::pick_result& r, float dist, const Fvector& S, const Fvector& D,
                                 u16* indices, CBoneData::FacesVec& faces)
{
	return pick_bone<vertBoned1W>(Vertices1W, Parent, r, dist, S, D, indices, faces);
}

BOOL CSkeletonX::_PickBoneSoft2W(IKinematics::pick_result& r, float dist, const Fvector& S, const Fvector& D,
                                 u16* indices, CBoneData::FacesVec& faces)
{
	return pick_bone<vertBoned2W>(Vertices2W, Parent, r, dist, S, D, indices, faces);
}

BOOL CSkeletonX::_PickBoneSoft3W(IKinematics::pick_result& r, float dist, const Fvector& S, const Fvector& D,
                                 u16* indices, CBoneData::FacesVec& faces)
{
	return pick_bone<vertBoned3W>(Vertices3W, Parent, r, dist, S, D, indices, faces);
}

BOOL CSkeletonX::_PickBoneSoft4W(IKinematics::pick_result& r, float dist, const Fvector& S, const Fvector& D,
                                 u16* indices, CBoneData::FacesVec& faces)
{
	return pick_bone<vertBoned4W>(Vertices4W, Parent, r, dist, S, D, indices, faces);
}

/*
BOOL	CSkeletonX::_PickBoneSoft1W	(Fvector& normal, float& dist, const Fvector& S, const Fvector& D, u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY				(*Vertices1W);
	bool intersect		= FALSE;
	for (CBoneData::FacesVecIt it=faces.begin(); it!=faces.end(); it++){
		Fvector			p[3];
		u32 idx			= (*it)*3;
		for (u32 k=0; k<3; k++){
			vertBoned1W& vert		= Vertices1W[indices[idx+k]];
			get_pos_bones(vert, p[k], Parent);
		}
		float u,v,range	= flt_max;
		if (CDB::TestRayTri(S,D,p,u,v,range,true)&&(range<dist)){
			normal.mknormal(p[0],p[1],p[2]);
			dist		= range;
			intersect	= TRUE;
		}
	}
	return intersect;
}

BOOL CSkeletonX::_PickBoneSoft2W	(Fvector& normal, float& dist, const Fvector& S, const Fvector& D, u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY				(*Vertices2W);
	bool intersect		= FALSE;
	for (CBoneData::FacesVecIt it=faces.begin(); it!=faces.end(); it++){
		Fvector			p[3];
		u32 idx			= (*it)*3;
		for (u32 k=0; k<3; k++){
			vertBoned2W& vert		= Vertices2W[indices[idx+k]];
			get_pos_bones(vert, p[k], Parent);
		}
		float u,v,range	= flt_max;
		if (CDB::TestRayTri(S,D,p,u,v,range,true)&&(range<dist)){
			normal.mknormal(p[0],p[1],p[2]);
			dist		= range;
			intersect	= TRUE;
		}
	}
	return intersect;
}
*/
// Fill Vertices
void CSkeletonX::_FillVerticesSoft1W(const Fmatrix& view, CSkeletonWallmark& wm, const Fvector& normal, float size,
                                     u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY(*Vertices1W);
	for (CBoneData::FacesVecIt it = faces.begin(); it != faces.end(); it++)
	{
		Fvector p[3];
		u32 idx = (*it) * 3;
		CSkeletonWallmark::WMFace F;
		for (u32 k = 0; k < 3; k++)
		{
			vertBoned1W& vert = Vertices1W[indices[idx + k]];
			F.bone_id[k][0] = (u16)vert.matrix;
			F.bone_id[k][1] = F.bone_id[k][0];
			F.bone_id[k][2] = F.bone_id[k][0];
			F.bone_id[k][3] = F.bone_id[k][0];
			F.weight[k][0] = 0.f;
			F.weight[k][1] = 0.f;
			F.weight[k][2] = 0.f;

			const Fmatrix& xform = Parent->LL_GetBoneInstance(F.bone_id[k][0]).mRenderTransform;
			F.vert[k].set(vert.P);
			xform.transform_tiny(p[k], F.vert[k]);
		}
		Fvector test_normal;
		test_normal.mknormal(p[0], p[1], p[2]);
		float cosa = test_normal.dotproduct(normal);
		if (cosa < EPS) continue;
		if (CDB::TestSphereTri(wm.ContactPoint(), size, p))
		{
			Fvector UV;
			for (u32 k = 0; k < 3; k++)
			{
				Fvector2& uv = F.uv[k];
				view.transform_tiny(UV, p[k]);
				uv.x = (1 + UV.x) * .5f;
				uv.y = (1 - UV.y) * .5f;
			}
			wm.m_Faces.push_back(F);
		}
	}
}

void CSkeletonX::_FillVerticesSoft2W(const Fmatrix& view, CSkeletonWallmark& wm, const Fvector& normal, float size,
                                     u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY(*Vertices2W);
	for (CBoneData::FacesVecIt it = faces.begin(); it != faces.end(); it++)
	{
		Fvector p[3];
		u32 idx = (*it) * 3;
		CSkeletonWallmark::WMFace F;
		for (u32 k = 0; k < 3; k++)
		{
			Fvector P0, P1;

			vertBoned2W& vert = Vertices2W[indices[idx + k]];
			F.bone_id[k][0] = vert.matrix0;
			F.bone_id[k][1] = vert.matrix1;
			F.bone_id[k][2] = F.bone_id[k][1];
			F.bone_id[k][3] = F.bone_id[k][1];
			F.weight[k][0] = vert.w;
			F.weight[k][1] = 0.f;
			F.weight[k][2] = 0.f;

			Fmatrix& xform0 = Parent->LL_GetBoneInstance(F.bone_id[k][0]).mRenderTransform;
			Fmatrix& xform1 = Parent->LL_GetBoneInstance(F.bone_id[k][1]).mRenderTransform;
			F.vert[k].set(vert.P);
			xform0.transform_tiny(P0, F.vert[k]);
			xform1.transform_tiny(P1, F.vert[k]);
			p[k].lerp(P0, P1, F.weight[k][0]);
		}
		Fvector test_normal;
		test_normal.mknormal(p[0], p[1], p[2]);
		float cosa = test_normal.dotproduct(normal);
		if (cosa < EPS) continue;
		if (CDB::TestSphereTri(wm.ContactPoint(), size, p))
		{
			Fvector UV;
			for (u32 k = 0; k < 3; k++)
			{
				Fvector2& uv = F.uv[k];
				view.transform_tiny(UV, p[k]);
				uv.x = (1 + UV.x) * .5f;
				uv.y = (1 - UV.y) * .5f;
			}
			wm.m_Faces.push_back(F);
		}
	}
}

void CSkeletonX::_FillVerticesSoft3W(const Fmatrix& view, CSkeletonWallmark& wm, const Fvector& normal, float size,
                                     u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY(*Vertices3W);
	for (CBoneData::FacesVecIt it = faces.begin(); it != faces.end(); ++it)
	{
		Fvector p[3];
		u32 idx = (*it) * 3;
		CSkeletonWallmark::WMFace F;

		for (u32 k = 0; k < 3; k++)
		{
			const vertBoned3W& vert = Vertices3W[indices[idx + k]];
			F.bone_id[k][0] = vert.m[0];
			F.bone_id[k][1] = vert.m[1];
			F.bone_id[k][2] = vert.m[2];
			F.bone_id[k][3] = F.bone_id[k][2];
			F.weight[k][0] = vert.w[0];
			F.weight[k][1] = vert.w[1];
			F.weight[k][2] = 0.f;
			vert.get_pos(F.vert[k]);
			get_pos_bones(vert, p[k], Parent);
		}
		Fvector test_normal;
		test_normal.mknormal(p[0], p[1], p[2]);
		float cosa = test_normal.dotproduct(normal);
		if (cosa < EPS) continue;

		if (CDB::TestSphereTri(wm.ContactPoint(), size, p))
		{
			Fvector UV;
			for (u32 k = 0; k < 3; k++)
			{
				Fvector2& uv = F.uv[k];
				view.transform_tiny(UV, p[k]);
				uv.x = (1 + UV.x) * .5f;
				uv.y = (1 - UV.y) * .5f;
			}
			wm.m_Faces.push_back(F);
		}
	}
}

void CSkeletonX::_FillVerticesSoft4W(const Fmatrix& view, CSkeletonWallmark& wm, const Fvector& normal, float size,
                                     u16* indices, CBoneData::FacesVec& faces)
{
	VERIFY(*Vertices4W);
	for (CBoneData::FacesVecIt it = faces.begin(); it != faces.end(); ++it)
	{
		Fvector p[3];
		u32 idx = (*it) * 3;
		CSkeletonWallmark::WMFace F;

		for (u32 k = 0; k < 3; k++)
		{
			const vertBoned4W& vert = Vertices4W[indices[idx + k]];
			F.bone_id[k][0] = vert.m[0];
			F.bone_id[k][1] = vert.m[1];
			F.bone_id[k][2] = vert.m[2];
			F.bone_id[k][3] = vert.m[3];
			F.weight[k][0] = vert.w[0];
			F.weight[k][1] = vert.w[1];
			F.weight[k][2] = vert.w[2];
			vert.get_pos(F.vert[k]);
			get_pos_bones(vert, p[k], Parent);
		}
		Fvector test_normal;
		test_normal.mknormal(p[0], p[1], p[2]);
		float cosa = test_normal.dotproduct(normal);
		if (cosa < EPS) continue;

		if (CDB::TestSphereTri(wm.ContactPoint(), size, p))
		{
			Fvector UV;
			for (u32 k = 0; k < 3; k++)
			{
				Fvector2& uv = F.uv[k];
				view.transform_tiny(UV, p[k]);
				uv.x = (1 + UV.x) * .5f;
				uv.y = (1 - UV.y) * .5f;
			}
			wm.m_Faces.push_back(F);
		}
	}
}

#if defined(USE_DX10) || defined(USE_DX11)
void CSkeletonX::_DuplicateIndices(const char* N, IReader* data)
{
	//	We will have trouble with container since don't know were to take readable indices
	VERIFY(!data->find_chunk(OGF_ICONTAINER));
	//	Index buffer replica since we can't read from index buffer in DX10
	//ref_smem<u16>			Indices;
	R_ASSERT(data->find_chunk(OGF_INDICES));
	u32 iCount = data->r_u32();

	u32 size = iCount * 2;
	u32 crc = crc32(data->pointer(), size);
	m_Indices.create(crc, iCount, (u16*)data->pointer());
}
#endif	//	USE_DX10

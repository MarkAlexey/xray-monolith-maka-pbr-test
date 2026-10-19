#include "stdafx.h"
#include "ISpatial.h"
#include "frustum.h"

extern Fvector c_spatial_offset[8];

class _MM_ALIGN16 spatial_frustum_walker
{
public:
	u32 mask;
	CFrustum* F;
	ISpatial_DB* space;
	Fbox BB;

public:
	spatial_frustum_walker(ISpatial_DB* _space, u32 _mask, const CFrustum* _F)
	{
		mask = _mask;
		F = (CFrustum*)_F;
		space = _space;
	}

	void walk(xr_vector<ISpatialShared>& R, ISpatial_NODE* N, Fvector& n_C, float n_R, u32 fmask)
	{
		// box
		float n_vR = 2 * n_R;
		BB.set(n_C.x - n_vR, n_C.y - n_vR, n_C.z - n_vR, n_C.x + n_vR, n_C.y + n_vR, n_C.z + n_vR);

		if (fcvNone == F->testAABB(BB.data(), fmask))
			return;

		// test items
		for (ISpatialShared S : N->items)
		{
			if (!S.get()) continue;
			if (0 == (S->spatial.type & mask))
				continue;

			Fvector& sC = S->spatial.sphere.P;
			float sR = S->spatial.sphere.R;
			u32 tmask = fmask;

			if (fcvNone == F->testSphere(sC, sR, tmask))
				continue;

			R.push_back(S);
		}

		// recurse
		float c_R = n_R / 2;
		for (u32 octant = 0; octant < 8; octant++)
		{
			if (0 == N->children[octant])
				continue;

			Fvector c_C;
			c_C.mad(n_C, c_spatial_offset[octant], c_R);
			walk(R, N->children[octant], c_C, c_R, fmask);
		}
	}
};

void ISpatial_DB::q_frustum(xr_vector<ISpatialShared>& R, u32 _o, u32 _mask, const CFrustum& _frustum)
{
    q_frustum_snapshot(R, _mask, _frustum);
}

u64 ISpatial_DB::q_frustum_snapshot(xr_vector<ISpatialShared>& R, u32 _mask, const CFrustum& _frustum)
{
	PROF_EVENT("ISpatial_DB::q_frustum");
	xrSRWLockGuard guard(&db_lock, true);
	R.clear();
    if (m_root)
    {
        spatial_frustum_walker W(this, _mask, &_frustum);
        W.walk(R, m_root, m_center, m_bounds, _frustum.getMask());
    }
    return query_revision.load(std::memory_order_relaxed);
}

u64 ISpatial_DB::q_frustums(xr_vector<ISpatialShared> (&results)[3], u32 mask, const CFrustum* const (&views)[3])
{
    PROF_EVENT("ISpatial_DB::q_frustum.cascades");
    for (auto& list : results) list.clear_not_free();
    xrSRWLockGuard guard(&db_lock, true);
    u32 initial[3]{};
    u32 active = 0;
    for (u32 i = 0; i < 3; ++i) if (views[i]) { initial[i] = views[i]->getMask(); active |= 1u << i; }
    auto walk = [&](auto&& self, ISpatial_NODE* node, const Fvector& center, float radius,
        const u32* parent_masks, u32 bits) -> void
    {
        Fbox box; box.setb(center, Fvector().set(2 * radius, 2 * radius, 2 * radius));
        u32 masks[3] = {parent_masks[0], parent_masks[1], parent_masks[2]};
        for (u32 i = 0; i < 3; ++i)
            if ((bits & (1u << i)) && views[i]->testAABB(box.data(), masks[i]) == fcvNone) bits &= ~(1u << i);
        if (!bits) return;
        for (const ISpatialShared& spatial : node->items)
        {
            if (!spatial || !(spatial->spatial.type & mask)) continue;
            const Fsphere& sphere = spatial->spatial.sphere;
            Fvector position = sphere.P;
            for (u32 i = 0; i < 3; ++i)
            {
                u32 item_mask = masks[i];
                if ((bits & (1u << i)) && views[i]->testSphere(position, sphere.R, item_mask) != fcvNone)
                    results[i].emplace_back(spatial);
            }
        }
        for (u32 i = 0; i < 8; ++i) if (node->children[i])
        {
            Fvector child; child.mad(center, c_spatial_offset[i], radius * .5f);
            self(self, node->children[i], child, radius * .5f, masks, bits);
        }
    };
    if (m_root && active) walk(walk, m_root, m_center, m_bounds, initial, active);
    return query_revision.load(std::memory_order_relaxed);
}

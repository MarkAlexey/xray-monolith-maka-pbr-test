#pragma once
#include "r__dsgraph_types.h"
#include "r__sector.h"

namespace sun
{
	struct ray
	{
		ray() = default;
		ray(Fvector3 const& _P, Fvector3 const& _D): P(_P), D(_D)
		{
		}
		~ray() = default;

		Fvector3 D;
		Fvector3 P;
	};

	struct cascade
	{
		cascade() : size(0.0f), bias(0.0f), reset_chain(false)
		{
		}
		cascade(const cascade& other)
		{
		}

		Fmatrix xform;
		xr_vector<ray> rays;
		float size;
		float bias;
		bool reset_chain;

		CFrustum cull_frustum;
		Fvector3 cull_COP;
		Fmatrix cull_xform;

		// Monolith sun shadow cache (R4 only, r4_sun_shadow_cache; R_sun.cpp sun_cache_select).
		// What the depth stored in this cascade's map was rendered with. When `skip` is set for
		// the frame, the cascade renders nothing: cull_xform / xform are replaced by `xform`
		// below so lighting reads the stored depth with the transform that wrote it.
		struct shadow_cache
		{
			Fmatrix xform;
			Fvector direction{0.f, -1.f, 0.f};
			float size = 0.f;
			u32 smap = 0;
			u32 frame = 0;
			bool valid = false;
			bool skip = false;
			const char* reason = "never rendered";	// why it was last rendered (string literal)
			u32 rendered = 0, reused = 0;			// since r4_sun_shadow_cache_stats last ran
		} cache;

		CDSGraphManager GMCascade = CDSGraphManager(u32(0), u32(STYPE_RENDERABLE + STYPE_RENDERABLESHADOW), { true,false,false,false,true,false,false });
	};
} //namespace sun

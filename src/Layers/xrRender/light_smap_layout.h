#pragma once

#include <limits>
#include <type_traits>
#include <utility>

// Places every shadowed light in a shadow-map atlas, largest first, starting a
// new atlas (the next smap_ID) whenever the current one is full. Each atlas
// starts empty, so a pass that places nothing can never finish; that and
// running out of smap_ID values are fatal errors instead of an endless loop.
template <typename Light, typename Pool>
void layout_shadow_atlases(xr_vector<Light*>& source, xr_vector<Light*>& placed, Pool& pool, u32 atlas_size)
{
	using AtlasId = std::remove_reference_t<decltype(std::declval<Light&>().vis.smap_ID)>;
	const size_t total = source.size();
	for (u32 atlas = 0; placed.size() != total; ++atlas)
	{
		R_ASSERT2(atlas <= (std::numeric_limits<AtlasId>::max)(), "Shadow map layout needs more atlases than smap_ID can count");
		const AtlasId smap_ID = AtlasId(atlas);
		const size_t placed_before = placed.size();

		pool.initialize(atlas_size);
		std::sort(source.begin(), source.end(), [](Light* _1, Light* _2) { return _1->X.S.size > _2->X.S.size; });
		source.erase(std::remove_if(source.begin(), source.end(), [&](Light* L)
		{
			SMAP_Rect R;
			if (!pool.push(R, L->X.S.size))
				return false;
			L->X.S.posX = R.min.x;
			L->X.S.posY = R.min.y;
			L->vis.smap_ID = smap_ID;
			placed.push_back(L);
			return true;
		}), source.end());

		R_ASSERT2(placed.size() != placed_before, "A light's shadow map does not fit an empty shadow atlas");
	}
}

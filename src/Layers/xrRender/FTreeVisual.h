#pragma once

// refs
struct FSlideWindowItem;

#include "FBasicVisual.h"
#include <memory>
struct TreeShadowMesh;
struct TreeShadowBatch;

class FTreeVisual : public dxRender_Visual, public IRender_Mesh
{
private:
	struct _5color
	{
		Fvector rgb; // - all static lighting
		float hemi; // - hemisphere
		float sun; // - sun
	};

protected:
	_5color c_scale;
	_5color c_bias;
	Fmatrix xform;
#if RENDER != R_R1
    Fmatrix prepared_view, prepared_world, prepared_combined;
    bool prepared_valid = false;
#if defined(USE_DX11) && RENDER == R_R4
    std::shared_ptr<TreeShadowMesh> shadow_mesh;
    std::shared_ptr<TreeShadowBatch> shadow_batch;
    u32 shadow_member = 0;
    u32 shadow_ticket[RenderContextCount] = {}, shadow_frame[RenderContextCount] = {};
    u32 load_vb = u32(-1), load_ib = u32(-1);
#endif
public:
#if defined(USE_DX11) && RENDER == R_R4
    static void BuildShadowGeometry(const xr_vector<dxRender_Visual*>& visuals);
    static void ResetShadowStats();
    static void ProfileShadowStats();
    static void PrepareFrame();
    void SelectShadowBatch(u32 ticket);
    bool DrawShadowBatch(float lod, u32 ticket);
    bool DrawShadowGeometry();
    bool ShadowGeometryValid(bool bound = true) const;
#endif
    const Fmatrix& PreparationTransform() const { return xform; }
    void PublishPreparedView(const Fmatrix& view, const Fmatrix& world, const Fmatrix& combined)
    {
        prepared_view = view;
        prepared_world = world;
        prepared_combined = combined;
        prepared_valid = true;
    }
#endif
public:
	virtual void Render(float LOD); // LOD - Level Of Detail  [0.0f - min, 1.0f - max], Ignored
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();

	FTreeVisual(void);
	virtual ~FTreeVisual(void);
};

class FTreeVisual_ST : public FTreeVisual
{
	typedef FTreeVisual inherited;
public:
	FTreeVisual_ST(void);
	virtual ~FTreeVisual_ST(void);

	virtual void Render(float LOD); // LOD - Level Of Detail  [0.0f - min, 1.0f - max], Ignored
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();
private:
	FTreeVisual_ST(const FTreeVisual_ST& other);
	void operator=(const FTreeVisual_ST& other);
};

class FTreeVisual_PM : public FTreeVisual
{
	typedef FTreeVisual inherited;
private:
	FSlideWindowItem* pSWI;
	u32 last_lod;
public:
	FTreeVisual_PM(void);
	virtual ~FTreeVisual_PM(void);

	virtual void Render(float LOD); // LOD - Level Of Detail  [0.0f - min, 1.0f - max], Ignored
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();
private:
	FTreeVisual_PM(const FTreeVisual_PM& other);
	void operator=(const FTreeVisual_PM& other);
};

const int FTreeVisual_tile = 16;
const int FTreeVisual_quant = 32768 / FTreeVisual_tile;

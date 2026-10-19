#pragma once
#include <memory>
#include <vector>

class light;

class light_Package;

// Render-only copy: no spatial/game callbacks or mutable light::get_LOD().
struct RecordedLight
{
    struct { u32 type = 0; bool bShadow = false; } flags;
    Fmatrix m_xform;
    Fvector position;
    Fcolor color;
    float range = 0.f, lod = 1.f;
    int sss_id = -1;
    struct
    {
        struct
        {
            Fmatrix view, project;
            u32 size = 0, posX = 0, posY = 0;
            BOOL transluent = FALSE;
        } S;
    } X;
    ref_shader s_point, s_spot;
    ref_shader s_point_msaa[8], s_spot_msaa[8];
    std::vector<const SPass*> passes;
    float get_LOD() const { return lod; }
};

// Frame-scoped local shadow pipeline. Queries overlap later scene work;
// complete native shadow graphs record while the owner renders sun/emissive.
class LocalLightShadows
{
    struct State;
    std::unique_ptr<State> state;

public:
    LocalLightShadows();
    ~LocalLightShadows();
    void BeginFrame(light_Package& lights);
    void Record();
    bool RenderShadows(light_Package& lights);
    void EndFrame();
    void Forget(light* source);
    void Clear();
};

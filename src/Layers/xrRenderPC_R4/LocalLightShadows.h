#pragma once
#include <memory>

class light;

class light_Package;

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

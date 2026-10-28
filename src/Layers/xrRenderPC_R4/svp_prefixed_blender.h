#pragma once

// Compiles another blender with the pixel shaders "<prefix><ps>" where those files exist
// (CBlender_Compile::ps_prefix, for this compilation only). The pass states, textures and vertex
// shaders stay the inner blender's, so a wrapper shader runs the installed one in its own pass.
class SvpPrefixedBlender final : public IBlender
{
public:
    SvpPrefixedBlender(IBlender* inner, LPCSTR prefix) : inner(inner), prefix(prefix) {}
    LPCSTR getComment() override { return inner->getComment(); }
    BOOL canBeDetailed() override { return inner->canBeDetailed(); }
    BOOL canBeLMAPped() override { return inner->canBeLMAPped(); }
    BOOL canUseSteepParallax() override { return inner->canUseSteepParallax(); }
    void Compile(CBlender_Compile& C) override
    {
        C.ps_prefix = prefix;
        inner->Compile(C);
        C.ps_prefix = nullptr;
    }
private:
    IBlender* inner;
    LPCSTR prefix;
};

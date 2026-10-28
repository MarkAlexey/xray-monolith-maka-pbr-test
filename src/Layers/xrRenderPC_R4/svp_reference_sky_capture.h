#pragma once
#include "svp_reference_sky_verdict.h"

// Resources of the Reference sky capture (svp_reference_sky.inl), held by the PiP target.
struct SvpReferenceSkyCapture
{
    static constexpr unsigned ReadbackSlots = 3;

    ref_shader installed; // the engine sky blender through the scope_reference_ pixel shader prefix
    ref_shader raw, check;
    ref_texture display, scene;
    ref_rt checkResult; // 1x1, 1 when the frame's comparison passed
    ID3DTexture2D* staging[ReadbackSlots] = {};
    SvpReferenceSkyReadback<ReadbackSlots> readback;

    SvpReferenceSkyCapture() = default;
    SvpReferenceSkyCapture(const SvpReferenceSkyCapture&) = delete;
    SvpReferenceSkyCapture& operator=(const SvpReferenceSkyCapture&) = delete;
    ~SvpReferenceSkyCapture()
    {
        for (auto*& texture : staging) _RELEASE(texture);
    }
};

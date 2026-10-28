#pragma once
#include "../../xrEngine/svp_reference_optics.h"

// Identifies the optic configuration that a packet or a capture belongs to.
struct SvpReferenceStamp
{
    u32 session = 0, opticEpoch = 0, routeEpoch = 0, generation = 0;
    bool operator==(const SvpReferenceStamp& other) const
    {
        return session == other.session && opticEpoch == other.opticEpoch
            && routeEpoch == other.routeEpoch && generation == other.generation;
    }
    bool operator!=(const SvpReferenceStamp& other) const { return !(*this == other); }
};

// The published frame with the renderer's textures and per-draw state.
struct SvpReferencePacket : SvpReferenceOpticsFrame
{
    ref_texture materials[2]; // translucent dirt, opaque dirt
    ref_texture glassMaterials[2]; // spots, surface
    // False while the eye anchor has to come from the ocular and none is drawn
    // this frame. The glass material does not need the anchor.
    bool imageAnchorQualified = true;
    // The lens node being drawn; draw_scope skips every other node.
    const void* ocularVisual = nullptr;
    const Fmatrix* ocularRoot = nullptr;

    SvpReferenceStamp Stamp() const { return {session, opticEpoch, routeEpoch, generation}; }
};

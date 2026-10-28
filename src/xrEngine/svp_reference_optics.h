#pragma once

// Published by the game once per frame for the scope in the player's hands; read by the renderer.
struct SvpReferenceOpticsFrame
{
    enum { ConstantCount = 62, GlassConstantCount = 19 };
    u32 frame=u32(-1),session=0,opticEpoch=0,routeEpoch=0,generation=0;
    // active: the optic is the aimed sight. allowDeferredCapture: its profile permits the Reference image and glass.
    bool enabled=false,active=false,allowDeferredCapture=false;
    char materialNames[2][260]={}; // translucent dirt, opaque dirt
    char glassMaterialNames[2][260]={};
    char sourcePackage[260]={},sourceVariant[64]={};
    float sourceRadius=0.f;
    Fvector sourceSocket={},sourceTangent={},sourceBitangent={};
    Fvector sourceBaselineEye={}; // Aimed eye position in the optics model.
    Fvector ocularBaselineEye={}; // Aimed eye position in ocular right/up/forward metres.
    bool ocularBaselineFromWeapon=false; // The renderer derives ocularBaselineEye from weaponEyeWorld.
    bool classicPupilTracking=false; // Classic with the deadzone lag: the eye follows the tracked pupil.
    bool classicFollowEye=false; // Classic with the follow lag: the current eye, as in Reference.
    Fvector weaponEyeWorld={}; // Aimed eye position carried by the weapon transform, world space.
    Fvector4 glassConstants[GlassConstantCount]={};
    Fvector4 constants[ConstantCount]={};
    float threshold=0,range=0,contrast=0;
    float toneMapInputGamma=0; // 1 for the stock tonemap input, 2.2 for SSS.
};
ENGINE_API bool svp_reference_optics_read(SvpReferenceOpticsFrame&);
ENGINE_API bool svp_reference_optics_publish(const SvpReferenceOpticsFrame&);
ENGINE_API void svp_reference_optics_clear();

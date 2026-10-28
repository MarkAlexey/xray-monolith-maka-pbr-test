#ifndef SCOPE_REFERENCE_COMPOSITE_H
#define SCOPE_REFERENCE_COMPOSITE_H
struct ScopeReferenceCompositeVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 view : TEXCOORD1; // eye minus surface, world space, unnormalised
};
#endif

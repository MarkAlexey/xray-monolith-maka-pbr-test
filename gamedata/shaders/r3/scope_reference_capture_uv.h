// Maps the optical kernel's capture coordinates onto the PiP image.
#ifndef SCOPE_REFERENCE_CAPTURE_UV_H
#define SCOPE_REFERENCE_CAPTURE_UV_H
float4 svp_env;
float4 svp_lens_center,svp_lens_right,svp_lens_up;
float3 eye_position;
static float2 reference_image_uv=.5;
static float2 reference_source_capture_base=.5;
static bool reference_capture_flip=false;
float2 reference_capture_uv(int slot,float2 sampleUV)
{
    float2 kernelBase=slot==4?.5+(reference_source_capture_base-.5)*.8:reference_source_capture_base;
    float2 tc=reference_image_uv+(sampleUV-kernelBase);
    if(reference_capture_flip)tc.y=1-tc.y;
    return tc;
}
#endif

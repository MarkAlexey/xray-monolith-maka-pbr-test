#ifndef SCOPE_REFERENCE_COORDINATES_H
#define SCOPE_REFERENCE_COORDINATES_H
// Lens frame to kernel frame: X is mirrored, and one scale applies to all lengths.
float3 reference_source_vector(float3 value,float3 lensRight,float3 lensUp,float3 lensForward,float scale)
{
    return float3(-dot(value,lensRight),dot(value,lensUp),dot(value,lensForward))*scale;
}
// The eye relative to its aim anchor, mapped onto the kernel's anchor.
float3 reference_source_eye(float3 lensEye,float3 lensRight,float3 lensUp,float3 lensForward,
    float3 lensAnchor,float3 sourceAnchor,float scale)
{
    float3 local=float3(dot(lensEye,lensRight),dot(lensEye,lensUp),dot(lensEye,lensForward))-lensAnchor;
    return sourceAnchor+float3(-local.x,local.y,local.z)*scale;
}
// Scales the eye's lateral offset from the anchor by the zoom gain. Depth is unchanged.
float3 reference_zoom_pupil_eye(float3 sourceEye,float3 sourceAnchor,float gain)
{
    return sourceEye+float3((sourceEye.xy-sourceAnchor.xy)*(gain-1.0),0);
}
#endif

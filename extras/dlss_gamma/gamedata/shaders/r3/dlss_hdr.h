// Reversible curve around the upscaler (r_dlss_hdr_input). The finished frame is
// decoded to linear light and its highlights are expanded for DLSS; the resolve
// compresses them back, so the shader pack's own tone mapping is unchanged.
// Max-channel form keeps the hue; display white (1) maps to 16.
#ifndef DLSS_HDR_H
#define DLSS_HDR_H
#define DLSS_HDR_SLOPE (15.0 / 16.0)

float3 DlssHdrToLinear(float3 c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 DlssHdrToSrgb(float3 c)
{
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float3 DlssHdrExpand(float3 c)
{
    return c / (1.0 - DLSS_HDR_SLOPE * max(c.r, max(c.g, c.b)));
}

float3 DlssHdrCompress(float3 c)
{
    return c / (1.0 + DLSS_HDR_SLOPE * max(c.r, max(c.g, c.b)));
}
#endif

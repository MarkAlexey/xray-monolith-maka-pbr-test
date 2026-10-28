// motion.w of the G-buffer tags lens glass and carries its roughness. The stock
// gloss channel depends on the light setup, so it cannot hold the roughness.
#ifndef SCOPE_GLASS_MATERIAL_PAYLOAD_H
#define SCOPE_GLASS_MATERIAL_PAYLOAD_H
// Tag of plain glass and of glass that shows the optical image in focus.
static const float scope_glass_tag=2.0,scope_glass_focus_tag=3.0;
// Roughness spans this much above its tag, below the next classifier threshold.
static const float scope_glass_payload_span=.49;
// Half-float spacing in [2,4) is 1/512.
static const float scope_glass_payload_steps=512.0;
float scope_glass_encode_roughness(float roughness,bool opticalFocus)
{
 // Round to the stored step; the render-target conversion truncates.
 float payload=floor(scope_glass_payload_span*saturate(roughness)*scope_glass_payload_steps+.5)/scope_glass_payload_steps;
 return (opticalFocus?scope_glass_focus_tag:scope_glass_tag)+payload;
}
float scope_glass_decode_roughness(float tag)
{
 return saturate((tag-(tag>2.5?scope_glass_focus_tag:scope_glass_tag))/scope_glass_payload_span);
}
#endif

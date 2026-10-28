// Rasterises the lens mesh in its UV chart and keeps the clip position for the pixel shader.
#define SCOPE_REFERENCE_GLYPH_LAYER
#define main scope_vertex_main
#include "scope_vertex.vs"
#undef main
v_out main(v_in input)
{
    v_out output=scope_vertex_main(input);
    output.reference_clip=output.hpos;
    output.hpos=float4(input.tc.x*2-1,1-input.tc.y*2,0,1);
    return output;
}

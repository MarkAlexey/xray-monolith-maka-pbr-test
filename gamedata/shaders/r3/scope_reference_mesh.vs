// The ocular mesh with the stock scope vertex input and skinning.
#define main scope_vertex_main
#include "scope_vertex.vs"
#undef main
#include "scope_reference_composite.h"
ScopeReferenceCompositeVertex main(v_in input)
{
    v_out vertex = scope_vertex_main(input);
    ScopeReferenceCompositeVertex output;
    output.position = vertex.hpos;
    output.uv = vertex.tc0;
    output.view = eye_position.xyz - vertex.w_P;
    return output;
}

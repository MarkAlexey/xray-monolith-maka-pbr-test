// x: 1 while particles are replayed into the Reference capture, which takes
// no negative colour. Otherwise the colour passes through.
float4 scope_reference_particle_transfer;
float4 scope_reference_particle(float4 value)
{
    if (scope_reference_particle_transfer.x > 0.5)
        value.rgb = max(value.rgb, 0);
    return value;
}

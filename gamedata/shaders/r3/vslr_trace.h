// Reflection tracing against a camera-centred radial distance cubemap.
// Distance is in metres; zero denotes an empty texel. Colour is linear HDR, zero in empty texels;
// coverage is one where the scene was captured. Colour and coverage carry mip chains. The previous
// capture's colour and coverage stay bound while a new capture fades in.
#ifndef SCOPE_VSLR_TRACE_H
#define SCOPE_VSLR_TRACE_H
TextureCube<float> vslr_distance;
TextureCube<float3> vslr_colour;
TextureCube<float> vslr_coverage;
TextureCube<float3> vslr_colour_previous;
TextureCube<float> vslr_coverage_previous;
SamplerState vslr_point;
SamplerState vslr_linear;
// How far a traced hit may lie from the captured surface and still count, metres.
static const float vslr_hit_tolerance = 0.1f;

struct VslrHit
{
    float3 direction;
    float confidence;
};

VslrHit vslr_trace(float3 origin, float3 direction, float radius, float tolerance)
{
    VslrHit result = (VslrHit)0;
    // origin is relative to the frozen capture centre, not the current camera.
    float b = dot(origin, direction);
    float discriminant = b * b + radius * radius - dot(origin, origin);
    if (discriminant <= 0.0f)
        return result;
    float root = sqrt(discriminant);
    float start = max(0.0f, -b - root);
    float end = -b + root;
    if (end <= 0.0f)
        return result;

    float previousT = start;
    float3 startPosition = origin + start * direction;
    float startRadius = length(startPosition);
    float startDepth = vslr_distance.SampleLevel(vslr_point,
        startRadius > 0.0001f ? startPosition : direction, 0);
    float previousDelta = startRadius - startDepth;
    bool previousValid = startDepth > 0.0f && startDepth <= radius;
    [loop]
    for (uint step = 1; step <= 32; ++step)
    {
        float fraction = float(step) / 32.0f;
        float t = start + (end - start) * fraction * fraction;
        float3 samplePosition = origin + t * direction;
        float radial = length(samplePosition);
        if (radial < 0.0001f)
            continue;
        float distance = vslr_distance.SampleLevel(vslr_point, samplePosition, 0);
        bool valid = distance > 0.0f && distance <= radius;
        float delta = radial - distance;
        if (valid && previousValid && previousDelta <= 0.0f && delta >= 0.0f)
        {
            float lo = previousT;
            float hi = t;
            [unroll]
            for (uint refinement = 0; refinement < 5; ++refinement)
            {
                float mid = (lo + hi) * 0.5f;
                float3 candidate = origin + mid * direction;
                float surface = vslr_distance.SampleLevel(vslr_point, candidate, 0);
                // A discontinuity into empty space is not a valid intersection.
                if (surface <= 0.0f || surface > radius)
                    return result;
                if (length(candidate) > surface)
                    hi = mid;
                else
                    lo = mid;
            }
            float3 hit = origin + (lo + hi) * 0.5f * direction;
            float surface = vslr_distance.SampleLevel(vslr_point, hit, 0);
            float error = abs(length(hit) - surface);
            result.direction = normalize(hit);
            result.confidence = saturate(1.0f - error / max(tolerance, 0.0001f));
            return result;
        }
        previousValid = valid;
        previousDelta = delta;
        previousT = t;
    }
    return result;
}

// A reflection lobe from the capture at a mip: the captured scene, with background radiance behind
// the share of the lobe where nothing was captured.
float3 vslr_lobe_of(TextureCube<float3> colour, TextureCube<float> coverage, float3 direction, float lod,
    float3 background)
{
    return colour.SampleLevel(vslr_linear, direction, lod) +
        (1.0f - coverage.SampleLevel(vslr_linear, direction, lod)) * background;
}

float3 vslr_lobe(float3 direction, float lod, float3 background)
{
    return vslr_lobe_of(vslr_colour, vslr_coverage, direction, lod, background);
}

// The lobe while the published capture fades in over the previous one; fade is its weight.
float3 vslr_lobe_faded(float3 direction, float lod, float3 background, float fade)
{
    float3 current = vslr_lobe(direction, lod, background);
    [branch] if (fade >= 1.0f)
        return current;
    return lerp(vslr_lobe_of(vslr_colour_previous, vslr_coverage_previous, direction, lod, background), current, fade);
}
#endif

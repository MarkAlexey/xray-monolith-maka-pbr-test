#ifndef SCOPE_REFERENCE_SCENE_TONE_H
#define SCOPE_REFERENCE_SCENE_TONE_H

// The Reference capture's raw radiance stores pow(c, gamma), where c is the colour the installed
// combine passes to the installed tonemap() and gamma is the packet's toneMapInputGamma. Display
// values convert to and from it through that installed tonemap() under the main view's exposure,
// whatever its curve. Include after common.h, which declares tonemap().
//
// Contract with the installed tonemap(low, high, rgb, scale): low.rgb is the displayed colour, and each
// channel does not decrease as the same channel of rgb grows. Channels may be coupled, as in hue-preserving
// curves (Atmospherics 2.69 RC7.6's PBR Neutral).

// The main view's exposure texture under its own name: the capture renders in the PiP target,
// whose s_tonemap is the PiP view's own.
Texture2D scope_reference_main_meter;

// The floor keeps the exposure positive.
float scope_reference_meter()
{
    return max(scope_reference_main_meter.Load(int3(0, 0, 0)).x, 1e-4);
}

// Display value of a tone map input under the installed tone map.
float3 scope_reference_tone_display(float3 toneInput, float meter)
{
    float4 low, high;
    tonemap(low, high, max(toneInput, 0), meter);
    return low.rgb;
}

// Display value of a raw radiance.
float3 scope_reference_scene_tone(float3 raw, float gamma, float meter)
{
    return scope_reference_tone_display(pow(max(raw, 0), 1.0 / gamma), meter);
}

// Display value of each channel of toneInput tone mapped as a neutral grey, so channels do not interact.
float3 scope_reference_tone_display_grey(float3 toneInput, float meter)
{
    return float3(scope_reference_tone_display(toneInput.xxx, meter).x,
        scope_reference_tone_display(toneInput.yyy, meter).y, scope_reference_tone_display(toneInput.zzz, meter).z);
}

// Whether the installed curve couples channels: a bright, saturated colour then differs from its channels
// tone mapped as greys.
bool scope_reference_tone_coupled(float meter)
{
    const float3 probe = float3(64, 1, 1.0 / 64) / meter;
    return any(abs(scope_reference_tone_display(probe, meter) - scope_reference_tone_display_grey(probe, meter)) > 1e-4);
}

// Raw radiance whose display value is display. The installed curve has no closed-form inverse here, so
// the tone map input is found per channel by bisection on its base-2 logarithm, between 2^-24 and 2^12
// (display white of any exposure the meter floor allows); values outside clamp to the ends. A coupled curve
// is searched with grey probes and the estimate then refined against the whole colour by damped
// Gauss-Newton steps on the logarithm.
float3 scope_reference_scene_tone_inverse(float3 display, float gamma, float meter)
{
    const bool coupled = scope_reference_tone_coupled(meter);
    float3 low = -24, high = 12;
    [loop] for (int step = 0; step < 24; ++step)
    {
        const float3 middle = 0.5 * (low + high);
        const float3 shown = coupled ? scope_reference_tone_display_grey(exp2(middle), meter)
            : scope_reference_tone_display(exp2(middle), meter);
        const bool3 below = shown < display;
        low = below ? middle : low;
        high = below ? high : middle;
    }
    float3 logInput = 0.5 * (low + high);
    if (coupled)
    {
        float3 miss = scope_reference_tone_display(exp2(logInput), meter) - display;
        float error = dot(miss, miss), damping = 1e-3;
        [loop] for (int iteration = 0; iteration < 16 && any(abs(miss) > 2e-4); ++iteration)
        {
            const float h = 1.0 / 256;
            float3x3 jacobian;
            [unroll] for (int k = 0; k < 3; ++k)
            {
                float3 probe = logInput;
                probe[k] += h;
                jacobian[k] = (scope_reference_tone_display(exp2(probe), meter) - display - miss) / h; // row k: d/dlog[k]
            }
            // jacobian holds the transposed Jacobian: J^T J and J^T miss.
            const float3x3 normal = mul(jacobian, transpose(jacobian));
            const float3 gradient = mul(jacobian, miss);
            const float scale = max((normal[0][0] + normal[1][1] + normal[2][2]) / 3, 1e-8);
            bool improved = false;
            [loop] for (int attempt = 0; attempt < 5 && !improved; ++attempt)
            {
                float3x3 damped = normal;
                damped[0][0] += damping * scale;
                damped[1][1] += damping * scale;
                damped[2][2] += damping * scale;
                const float3 c0 = cross(damped[1], damped[2]), c1 = cross(damped[2], damped[0]), c2 = cross(damped[0], damped[1]);
                const float determinant = dot(damped[0], c0);
                const float3 stepLog = -clamp(float3(dot(c0, gradient), dot(c1, gradient), dot(c2, gradient)) / determinant, -4, 4);
                const float3 candidate = clamp(logInput + stepLog, -24, 12);
                const float3 candidateMiss = scope_reference_tone_display(exp2(candidate), meter) - display;
                if (dot(candidateMiss, candidateMiss) < error)
                {
                    logInput = candidate;
                    miss = candidateMiss;
                    error = dot(miss, miss);
                    damping = max(damping * 0.3, 1e-6);
                    improved = true;
                }
                else
                    damping *= 10;
            }
            if (!improved)
                break;
        }
    }
    const float3 toneInput = display <= scope_reference_tone_display(exp2(-24), meter) ? 0 : exp2(logInput);
    return pow(toneInput, gamma);
}

#endif

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dlss
{
enum class Quality : unsigned { Off, DLAA, Quality, Balanced, Performance, UltraPerformance };
// NGX Super Resolution preset values. F is deprecated in runtime 310.9.1;
// reserved presets are deliberately not exposed as working choices.
enum class Preset : unsigned { Default = 0, J = 10, K = 11, L = 12, M = 13 };
inline bool ValidPreset(Preset preset)
{
    return preset == Preset::Default || preset == Preset::J || preset == Preset::K ||
        preset == Preset::L || preset == Preset::M;
}

struct Size
{
    unsigned width = 0, height = 0;
    bool operator==(const Size& other) const { return width == other.width && height == other.height; }
    bool operator!=(const Size& other) const { return !(*this == other); }
    bool Valid() const { return width != 0 && height != 0; }
};

struct Offset { float x = 0, y = 0; };

inline float Halton(unsigned index, unsigned base)
{
    float value = 0, fraction = 1;
    while (index)
    {
        fraction /= float(base);
        value += fraction * float(index % base);
        index /= base;
    }
    return value;
}

// Pixel-space raster offset: +X right, +Y down. Use the SAME sample for the
// geometry shader constant and NGX evaluation, advancing only on main-view frames.
inline Offset Jitter(unsigned sample, Size render, Size display)
{
    if (!render.Valid() || !display.Valid()) return {};
    const double ratio = double(display.width) / render.width;
    const unsigned phases = unsigned(std::clamp(std::ceil(8.0 * ratio * ratio), 8.0, 128.0));
    const unsigned index = sample % phases + 1;
    return {Halton(index, 2) - 0.5f, Halton(index, 3) - 0.5f};
}

inline Offset ClipJitter(Offset pixels, Size render)
{
    if (!render.Valid()) return {};
    return {2.0f * pixels.x / render.width, -2.0f * pixels.y / render.height};
}

// SSS 23 writes currentUV - previousUV, excluding jitter. NGX wants the
// current-to-previous displacement in pixels; negate BOTH axes, without a copy.
inline Offset MotionScale(Size render) { return {-float(render.width), -float(render.height)}; }

// Own one history per camera. Never feed a scope camera into the main history.
class History
{
public:
    bool Begin(std::uint64_t frame, bool cameraCut)
    {
        m_reset = !m_valid || cameraCut || frame <= m_lastFrame;
        return m_reset;
    }
    void Commit(std::uint64_t frame) { m_lastFrame = frame; m_valid = true; m_reset = false; }
    void Invalidate() { m_valid = false; m_reset = true; }
    bool Reset() const { return m_reset; }
private:
    std::uint64_t m_lastFrame = 0;
    bool m_valid = false, m_reset = true;
};
}

#pragma once

#include <array>
#include <d3d11.h>
#include <wrl/client.h>

namespace scope_vslr
{
// GPU timestamps of the capture and debug draws, read back a few frames late so the CPU never waits.
class Timing
{
public:
    enum Mark { Start, Captured, Drawn, MarkCount };

    struct Totals
    {
        unsigned frames = 0;
        unsigned faces = 0;
        double capture_gpu_ms = 0;
        double draw_gpu_ms = 0;
        double traverse_cpu_ms = 0;
        double submit_cpu_ms = 0;
    };

    bool create(ID3D11Device* device);
    void begin(ID3D11DeviceContext* context);
    void mark(ID3D11DeviceContext* context, Mark mark);
    // faces and CPU times belong to the frame begun last; its GPU times arrive later.
    void end(ID3D11DeviceContext* context, unsigned faces, double traverse_ms, double submit_ms);
    // Moves completed frames into the totals and returns them, leaving the totals empty.
    Totals take();

private:
    using Query = Microsoft::WRL::ComPtr<ID3D11Query>;
    struct Frame
    {
        Query disjoint;
        std::array<Query, MarkCount> marks;
        unsigned faces = 0;
        double traverse_ms = 0;
        double submit_ms = 0;
        bool pending = false;
    };
    void collect(ID3D11DeviceContext* context);

    std::array<Frame, 4> frames_;
    unsigned current_ = 0;
    bool open_ = false;
    Totals totals_;
};

// GPU time of one pass outside the capture frame (a consumer's draw), read back the same way.
class Span
{
public:
    struct Totals
    {
        unsigned passes = 0;
        double gpu_ms = 0;
    };

    bool create(ID3D11Device* device);
    void begin(ID3D11DeviceContext* context);
    void end(ID3D11DeviceContext* context);
    Totals take(ID3D11DeviceContext* context);

private:
    using Query = Microsoft::WRL::ComPtr<ID3D11Query>;
    struct Slot
    {
        Query disjoint, start, finish;
        bool pending = false;
    };
    void collect(ID3D11DeviceContext* context);

    std::array<Slot, 4> slots_;
    unsigned current_ = 0;
    bool open_ = false;
    Totals totals_;
};
}

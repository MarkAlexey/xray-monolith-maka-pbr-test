#include "scope_vslr_timing.h"

namespace scope_vslr
{
bool Timing::create(ID3D11Device* device)
{
    D3D11_QUERY_DESC disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC timestamp{D3D11_QUERY_TIMESTAMP, 0};
    for (Frame& frame : frames_)
    {
        if (FAILED(device->CreateQuery(&disjoint, frame.disjoint.GetAddressOf())))
            return false;
        for (Query& query : frame.marks)
            if (FAILED(device->CreateQuery(&timestamp, query.GetAddressOf())))
                return false;
    }
    return true;
}

void Timing::collect(ID3D11DeviceContext* context)
{
    for (Frame& frame : frames_)
    {
        if (!frame.pending)
            continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock;
        if (context->GetData(frame.disjoint.Get(), &clock, sizeof(clock), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        std::array<UINT64, MarkCount> ticks;
        bool complete = true;
        for (unsigned i = 0; i < MarkCount; ++i)
            complete = complete &&
                context->GetData(frame.marks[i].Get(), &ticks[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        if (!complete)
            continue;
        frame.pending = false;
        if (clock.Disjoint || !clock.Frequency)
            continue;
        const double ms = 1000.0 / double(clock.Frequency);
        totals_.frames += 1;
        totals_.faces += frame.faces;
        totals_.capture_gpu_ms += double(ticks[Captured] - ticks[Start]) * ms;
        totals_.draw_gpu_ms += double(ticks[Drawn] - ticks[Captured]) * ms;
        totals_.traverse_cpu_ms += frame.traverse_ms;
        totals_.submit_cpu_ms += frame.submit_ms;
    }
}

void Timing::begin(ID3D11DeviceContext* context)
{
    collect(context);
    // A slot still in flight after a full ring is skipped rather than reissued.
    open_ = !frames_[current_].pending;
    if (!open_)
        return;
    context->Begin(frames_[current_].disjoint.Get());
    context->End(frames_[current_].marks[Start].Get());
}

void Timing::mark(ID3D11DeviceContext* context, Mark mark)
{
    if (open_)
        context->End(frames_[current_].marks[mark].Get());
}

void Timing::end(ID3D11DeviceContext* context, unsigned faces, double traverse_ms, double submit_ms)
{
    if (!open_)
        return;
    Frame& frame = frames_[current_];
    context->End(frame.disjoint.Get());
    frame.faces = faces;
    frame.traverse_ms = traverse_ms;
    frame.submit_ms = submit_ms;
    frame.pending = true;
    current_ = (current_ + 1) % frames_.size();
    open_ = false;
}

Timing::Totals Timing::take()
{
    const Totals result = totals_;
    totals_ = {};
    return result;
}

bool Span::create(ID3D11Device* device)
{
    D3D11_QUERY_DESC disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC timestamp{D3D11_QUERY_TIMESTAMP, 0};
    for (Slot& slot : slots_)
        if (FAILED(device->CreateQuery(&disjoint, slot.disjoint.GetAddressOf())) ||
            FAILED(device->CreateQuery(&timestamp, slot.start.GetAddressOf())) ||
            FAILED(device->CreateQuery(&timestamp, slot.finish.GetAddressOf())))
            return false;
    return true;
}

void Span::collect(ID3D11DeviceContext* context)
{
    for (Slot& slot : slots_)
    {
        if (!slot.pending)
            continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock;
        UINT64 start, finish;
        if (context->GetData(slot.disjoint.Get(), &clock, sizeof(clock), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(slot.start.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(slot.finish.Get(), &finish, sizeof(finish), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        slot.pending = false;
        if (clock.Disjoint || !clock.Frequency)
            continue;
        totals_.passes += 1;
        totals_.gpu_ms += double(finish - start) * 1000.0 / double(clock.Frequency);
    }
}

void Span::begin(ID3D11DeviceContext* context)
{
    collect(context);
    // A slot still in flight after a full ring is skipped rather than reissued.
    open_ = !slots_[current_].pending;
    if (!open_)
        return;
    context->Begin(slots_[current_].disjoint.Get());
    context->End(slots_[current_].start.Get());
}

void Span::end(ID3D11DeviceContext* context)
{
    if (!open_)
        return;
    Slot& slot = slots_[current_];
    context->End(slot.finish.Get());
    context->End(slot.disjoint.Get());
    slot.pending = true;
    current_ = (current_ + 1) % slots_.size();
    open_ = false;
}

Span::Totals Span::take(ID3D11DeviceContext* context)
{
    collect(context);
    const Totals result = totals_;
    totals_ = {};
    return result;
}
}

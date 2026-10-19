#pragma once
#include <memory>

class CRender;
class SunShadows
{
    struct State;
    std::unique_ptr<State> state;
    void RecordStatic(u32 cascade);
public:
    SunShadows();
    ~SunShadows();
    bool Prepare(CRender& renderer);
    void CapturePending(bool details_prepared = false);
    void RecordStatic();
    void Record();
    bool Prepared() const;
    bool Bind(u32 cascade);
    bool GrassRecorded(u32 cascade) const;
    void EndFrame();
    void Clear();
};

class RainShadows
{
    struct State;
    std::unique_ptr<State> state;
public:
    RainShadows();
    ~RainShadows();
    bool Prepare(CRender& renderer);
    bool Prepared() const;
    bool Bind();
    void EndFrame();
    void Clear();
};

#pragma once
// DX12 executable only. No graphics types in the simulation-thread interface.
namespace fg
{
void BeginSimulation();
void EndSimulation();
void BeginRender();
void EndPresent();
void CaptureScene();
void CaptureHudless();
void Reset();
// r__framelimit integration. With Streamline, Reflex applies the cap and the engine
// limiter must not wait as well. While frames are generated the cap is interpreted
// as displayed frames, so rendered frames are limited to half of it.
bool ReflexLimitsFrames();
float FrameLimitScale();
}

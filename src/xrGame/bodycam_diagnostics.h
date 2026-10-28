#pragma once
// Bodycam log diagnostics. Off unless a level is set on the console.
namespace Bodycam
{
// Level 1: one line per decision or event. Level 2: per-frame traces.
bool DiagnosticsEnabled(int level);
}

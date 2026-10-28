#pragma once

// Debug: r__svp_frame_burst N saves the next N presented frames, scaled down by
// r__svp_frame_burst_scale (default 4; 1 is full size), as raw RGBA8 to appdata/logs/burst/
// ([SVP-BURST] logs each frame's number and time), to inspect flicker or jitter that lasts single
// frames. Called once per frame just before Present.
void svp_frame_burst_capture();

#pragma once

// pip and 3DSS shader warm-up (r4_shader_warmup.cpp). The pixel shaders of the PiP lens glue, the Reference image
// and the 3DSS sight materials take seconds each to compile and would otherwise compile on the render thread the
// first frame, aim or weapon raise that draws them. While the level loads, worker threads compile them into memory;
// shader_compile takes that bytecode on a cache miss and caches it as it would its own.

// level load: reads the sources and their includes, then starts the workers, which touch nothing but their jobs
void shader_warmup_start();
// shader_compile on a cache miss: the bytecode compiled ahead for this cache file, source and flags, waiting for
// its worker if it is still compiling; null when the warm-up has none (the caller compiles)
ID3DBlob* shader_warmup_take(LPCSTR cache_file, u32 source_crc, LPCSTR entry, DWORD flags);
// device destroy: waits for the workers and drops what was not taken
void shader_warmup_stop();

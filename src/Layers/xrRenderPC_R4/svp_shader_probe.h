#pragma once

// Compiles a pixel shader without registering it, so that a failure only logs: the resource
// manager ends the game on a compile error. Wrappers of installed shaders are probed with it.
bool svp_pixel_shader_compiles(LPCSTR name);

#include "stdafx.h"
#include "svp_shader_probe.h"
#include "../xrRender/ResourceManager.h"

bool svp_pixel_shader_compiles(LPCSTR name)
{
    string_path path;
    strconcat(sizeof(path), path, ::Render->getShaderPath(), name, ".ps");
    FS.update_path(path, "$game_shaders$", path);
    IReader* file = FS.r_open(path);
    if (!file) return false;
    SPS probe;
    probe.ps = nullptr;
    void* result = &probe;
    // shader_compile only reads the profile's leading "p" and picks the real one itself.
    const HRESULT compiled = ::Render->shader_compile(name, static_cast<DWORD const*>(file->pointer()),
        file->length(), "main", "ps_2_0", D3D10_SHADER_PACK_MATRIX_ROW_MAJOR, result);
    FS.r_close(file);
    return SUCCEEDED(compiled);
}

#pragma once

// Persistent, release-visible renderer failure. Pass nullptr to clear it after
// recovery or after the user applies a setting that disables the failed feature.
// The caller also logs the diagnostic; this does not depend on log callbacks.
ENGINE_API void SetRendererError(const char* summary, const char* detail = nullptr,
    const char* recovery = nullptr);

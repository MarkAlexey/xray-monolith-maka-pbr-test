AMD FidelityFX API headers (FSR 3.1.4 frame generation, DirectX 12)
===================================================================
From AMD's FidelityFX SDK v1.1.4 (tag v1.1.4, commit c6efa6b), ffx-api/include/ffx_api:
ffx_api.h, ffx_api_types.h, ffx_framegeneration.h, dx12/ffx_api_dx12.h. MIT license (LICENSE.txt).

Used by src/Layers/xrRenderPC_R4/FrameGen (r4_framegen 1). The runtime is AMD's signed
amd_fidelityfx_dx12.dll from the same release (PrebuiltSignedDLL/), loaded at run time only
when frame generation is switched on; the exe does not link it. The release workflow
downloads it (sha256 12a5081257ec95b0b53ad51b4a87fb3c03f97fe0bbb59f9496968f8d50ef93a6) and
ships it in the separate *_framegen.zip.

The headers are unmodified. Keep them and the DLL at the same SDK version.

#pragma once

void uber_deffer(CBlender_Compile& C, bool hq, LPCSTR _vspec, LPCSTR _pspec, BOOL _aref, LPCSTR _detail_replace = 0,
                 bool DO_NOT_FINISH = false, bool DO_NOT_WRITE = false);
void uber_shadow(CBlender_Compile& C, LPCSTR _vspec);

#if RENDER == R_R4
// Monolith: IX-Ray VSLR capture element (E[SE_R2_REFLECTIONS]). Builds the same vertex shader as
// the normal LQ element with the capture pixel shader "deffer_vslr_*" (r3\vslr_capture.h) and marks
// the element (flags.bReflect). Does nothing and returns false with r4_vslr_enable 0.
bool uber_reflect(CBlender_Compile& C, LPCSTR _vspec, BOOL _aref);
#endif

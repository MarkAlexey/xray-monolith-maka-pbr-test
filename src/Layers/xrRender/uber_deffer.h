#pragma once

// _arefValue is the alpha ref this pass publishes to the m_AlphaRef shader constant.
// 200 is the historical def_aref literal, so it is the safe default for world geometry.
void uber_deffer(CBlender_Compile& C, bool hq, LPCSTR _vspec, LPCSTR _pspec, BOOL _aref, LPCSTR _detail_replace = 0,
                 bool DO_NOT_FINISH = false, bool DO_NOT_WRITE = false, u32 _arefValue = 200);
void uber_shadow(CBlender_Compile& C, LPCSTR _vspec);

#if RENDER == R_R4
// HUD blood experiment (tools/hud_blood): binds the blood atlas on first-person models. The
// textures are shader bus texture lanes the hud_blood script fills; a shader that does not
// declare these names ignores them (r_dx10Texture skips names the pass does not use), and an
// empty lane reads 0, which the blood shader treats as no blood.
inline void hud_blood_bind(CBlender_Compile& C)
{
	C.r_dx10Texture("s_hud_blood_mask", "$user$bus_hud_blood_mask");
	C.r_dx10Texture("s_hud_blood_na", "$user$bus_hud_blood_na");
	C.r_dx10Texture("s_hud_blood_nb", "$user$bus_hud_blood_nb");
}

// Monolith: IX-Ray VSLR capture element (E[SE_R2_REFLECTIONS]). Builds the same vertex shader as
// the normal LQ element with the capture pixel shader "deffer_vslr_*" (r3\vslr_capture.h) and marks
// the element (flags.bReflect). Does nothing and returns false with r4_vslr_enable 0.
bool uber_reflect(CBlender_Compile& C, LPCSTR _vspec, BOOL _aref);
#endif

#pragma once

// Monolith: reactive mask for DLSS / FSR 3 (r4_rendertarget_phase_reactive.cpp).
// Elements: 0 = mask from the scene before / after rain and forward rendering,
//           1 = debug overlay (r__upscaler_reactive_debug). Shaders: r3\upscaler_reactive*.ps
class CBlender_upscaler_reactive : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: upscaler reactive mask"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_upscaler_reactive() { description.CLS = 0; }
	virtual ~CBlender_upscaler_reactive() {}
};

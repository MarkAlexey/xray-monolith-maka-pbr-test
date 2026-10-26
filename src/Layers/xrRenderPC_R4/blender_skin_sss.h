#pragma once

// Monolith: skin subsurface scattering blur (r4_rendertarget_phase_skin_sss.cpp).
// Elements: 0 = horizontal (light accumulator -> rt_skin_sss_tmp),
//           1 = vertical   (rt_skin_sss_tmp    -> rt_skin_sss). Shaders: r3\skin_sss_blur_*.ps
class CBlender_skin_sss : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: skin subsurface scattering blur"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_skin_sss() { description.CLS = 0; }
	virtual ~CBlender_skin_sss() {}
};

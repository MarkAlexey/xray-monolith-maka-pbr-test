#pragma once

// Monolith: IX-Ray style single-pass cascaded sun shadows (see r4_rendertarget_accum_csm.cpp).
// Element 0: accum_sun_csm.vs / accum_sun_csm.ps, fullscreen, lights every pixel covered by
// the shadow-map array cascades in one draw.
class CBlender_accum_csm : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: IX-Ray single-pass cascaded sun"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_accum_csm();
	virtual ~CBlender_accum_csm();
};

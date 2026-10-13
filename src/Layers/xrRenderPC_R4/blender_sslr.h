#pragma once

// Monolith: IX-Ray SSLR (screen-space local reflections), ported from IX-Ray 1.6
// (src/Layers/xrRenderPC_R4/blender_sslr.*, LVutner / IX-Ray team).
// Elements: 0 = trace, 1 = spatial resolve, 2 = temporal. Shaders: r3\sslr_*.cs
class CBlender_sslr : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: IX-Ray screen space local reflections"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_sslr();
	virtual ~CBlender_sslr();
};

// Monolith: IX-Ray VSLR (offscreen reflections) panorama pass, see r4_rendertarget_phase_vslr.cpp.
// Element 0: the captured cubemaps -> octahedral panorama (r3\vslr_panorama.cs)
class CBlender_vslr : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: IX-Ray offscreen reflections panorama"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_vslr() { description.CLS = 0; }
	virtual ~CBlender_vslr() {}
};

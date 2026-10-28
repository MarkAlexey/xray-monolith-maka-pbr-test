#pragma once

namespace svp
{
struct TargetSettings
{
	bool upscale;
	bool flatWindow;
	bool adaptiveGrow;
	float adaptiveResolution;
	float supersample;
	float renderScale;
};

struct TargetFrame
{
	unsigned baseHeight;
	unsigned displayHeight;
	float disc;
	float panelAspect;
};

struct TargetSize
{
	unsigned width;
	unsigned height;
};

inline unsigned RenderExtent(const TargetSettings& settings, const TargetFrame& frame)
{
	if (!settings.upscale)
	{
		unsigned base = frame.baseHeight;
		if (settings.flatWindow && frame.disc > 1.0f)
		{
			unsigned want = unsigned(frame.disc) & ~1u;
			const unsigned cap = frame.displayHeight & ~1u;
			if (want > cap) want = cap;
			if (want > base) base = want;
		}
		else if (settings.adaptiveResolution > 0.f)
		{
			if (frame.disc > 1.0f)
			{
				unsigned want = unsigned(frame.disc * settings.adaptiveResolution) & ~1u;
				if (want < 256) want = 256;
				if (settings.adaptiveGrow)
				{
					const unsigned cap = frame.displayHeight & ~1u;
					base = (want > cap) ? cap : want;
				}
				else if (want < base)
					base = want;
			}
		}
		float ss = settings.supersample;
		if (ss < 1.0f) ss = 1.0f;
		if (ss > 2.0f) ss = 2.0f;
		if (base == frame.baseHeight && ss <= 1.0f)
			return frame.baseHeight;
		unsigned e = unsigned(base * ss) & ~1u;
		return (e < 2) ? 2 : e;
	}
	float eff = settings.renderScale;
	if (eff < 0.5f) eff = 0.5f;
	if (eff > 1.0f) eff = 1.0f;
	if (eff >= 1.0f)
		return frame.baseHeight;
	unsigned e = unsigned(frame.baseHeight * eff) & ~1u;
	if (e < 2) e = 2;
	return e;
}

inline TargetSize Size(const TargetSettings& settings, const TargetFrame& frame)
{
	// Round up to avoid reallocating the chain for small changes in lens size.
	unsigned side = (RenderExtent(settings, frame) + 31u) & ~31u;
	if (side < 64) side = 64;
	TargetSize result{side, side};
	if (settings.flatWindow && frame.panelAspect > 1.01f)
	{
		result.height = (unsigned(side / frame.panelAspect) + 31u) & ~31u;
		if (result.height < 64) result.height = 64;
	}
	return result;
}
}

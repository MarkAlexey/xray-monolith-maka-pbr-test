#pragma once

// surface_get returns an owning reference and waits for texture loading.
// CopyResource retains neither argument.
inline void CopyTextureSurface(CTexture* destination, CTexture* source)
{
	ID3DBaseTexture* dst = destination->surface_get();
	ID3DBaseTexture* src = source->surface_get();
	HW.pContext->CopyResource(dst, src);
	_RELEASE(src);
	_RELEASE(dst);
}

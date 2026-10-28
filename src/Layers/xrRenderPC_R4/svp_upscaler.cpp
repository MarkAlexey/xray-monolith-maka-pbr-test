#include "stdafx.h"

#if defined(USE_DX11)
// Integration point for the second viewport upscaler. Until an evaluator is
// installed, preserve the existing same-resolution copy.
void CRenderTarget::EvalSVP_DLSS(const SvpDlssInputs&)
{
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);
}

#endif

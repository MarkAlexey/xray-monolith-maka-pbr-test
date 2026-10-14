#include "stdafx.h"
#include "dx10State.h"

//#include "dx10RSManager.h"
#include "dx10StateCache.h"

dx10State::dx10State() :
	m_pRasterizerState(0),
	m_pDepthStencilState(0),
	m_pBlendState(0),
	m_uiStencilRef(UINT(-1)),
	m_uiAlphaRef(0)
{
}

dx10State::~dx10State()
{
	//	m_pRasterizerState is a weak link
	//	m_pDepthStencilState is a weak link
	//	m_pBlendState is a weak link
}

dx10State* dx10State::Create(SimulatorStates& state_code)
{
	dx10State* pState = xr_new<dx10State>();

	state_code.UpdateState(*pState);

	pState->m_pRasterizerState = RSManager.GetState(state_code);
	pState->m_pDepthStencilState = DSSManager.GetState(state_code);
	pState->m_pBlendState = BSManager.GetState(state_code);
#ifdef USE_DX11
    // Compile once from the actual immutable API states, rather than scanning
    // SimulatorStates for every visible mesh on every frame.
    if (pState->m_pDepthStencilState && pState->m_pBlendState)
    {
        D3D11_DEPTH_STENCIL_DESC depth{};
        D3D11_BLEND_DESC blend{};
        pState->m_pDepthStencilState->GetDesc(&depth);
        pState->m_pBlendState->GetDesc(&blend);
        bool safe = !blend.AlphaToCoverageEnable && depth.DepthEnable && depth.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL &&
            (depth.DepthFunc == D3D11_COMPARISON_LESS || depth.DepthFunc == D3D11_COMPARISON_LESS_EQUAL);
        for (const auto& target : blend.RenderTarget) safe = safe && !target.BlendEnable;
        if (depth.StencilEnable)
        {
            const auto safe_face = [](const D3D11_DEPTH_STENCILOP_DESC& face)
            {
                const auto safe_op = [](D3D11_STENCIL_OP op) { return op == D3D11_STENCIL_OP_KEEP || op == D3D11_STENCIL_OP_REPLACE; };
                return safe_op(face.StencilFailOp) && safe_op(face.StencilDepthFailOp) && safe_op(face.StencilPassOp);
            };
            safe = safe && safe_face(depth.FrontFace) && safe_face(depth.BackFace);
        }
        pState->m_static_packet_safe = safe;
    }
#endif
	//ID3DxxDevice::CreateSamplerState

	//	Create samplers here
	{
		InitSamplers(pState->m_VSSamplers, state_code, CTexture::rstVertex);
		InitSamplers(pState->m_PSSamplers, state_code, CTexture::rstPixel);
		InitSamplers(pState->m_GSSamplers, state_code, CTexture::rstGeometry);
#ifdef USE_DX11
		InitSamplers(pState->m_HSSamplers, state_code, CTexture::rstHull);
		InitSamplers(pState->m_DSSamplers, state_code, CTexture::rstDomain);
		InitSamplers(pState->m_CSSamplers, state_code, CTexture::rstCompute);
#endif
	}


	return pState;
}

HRESULT dx10State::Apply()
{
	VERIFY(m_pRasterizerState);
	StateManager.SetRasterizerState(m_pRasterizerState);
	VERIFY(m_pDepthStencilState);
	StateManager.SetDepthStencilState(m_pDepthStencilState);
	if (m_uiStencilRef != -1)
		StateManager.SetStencilRef(m_uiStencilRef);
	VERIFY(m_pBlendState);
	StateManager.SetBlendState(m_pBlendState);
	StateManager.SetAlphaRef(m_uiAlphaRef);

	SSManager.GSApplySamplers(m_GSSamplers);
	SSManager.VSApplySamplers(m_VSSamplers);
	SSManager.PSApplySamplers(m_PSSamplers);
#ifdef USE_DX11
	SSManager.HSApplySamplers(m_HSSamplers);
	SSManager.DSApplySamplers(m_DSSamplers);
	SSManager.CSApplySamplers(m_CSSamplers);
#endif

	//	static const FLOAT BlendFactor[4] = {0.000f, 0.000f, 0.000f, 0.000f};
	//	static const UINT SampleMask = 0xffffffff;

	//	VERIFY(m_pRasterizerState);
	//	HW.pDevice->RSSetState(m_pRasterizerState);
	//	VERIFY(m_pDepthStencilState);
	//	HW.pDevice->OMSetDepthStencilState(m_pDepthStencilState, m_uiStencilRef);
	//	VERIFY(m_pBlendState);
	//	HW.pDevice->OMSetBlendState(m_pBlendState, BlendFactor, SampleMask);

	return S_OK;
}

void dx10State::Release()
{
	dx10State* pState = this;
	xr_delete<dx10State>(pState);
}

void dx10State::InitSamplers(tSamplerHArray& SamplerArray, SimulatorStates& state_code, int iBaseSamplerIndex)
{
	D3D_SAMPLER_DESC descArray[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
	bool SamplerUsed[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];

	for (int i = 0; i < D3D_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
	{
		SamplerUsed[i] = false;
		dx10StateUtils::ResetDescription(descArray[i]);
	}

	state_code.UpdateDesc(descArray, SamplerUsed, iBaseSamplerIndex);

	int iMaxSampler = D3D_COMMONSHADER_SAMPLER_SLOT_COUNT - 1;
	for (; iMaxSampler > -1; --iMaxSampler)
	{
		if (SamplerUsed[iMaxSampler])
			break;
	}

	if (iMaxSampler > -1)
	{
		SamplerArray.reserve(iMaxSampler + 1);
		for (int i = 0; i <= iMaxSampler; ++i)
		{
			if (SamplerUsed[i])
				SamplerArray.push_back(SSManager.GetState(descArray[i]));
			else
				SamplerArray.push_back(u32(dx10SamplerStateCache::hInvalidHandle));
		}
	}
}

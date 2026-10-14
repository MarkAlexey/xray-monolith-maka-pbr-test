#include "stdafx.h"
#include "dx10SamplerStateCache.h"

#include "../dx10StateUtils.h"

using dx10StateUtils::operator==;

dx10SamplerStateCache SSManager;

dx10SamplerStateCache::dx10SamplerStateCache():
    m_uiMaxAnisotropy(1), m_uiMipLODBias(0.0f)
{
    static const int iMaxRSStates = 10;
    m_StateArray.reserve(iMaxRSStates);
    ResetDeviceState();
}

dx10SamplerStateCache::~dx10SamplerStateCache()
{
    ClearStateArray();
}

dx10SamplerStateCache::SHandle dx10SamplerStateCache::GetState(D3D_SAMPLER_DESC& desc)
{
    SHandle hResult;

    //  MaxAnisitropy is reset by ValidateState if not aplicable
    //  to the filter mode used.
    desc.MaxAnisotropy = m_uiMaxAnisotropy;
    desc.MipLODBias = m_uiMipLODBias;
    dx10StateUtils::ValidateState(desc);

    u32 crc = dx10StateUtils::GetHash(desc);

    hResult = FindState(desc, crc);

    if (hResult == hInvalidHandle)
    {
        StateRecord rec;
        rec.m_crc = crc;
        CreateState(desc, &rec.m_pState);
        hResult = m_StateArray.size();
        m_StateArray.push_back(rec);
    }

    return hResult;
}

void dx10SamplerStateCache::CreateState(StateDecs desc, IDeviceState** ppIState)
{
    CHK_DX(HW.pDevice->CreateSamplerState( &desc, ppIState));
}

dx10SamplerStateCache::SHandle dx10SamplerStateCache::FindState(const StateDecs& desc, u32 StateCRC)
{
    u32 res = 0xffffffff;
    u32 i = 0;
    for (; i < m_StateArray.size(); ++i)
    {
        if (m_StateArray[i].m_crc == StateCRC)
        {
            StateDecs descCandidate;
            m_StateArray[i].m_pState->GetDesc(&descCandidate);
            if (descCandidate == desc)
                //return i;
                //  TEST
            {
                //return i;
                res = i;
                break;
            }
            //else
            //{
            //  VERIFY(0);
            //}
        }
    }

    return res != 0xffffffff ? i : (u32)hInvalidHandle;
}

void dx10SamplerStateCache::ClearStateArray()
{
    ResetDeviceState(); // Invalidate before handles can refer to released/reused objects.
    for (u32 i = 0; i < m_StateArray.size(); ++i)
    {
        _RELEASE(m_StateArray[i].m_pState);
    }

    m_StateArray.clear_not_free();
}

bool dx10SamplerStateCache::PrepareSamplerStates(
    HArray& samplers,
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT],
    SHandle pCurrentState[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT],
    u32& uiMin,
    u32& uiMax
)
{
    R_ASSERT(samplers.size() <= D3D_COMMONSHADER_SAMPLER_SLOT_COUNT);
#if XRCORE_PROFILER == PROFILER_OPTICK
    ++m_stats.requests;
#endif
    uiMin = D3D_COMMONSHADER_SAMPLER_SLOT_COUNT;
    uiMax = 0;
    for (u32 i = 0; i < D3D_COMMONSHADER_SAMPLER_SLOT_COUNT; ++i)
    {
        // The old path also unbound every trailing slot when a list got shorter.
        const SHandle desired = i < samplers.size() ? samplers[i] : SHandle(hInvalidHandle);
        if (m_external_depth || pCurrentState[i] != desired)
        {
            uiMin = _min(uiMin, i);
            uiMax = i;
        }
    }
    if (uiMin > uiMax)
    {
#if XRCORE_PROFILER == PROFILER_OPTICK
        ++m_stats.skipped;
#endif
        return false;
    }

    for (u32 i = uiMin; i <= uiMax; ++i)
    {
        const SHandle desired = i < samplers.size() ? samplers[i] : SHandle(hInvalidHandle);
        pSS[i] = nullptr;
        if (desired != hInvalidHandle)
        {
            R_ASSERT(desired < m_StateArray.size());
            pSS[i] = m_StateArray[desired].m_pState;
        }
    }
    return true;
}

void dx10SamplerStateCache::CommitSamplerStates(const HArray& samplers, SHandle* current, u32 first, u32 last)
{
    // Publish only after the corresponding context SetSamplers call.
    for (u32 i = first; i <= last; ++i)
        current[i] = i < samplers.size() ? samplers[i] : SHandle(hInvalidHandle);
#if XRCORE_PROFILER == PROFILER_OPTICK
    ++m_stats.calls;
    m_stats.slots += last - first + 1;
#endif
}

void dx10SamplerStateCache::VSApplySamplers(HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    u32 uiMin;
    u32 uiMax;
    if (!PrepareSamplerStates(samplers, pSS, m_aVSSamplers, uiMin, uiMax)) return;
    HW.pContext->VSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
    CommitSamplerStates(samplers, m_aVSSamplers, uiMin, uiMax);
}

void dx10SamplerStateCache::PSApplySamplers(HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    u32 uiMin;
    u32 uiMax;
    if (!PrepareSamplerStates(samplers, pSS, m_aPSSamplers, uiMin, uiMax)) return;
    HW.pContext->PSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
    CommitSamplerStates(samplers, m_aPSSamplers, uiMin, uiMax);
}

void dx10SamplerStateCache::GSApplySamplers(HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    u32 uiMin;
    u32 uiMax;
    if (!PrepareSamplerStates(samplers, pSS, m_aGSSamplers, uiMin, uiMax)) return;
    HW.pContext->GSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
    CommitSamplerStates(samplers, m_aGSSamplers, uiMin, uiMax);
}

#ifdef USE_DX11
void dx10SamplerStateCache::HSApplySamplers(HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    u32 uiMin;
    u32 uiMax;
    if (!PrepareSamplerStates(samplers, pSS, m_aHSSamplers, uiMin, uiMax)) return;
    HW.pContext->HSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
    CommitSamplerStates(samplers, m_aHSSamplers, uiMin, uiMax);
}

void dx10SamplerStateCache::DSApplySamplers(HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    u32 uiMin;
    u32 uiMax;
    if (!PrepareSamplerStates(samplers, pSS, m_aDSSamplers, uiMin, uiMax)) return;
    HW.pContext->DSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
    CommitSamplerStates(samplers, m_aDSSamplers, uiMin, uiMax);
}

void dx10SamplerStateCache::CSApplySamplers(HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    u32 uiMin;
    u32 uiMax;
    if (!PrepareSamplerStates(samplers, pSS, m_aCSSamplers, uiMin, uiMax)) return;
    HW.pContext->CSSetSamplers(uiMin, uiMax - uiMin + 1, &pSS[uiMin]);
    CommitSamplerStates(samplers, m_aCSSamplers, uiMin, uiMax);
}

void dx10SamplerStateCache::InvalidateComputeSamplers()
{
    for (auto& sampler : m_aCSSamplers) sampler = hUnknownHandle;
}
#endif


void dx10SamplerStateCache::SetMaxAnisotropy(u32 uiMaxAniso)
{
    clamp(uiMaxAniso, (u32)1, (u32)16);

    if (m_uiMaxAnisotropy == uiMaxAniso)
        return;

    m_uiMaxAnisotropy = uiMaxAniso;
    ResetDeviceState(); // Sampler objects change even though their handles do not.

    for (u32 i = 0; i < m_StateArray.size(); ++i)
    {
        StateRecord& rec = m_StateArray[i];
        StateDecs desc;

        rec.m_pState->GetDesc(&desc);

        //  MaxAnisitropy is reset by ValidateState if not aplicable
        //  to the filter mode used.
        //  Reason: all checks for aniso applicability are done
        //  in ValidateState.
        desc.MaxAnisotropy = m_uiMaxAnisotropy;
        dx10StateUtils::ValidateState(desc);

        //  This can cause fragmentation if called too often
        rec.m_pState->Release();
        CreateState(desc, &rec.m_pState);
    }
}

void dx10SamplerStateCache::SetMipLODBias(float uiMipLODBias)
{
    if (m_uiMipLODBias == uiMipLODBias)
        return;

    m_uiMipLODBias = uiMipLODBias;
    ResetDeviceState();

    for (u32 i = 0; i < m_StateArray.size(); ++i)
    {
        StateRecord& rec = m_StateArray[i];
        StateDecs desc;

        rec.m_pState->GetDesc(&desc);

        desc.MipLODBias = m_uiMipLODBias;
        dx10StateUtils::ValidateState(desc);

        // This can cause fragmentation if called too often
        rec.m_pState->Release();
        CreateState(desc, &rec.m_pState);
    }
}


void dx10SamplerStateCache::ResetDeviceState()
{
    for (int i = 0; i < sizeof(m_aPSSamplers) / sizeof(m_aPSSamplers[0]); ++i)
    {
        m_aPSSamplers[i] = hUnknownHandle;
        m_aVSSamplers[i] = hUnknownHandle;
        m_aGSSamplers[i] = hUnknownHandle;
#ifdef USE_DX11
        m_aHSSamplers[i] = hUnknownHandle;
        m_aDSSamplers[i] = hUnknownHandle;
        m_aCSSamplers[i] = hUnknownHandle;
#endif
    }
}

void dx10SamplerStateCache::ResetStats()
{
#if XRCORE_PROFILER == PROFILER_OPTICK
    m_stats = {};
#endif
}

void dx10SamplerStateCache::BeginExternalState()
{
    ++m_external_depth;
    ResetDeviceState();
}

void dx10SamplerStateCache::EndExternalState()
{
    R_ASSERT(m_external_depth);
    --m_external_depth;
    ResetDeviceState();
}

void dx10SamplerStateCache::ProfileStats() const
{
    PROF_TAG("Render.Samplers.Requests", m_stats.requests);
    PROF_TAG("Render.Samplers.Skipped", m_stats.skipped);
    PROF_TAG("Render.Samplers.Calls", m_stats.calls);
    PROF_TAG("Render.Samplers.Slots", m_stats.slots);
}

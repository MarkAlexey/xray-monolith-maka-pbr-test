#ifndef dx10SamplerStateCache_included
#define dx10SamplerStateCache_included
#pragma once


class dx10SamplerStateCache
{
public:
    enum
    {
        hInvalidHandle = 0xFFFFFFFF
    };

    //  State handle
    typedef u32 SHandle;
    typedef xr_vector<SHandle> HArray;
public:
    dx10SamplerStateCache();
    ~dx10SamplerStateCache();

    void ClearStateArray();

    SHandle GetState(D3D_SAMPLER_DESC& desc);

    void VSApplySamplers(HArray& samplers);
    void PSApplySamplers(HArray& samplers);
    void GSApplySamplers(HArray& samplers);
#ifdef USE_DX11
    void HSApplySamplers(HArray& samplers);
    void DSApplySamplers(HArray& samplers);
    void CSApplySamplers(HArray& samplers);
    void InvalidateComputeSamplers();
#endif

    void SetMaxAnisotropy(u32 uiMaxAniso);
    void SetMipLODBias(float uiMipLODBias);

    // Unknown is different from a known NULL binding: the next apply must resubmit.
    void ResetDeviceState();
    void BeginExternalState();
    void EndExternalState();
    void ResetStats();
    void ProfileStats() const;
    void Synchronize(const dx10SamplerStateCache& source);

    //  Private declarations
private:
    typedef ID3DSamplerState IDeviceState;
    typedef D3D_SAMPLER_DESC StateDecs;

	struct StateVariant
	{
		IDeviceState* m_pState = nullptr;
		float m_mipLODBias = 0.0f;
		u32 m_maxAnisotropy = 1;
	};

	struct StateRecord
	{
		u32 m_crc;
		bool m_usesAnisotropy;
		// MRU first, active state at index 0. Four entries cover both scene/UI
		// biases at selected/1x AF, with bounded LRU eviction after user changes.
		StateVariant m_variants[4];
	};


private:
    void CreateState(StateDecs desc, IDeviceState** ppIState);
    void SelectVariant(StateRecord& rec);
    SHandle FindState(const StateDecs& desc, u32 StateCRC);

    bool PrepareSamplerStates(
        HArray& samplers,
        ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT],
        SHandle pCurrentState[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT],
        u32& uiMin,
        u32& uiMax
    );
    void CommitSamplerStates(const HArray& samplers, SHandle* current, u32 first, u32 last);

    //  Private data
private:
    static constexpr SHandle hUnknownHandle = 0xFFFFFFFEu;
    u32 m_external_depth = 0; // Owner-thread scope; external callbacks may bypass every stage cache.
    struct Stats { u32 requests = 0, skipped = 0, calls = 0, slots = 0; } m_stats;
    //  This must be cleared on device destroy
    xr_vector<StateRecord> m_StateArray;
    u64 m_generation = 0, m_source_generation = u64(-1);
    mutable xrCriticalSection m_lock;

    SHandle m_aPSSamplers[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    SHandle m_aVSSamplers[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    SHandle m_aGSSamplers[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
#ifdef USE_DX11
    SHandle m_aHSSamplers[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    SHandle m_aDSSamplers[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
    SHandle m_aCSSamplers[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];
#endif

    u32 m_uiMaxAnisotropy;
    float m_uiMipLODBias;
};

extern dx10SamplerStateCache SSManager;
#if defined(USE_DX11) && RENDER == R_R4
#define SSManager ActiveSamplerManager()
#endif

#endif  //  dx10SamplerStateCache_included

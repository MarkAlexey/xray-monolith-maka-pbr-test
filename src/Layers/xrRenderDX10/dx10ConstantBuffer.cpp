#include "stdafx.h"
#include "dx10ConstantBuffer.h"

#include "dx10BufferUtils.h"
#include "../xrRender/dxRenderDeviceRender.h"

thread_local dx10ConstantBuffer::Stats dx10ConstantBuffer::s_stats{};

dx10ConstantBuffer::~dx10ConstantBuffer()
{
	if (!m_private_copy && Device.m_pRender && DEV)
		DEV->_DeleteConstantBuffer(this);
	//	Flush();
	_RELEASE(m_pBuffer);
	xr_free(m_pBufferData);
}

dx10ConstantBuffer::dx10ConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable)
	: m_bChanged(true)
{
	D3D_SHADER_BUFFER_DESC Desc;

	CHK_DX(pTable->GetDesc(&Desc));

	m_strBufferName._set(Desc.Name);
	m_eBufferType = Desc.Type;
	m_uiBufferSize = Desc.Size;

	//	Fill member list with variable descriptions
	m_MembersList.resize(Desc.Variables);
	m_MembersNames.resize(Desc.Variables);
    m_MembersOffsets.resize(Desc.Variables);
	for (u32 i = 0; i < Desc.Variables; ++i)
	{
		ID3DShaderReflectionVariable* pVar;
		ID3DShaderReflectionType* pType;

		D3D_SHADER_VARIABLE_DESC var_desc;

		pVar = pTable->GetVariableByIndex(i);
		VERIFY(pVar);
		pType = pVar->GetType();
		VERIFY(pType);
		pType->GetDesc(&m_MembersList[i]);
		//	Buffers with the same layout can contain totally different members
		CHK_DX(pVar->GetDesc(&var_desc));
		m_MembersNames[i] = var_desc.Name;
        m_MembersOffsets[i] = var_desc.StartOffset;
	}

	m_uiMembersCRC = crc32(&m_MembersList[0], Desc.Variables * sizeof(m_MembersList[0]));

	R_CHK(dx10BufferUtils::CreateConstantBuffer(&m_pBuffer, Desc.Size));
	VERIFY(m_pBuffer);
	m_pBufferData = xr_malloc(Desc.Size);
	VERIFY(m_pBufferData);
    ZeroMemory(m_pBufferData, Desc.Size);
}

bool dx10ConstantBuffer::Similar(dx10ConstantBuffer& _in)
{
	if (m_strBufferName._get() != _in.m_strBufferName._get())
		return false;

	if (m_uiBufferSize != _in.m_uiBufferSize || m_MembersOffsets != _in.m_MembersOffsets)
        return false;

	if (m_eBufferType != _in.m_eBufferType)
		return false;

	if (m_uiMembersCRC != _in.m_uiMembersCRC)
		return false;

	if (m_MembersList.size() != _in.m_MembersList.size())
		return false;

	if (memcmp(&m_MembersList[0], &_in.m_MembersList[0], m_MembersList.size() * sizeof(m_MembersList[0])))
		return false;

	VERIFY(m_MembersNames.size() == _in.m_MembersNames.size());

	int iMemberNum = m_MembersNames.size();
	for (int i = 0; i < iMemberNum; ++i)
	{
		if (m_MembersNames[i].c_str() != _in.m_MembersNames[i].c_str())
			return false;
	}

	return true;
}

dx10ConstantBuffer::dx10ConstantBuffer(const dx10ConstantBuffer& source, bool private_copy)
    : m_private_copy(private_copy), m_strBufferName(source.m_strBufferName), m_eBufferType(source.m_eBufferType),
      m_uiMembersCRC(source.m_uiMembersCRC), m_MembersList(source.m_MembersList),
      m_MembersNames(source.m_MembersNames), m_MembersOffsets(source.m_MembersOffsets),
      m_pBuffer(nullptr), m_uiBufferSize(source.m_uiBufferSize), m_bChanged(true)
{
    R_CHK(dx10BufferUtils::CreateConstantBuffer(&m_pBuffer, m_uiBufferSize));
    m_pBufferData = xr_malloc(m_uiBufferSize);
    ZeroMemory(m_pBufferData, m_uiBufferSize);
}

void dx10ConstantBuffer::CopyDefaults(const dx10ConstantBuffer& source)
{
    R_ASSERT(m_private_copy && m_uiBufferSize == source.m_uiBufferSize);
    if (memcmp(m_pBufferData, source.m_pBufferData, m_uiBufferSize))
    {
        CopyMemory(m_pBufferData, source.m_pBufferData, m_uiBufferSize);
        m_bChanged = true;
    }
}

#ifdef USE_DX11
bool dx10ConstantBuffer::WriteFloat4Array(R_constant* C, R_constant_load& L, const Fvector4* data, u32 count)
{
    if (!C || C->type != RC_float || L.cls != RC_1x4 || !data || !count ||
        L.index > m_uiBufferSize || count > (m_uiBufferSize - L.index) / sizeof(Fvector4)) return false;
    for (u32 i = 0; i < m_MembersNames.size(); ++i)
        if (m_MembersNames[i] == C->name && m_MembersOffsets[i] == L.index)
        {
            const auto& type = m_MembersList[i];
            if (type.Class != D3D_SVC_VECTOR || type.Type != D3D_SVT_FLOAT ||
                type.Rows != 1 || type.Columns != 4 || type.Elements < count) return false;
            Write(L.index, data, count * sizeof(Fvector4));
            return true;
        }
    return false;
}
#endif

void dx10ConstantBuffer::Flush()
{
    if (m_bChanged)
    {
        PROF_EVENT("Render.Constants.Upload");
        void    *pData;
#ifdef USE_DX11
        D3D11_MAPPED_SUBRESOURCE    pSubRes;
        CHK_DX(RenderDeviceContext()->Map(m_pBuffer, 0, D3D_MAP_WRITE_DISCARD, 0, &pSubRes));
        pData = pSubRes.pData;
#else
        CHK_DX(m_pBuffer->Map(D3D_MAP_WRITE_DISCARD, 0, &pData));
#endif
        VERIFY(pData);
        VERIFY(m_pBufferData);
        CopyMemory(pData, m_pBufferData, m_uiBufferSize);
#ifdef USE_DX11
        RenderDeviceContext()->Unmap(m_pBuffer, 0);
#else
        m_pBuffer->Unmap();
#endif
        m_bChanged = false;
#if XRCORE_PROFILER == PROFILER_OPTICK
        ++s_stats.uploads;
        s_stats.bytes += m_uiBufferSize;
#endif
    }
}

void dx10ConstantBuffer::ResetStats()
{
#if XRCORE_PROFILER == PROFILER_OPTICK
	s_stats = {};
#endif
}

void dx10ConstantBuffer::ProfileStats()
{
	PROF_TAG("Render.Constants.TypedWrites", s_stats.typed);
	PROF_TAG("Render.Constants.CleanWritesSkipped", s_stats.skipped);
	PROF_TAG("Render.Constants.DirectAccesses", s_stats.direct);
	PROF_TAG("Render.Constants.Uploads", s_stats.uploads);
	PROF_TAG("Render.Constants.UploadBytes", s_stats.bytes);
}

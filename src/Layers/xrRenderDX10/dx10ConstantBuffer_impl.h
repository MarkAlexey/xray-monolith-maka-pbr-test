#ifndef dx10ConstantBuffer_impl_included
#define dx10ConstantBuffer_impl_included
#pragma once

IC void dx10ConstantBuffer::Write(u32 offset, const void* value, u32 size)
{
    R_ASSERT(offset <= m_uiBufferSize && size <= m_uiBufferSize - offset);
    BYTE* destination = static_cast<BYTE*>(m_pBufferData) + offset;
#if XRCORE_PROFILER == PROFILER_OPTICK
    ++s_stats.typed;
#endif
    // Compare only this typed write (4..64 bytes), and only while otherwise clean.
    // Bitwise comparison preserves signed zero and NaN payloads. A dirty buffer
    // already needs uploading, so another comparison would buy nothing.
    if (!m_bChanged && memcmp(destination, value, size) == 0)
    {
#if XRCORE_PROFILER == PROFILER_OPTICK
        ++s_stats.skipped;
#endif
        return;
    }
    CopyMemory(destination, value, size);
    m_bChanged = true;
}

IC void dx10ConstantBuffer::WriteMatrix(R_constant* C, R_constant_load& L, u32 element, const Fmatrix& A)
{
    VERIFY(RC_float == C->type);
    const u32 rows = L.cls == RC_2x4 ? 2u : L.cls == RC_3x4 ? 3u : L.cls == RC_4x4 ? 4u : 0u;
    R_ASSERT(rows);
    const u32 size = rows * lineSize;
    R_ASSERT(L.index <= m_uiBufferSize && element < (m_uiBufferSize - L.index) / size);
    Fvector4 packed[4];
    packed[0].set(A._11, A._21, A._31, A._41);
    packed[1].set(A._12, A._22, A._32, A._42);
    if (rows >= 3) packed[2].set(A._13, A._23, A._33, A._43);
    if (rows == 4) packed[3].set(A._14, A._24, A._34, A._44);
    Write(L.index + element * size, packed, size);
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, const Fmatrix& A)
{
    WriteMatrix(C, L, 0, A);
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, const Fvector4& A)
{
    VERIFY(RC_float == C->type);
    VERIFY(RC_1x4 == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls || RC_1x1 == L.cls);
    u32 count = 4;
    switch (L.cls)
    {
    case RC_1x1:
        count = 1;
        break;
    case RC_1x2:
        count = 2;
        break;
    case RC_1x3:
        count = 3;
        break;
    case RC_1x4:
        count = 4;
        break;
    default:
        break;
    }

    Write(L.index, &A[0], count * sizeof(float));
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, float A)
{
    VERIFY(RC_float == C->type);
    VERIFY(RC_1x1 == L.cls);
    Write(L.index, &A, sizeof(A));
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, int A)
{
    VERIFY(RC_int == C->type);
    VERIFY(RC_1x1 == L.cls);
    Write(L.index, &A, sizeof(A));
}

IC void dx10ConstantBuffer::seta(R_constant* C, R_constant_load& L, u32 e, const Fmatrix& A)
{
    WriteMatrix(C, L, e, A);
}

IC void dx10ConstantBuffer::seta(R_constant* C, R_constant_load& L, u32 e, const Fvector4& A)
{
    VERIFY(RC_float == C->type);
    VERIFY(RC_1x4 == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls);

    // Array elements retain the original full float4 write, including padding.
    R_ASSERT(L.index <= m_uiBufferSize && e < (m_uiBufferSize - L.index) / lineSize);
    Write(L.index + lineSize * e, &A, lineSize);
}

IC void dx10ConstantBuffer::set_bytes(R_constant_load& L, const void* data, u32 bytes)
{
    R_ASSERT(u32(L.index) <= m_uiBufferSize && L.size <= m_uiBufferSize - L.index);
    const u32 copied = _min(bytes, L.size);
    Write(L.index, data, copied);
    if (copied < L.size)
    {
        BYTE* tail = static_cast<BYTE*>(m_pBufferData) + L.index + copied;
        const u32 count = L.size - copied;
        for (u32 i = 0; i < count; ++i)
        {
            if (tail[i] != 0)
            {
                ZeroMemory(tail, count);
                m_bChanged = true;
                break;
            }
        }
    }
}

IC void* dx10ConstantBuffer::AccessDirect(R_constant_load& L, u32 DataSize)
{
    //  Check buffer size in client code: don't know if actual data will cross
    //  buffer boundaries.
    VERIFY(L.index<(int)m_uiBufferSize);
    BYTE* res = ((BYTE*)m_pBufferData) + L.index;

    if (u32(L.index) <= m_uiBufferSize && DataSize <= m_uiBufferSize - u32(L.index))
    {
        m_bChanged = true;
#if XRCORE_PROFILER == PROFILER_OPTICK
        ++s_stats.direct;
#endif
        return res;
    }
    else return 0;
}

#endif  //  dx10ConstantBuffer_impl_included

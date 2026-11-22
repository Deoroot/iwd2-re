#include "CResBIO.h"

// NOTE: Inlined.
//
// Only ever emitted inline, in `CBaldurChitin::AllocResObject`'s case 1022 at
// 0x423637: `operator new(0x54)`, `CRes::CRes`, vtable 0x8480C8, `+0x50 = 0` --
// the same five steps as case 2050's CResINI a few bytes further down.
CResBIO::CResBIO()
{
    m_bParsed = FALSE;
}

// 0x423790
CResBIO::~CResBIO()
{
}

// 0x4029F0
void* CResBIO::Demand()
{
    void* pData = CRes::Demand();
    if (m_bParsed && GetDemands() > 1) {
        return pData;
    }
    if (pData != NULL) {
        m_bParsed = TRUE;
    }
    return m_bParsed ? pData : NULL;
}

// Shared with CResCHR::Release (0x4022B0).
// 0x4022B0
int CResBIO::Release()
{
    int result = CRes::Release();
    if (GetDemands() <= 0) {
        m_bParsed = FALSE;
    }
    return result;
}

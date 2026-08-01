#include "CResStore.h"

// 0x402660
CResStore::CResStore()
{
    m_bParsed = FALSE;
}

// 0x4026A0
CResStore::~CResStore()
{
}

// 0x4026B0
void* CResStore::Demand()
{
    void* pData = CRes::Demand();
    if (!m_bParsed || GetDemands() <= 1) {
        // NOTE: Uninline.
        Parse(pData);

        if (!m_bParsed) {
            return NULL;
        }
    }

    return pData;
}

// NOTE: Inlined in `CResStore::Demand`.
BOOL CResStore::Parse(void* pData)
{
    if (pData == NULL) {
        return FALSE;
    }

    // The binary compares the two header dwords against 0x524F5453 / 0x4F4F5453
    // and 0x302E3956, i.e. the bytes as they sit in the file. A multi-character
    // literal packs them the other way round, so it never matches.
    BYTE* header = reinterpret_cast<BYTE*>(pData);
    if ((memcmp(header, "STOR", 4) != 0 && memcmp(header, "STOO", 4) != 0)
        || memcmp(header + 4, "V9.0", 4) != 0) {
        return FALSE;
    }

    m_bParsed = TRUE;

    return m_bParsed;
}

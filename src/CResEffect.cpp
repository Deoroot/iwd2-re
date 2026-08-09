#include "CResEffect.h"

// 0x402850
CResEffect::CResEffect()
{
    m_bParsed = FALSE;
}

// 0x402890
CResEffect::~CResEffect()
{
}

// 0x4028A0
void* CResEffect::Demand()
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

// NOTE: Inlined in `CResEffect::Demand`.
BOOL CResEffect::Parse(void* pData)
{
    if (pData == NULL) {
        return FALSE;
    }

    // 0x20464645 / 0x302E3256 in the binary: the bytes as they sit in the file.
    // A multi-character literal packs them the other way round.
    BYTE* header = reinterpret_cast<BYTE*>(pData);
    if (memcmp(header, "EFF ", 4) != 0 || memcmp(header + 4, "V2.0", 4) != 0) {
        return FALSE;
    }

    m_bParsed = TRUE;

    return m_bParsed;
}

#include "CGameSave.h"

// NOTE: Inlined.
CGameSave::CGameSave()
{
    m_nCurrentWorldLink = 0;
    m_nPartyGold = 0;
    m_curFormation = 0;
    for (SHORT nFormation = 0; nFormation < 5; nFormation++) {
        m_quickFormations[nFormation] = nFormation;
    }
    memset(m_groupInventory, 0, sizeof(m_groupInventory));
    m_bSequenceMode = 0;
    field_1B0 = 0;
    field_1B2 = 0;
    m_mode = 0;
    m_cutScene = FALSE;
}

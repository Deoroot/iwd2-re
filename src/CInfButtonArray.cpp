#include "CInfButtonArray.h"

#include "CBaldurChitin.h"
#include "CButtonData.h"
#include "CGameSave.h"
#include "CGameSprite.h"
#include "CInfGame.h"
#include "CScreenWorld.h"
#include "CUIControlBase.h"
#include "CUIControlButton.h"
#include "CUIManager.h"
#include "CUIPanel.h"

// 0x851700
const BYTE CInfButtonArray::STATE_NONE = 0;

// 0x587960
CInfButtonSettings::CInfButtonSettings()
{
    field_1C8 = 0;
    field_1CC = 0;
    field_1D8 = 0;
    field_0 = 0;
    field_4 = 0;
    field_8 = 0;
    field_1D0 = 0;
    m_bGreyOut = FALSE;
    field_C = -1;
    field_10 = -1;
}

// 0x587B80
CInfButtonSettings::~CInfButtonSettings()
{
}

// 0x5879E0
CInfButtonArray::CInfButtonArray()
{
    // TODO: Incomplete.

    for (INT nButton = 0; nButton < 12; nButton++) {
        m_buttonTypes[nButton] = 100;
        m_buttonArray[nButton].m_bGreyOut = FALSE;
    }

    m_nSelectedButton = 100;
    m_nState = STATE_NONE;
    field_19B2 = 0;
}

// 0x588240
void CInfButtonArray::GetSelectedQuickWeaponData(CButtonData& cButtonData)
{
    if (g_pBaldurChitin->GetObjectGame()->GetGroup()->GetCount() != 0) {
        LONG* groupList = g_pBaldurChitin->GetObjectGame()->GetGroup()->GetGroupList();
        LONG nCharacterId = groupList[0];
        delete groupList;

        CGameSprite* pSprite;

        BYTE rc;
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

        if (rc == CGameObjectArray::SUCCESS) {
            pSprite->GetSelectedWeaponButton(cButtonData);

            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
    }
}

// 0x5883C0
BYTE CInfButtonArray::GetSelectedModalMode()
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    BYTE modalState = 0;

    if (pGame->GetGroup()->GetCount() != 0) {
        LONG nCharacterId = pGame->GetGroup()->GetGroupLeader();

        CGameSprite* pSprite;

        BYTE rc;
        do {
            rc = pGame->GetObjectArray()->GetShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

        if (rc == CGameObjectArray::SUCCESS) {
            modalState = pSprite->GetModalState();

            pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
    }

    return modalState;
}

// 0x588460
BYTE CInfButtonArray::GetButtonId(INT buttonType)
{
    for (BYTE id = 0; id < 12; id++) {
        if (m_buttonTypes[id] == buttonType && !m_buttonArray[id].m_bGreyOut) {
            return id;
        }
    }
    return -1;
}

// 0x588FF0
BOOL CInfButtonArray::ResetState()
{
    // TODO: Incomplete.

    m_nSelectedButton = 100;
    SetState(STATE_NONE, 0);

    return TRUE;
}

// 0x589100
void CInfButtonArray::UpdateState()
{
    SetState(m_nState, 0);
}

// 0x589110
BOOL CInfButtonArray::SetState(INT nState, int a2)
{
    // TODO: Incomplete.

    switch (nState) {
    case 0x6E:
        // Group / generic creature action bar.
        m_buttonTypes[0] = 7;
        m_buttonTypes[1] = 8;
        m_buttonTypes[2] = 0x0F;
        m_buttonTypes[3] = 0x10;
        m_buttonTypes[4] = 0x11;
        m_buttonTypes[5] = 0x12;
        m_buttonTypes[6] = 0x13;
        m_buttonTypes[7] = 0x14;
        m_buttonTypes[8] = 100;
        m_buttonTypes[9] = 100;
        m_buttonTypes[10] = 100;
        m_buttonTypes[11] = 100;
        m_nState = nState;
        UpdateButtons();
        return TRUE;
    case 0x72:
        // Single PC action bar.  The original fills several entries from the
        // selected creature's custom quick-button table; keep the static
        // Ghidra defaults here until those button-data helpers are recovered.
        m_buttonTypes[0] = 7;
        m_buttonTypes[1] = 0x3C;
        m_buttonTypes[2] = 0x3D;
        m_buttonTypes[3] = 5;
        m_buttonTypes[4] = 3;
        m_buttonTypes[5] = 0x46;
        m_buttonTypes[6] = 0x47;
        m_buttonTypes[7] = 0x50;
        m_buttonTypes[8] = 0x51;
        m_buttonTypes[9] = 0x5A;
        m_buttonTypes[10] = 0x5B;
        m_buttonTypes[11] = 10;
        m_nState = nState;
        UpdateButtons();
        return TRUE;
    case STATE_NONE:
    default:
        for (INT nButton = 0; nButton < 12; nButton++) {
            m_buttonTypes[nButton] = 100;
        }
        m_nState = STATE_NONE;
        UpdateButtons();
        return TRUE;
    }
}

// 0x58A340
void CInfButtonArray::UpdateButtons()
{
    // TODO: Incomplete.

    CScreenWorld* pWorld = g_pBaldurChitin->GetScreenWorld();
    if (pWorld == NULL) {
        return;
    }

    CUIPanel* pPanel = pWorld->m_cUIManager.GetPanel(1);
    if (pPanel == NULL) {
        return;
    }

    for (INT nButton = 0; nButton < 12; nButton++) {
        CUIControlBase* pControl = pPanel->GetControl(nButton + 6);
        if (pControl == NULL) {
            continue;
        }

        CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
        CResRef cResRef("GUIBTBUT");
        SHORT nNormalFrame = static_cast<SHORT>(nButton * 2);
        SHORT nPressedFrame = static_cast<SHORT>(nButton * 2 + 1);
        STRREF nToolTip = -1;
        USHORT nHotKey = 0xFFFF;
        BOOL bEnabled = TRUE;

        switch (m_buttonTypes[nButton]) {
        case 7:
            cResRef = CResRef("GUIBTACT");
            nNormalFrame = 0;
            nPressedFrame = 2;
            nToolTip = 0x3E35;
            break;
        case 8:
            cResRef = CResRef("GUIBTACT");
            nNormalFrame = 0x0C;
            nPressedFrame = 0x0E;
            nToolTip = 0x123A;
            nHotKey = 0x12;
            break;
        case 0x0F:
            cResRef = CResRef("GUIBTACT");
            nNormalFrame = 0x2C;
            nPressedFrame = 0x2C;
            nToolTip = 0x3E34;
            nHotKey = 0x11;
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14: {
            INT nFormationButton = m_buttonTypes[nButton] - 0x10;
            SHORT nFormation = g_pBaldurChitin->GetObjectGame()->GetGameSave()->m_quickFormations[nFormationButton];
            if (nFormation < 0) {
                cResRef = CResRef("STONFORM");
            } else if (nFormation < 10) {
                CString sResRef;
                sResRef.Format("FORM%d", nFormation);
                cResRef = CResRef(sResRef);
            } else {
                CString sResRef;
                sResRef.Format("FORM%c", static_cast<char>(nFormation + '7'));
                cResRef = CResRef(sResRef);
            }
            nNormalFrame = 0;
            nPressedFrame = 0;
            nToolTip = 0x1347;
            nHotKey = static_cast<USHORT>(0x20 + nFormationButton);
            break;
        }
        case 3:
            cResRef = CResRef("GUIBTACT");
            nNormalFrame = 8;
            nPressedFrame = 10;
            nToolTip = 0x1250;
            break;
        case 5:
            cResRef = CResRef("GUIBTACT");
            nNormalFrame = 0x60;
            nPressedFrame = 0x62;
            break;
        case 10:
            cResRef = CResRef("GUIBTACT");
            nNormalFrame = 0x28;
            nPressedFrame = 0x2A;
            nToolTip = 0x135A;
            nHotKey = 0x13;
            break;
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
            cResRef = CResRef("GUIBTACT");
            nNormalFrame = 0x68;
            nPressedFrame = 0x6A;
            break;
        case 100:
        default:
            bEnabled = FALSE;
            break;
        }

        pButton->m_nNormalFrame = nNormalFrame;
        pButton->m_nPressedFrame = nPressedFrame;
        pButton->m_nDisabledFrame = nNormalFrame;
        pButton->m_cVidCell.SetResRef(cResRef, pPanel->m_pManager->m_bDoubleSize, TRUE, TRUE);
        pButton->m_cVidCell.SequenceSet(0);
        pButton->m_cVidCell.FrameSet(nNormalFrame);
        pButton->SetToolTipStrRef(nToolTip, -1, -1);
        pButton->SetToolTipHotKey(nHotKey, 0xFFFF, CString(""));
        pButton->SetEnabled(bEnabled);
        pButton->InvalidateRect();
    }

    pPanel->InvalidateRect(NULL);
}

// 0x452C50
void CInfButtonArray::SetSelectedButton(INT nSelectedButton)
{
    m_nSelectedButton = nSelectedButton;
}

// 0x58FF20
void CInfButtonArray::OnLButtonPressed(int buttonID)
{
    // TODO: Incomplete.
}

// 0x594720
void CInfButtonArray::OnRButtonPressed(int buttonID)
{
    // TODO: Incomplete.
}

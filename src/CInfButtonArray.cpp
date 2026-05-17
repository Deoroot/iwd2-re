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

    field_1976 = "";
    m_nSelectedButton = 100;
    m_nState = STATE_NONE;
    memset(field_1986, 0, sizeof(field_1986));
    field_19B2 = 0;

    // 0x19B6..0x19D6, copied from selected sprite by SelectToolbar.
    m_customButtonTypes[0] = 5;
    m_customButtonTypes[1] = 3;
    m_customButtonTypes[2] = 0x46;
    m_customButtonTypes[3] = 0x47;
    m_customButtonTypes[4] = 0x50;
    m_customButtonTypes[5] = 0x51;
    m_customButtonTypes[6] = 0x5A;
    m_customButtonTypes[7] = 0x5B;
    m_customButtonTypes[8] = 10;
    m_nCurrentSelectedSpellClass = 0;
    m_nCurrentSelectedSpellLevel = 0;
    m_currentAbilityResRef = "";
    m_nQuickWeaponSlot = 0;
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
    case 0x6C:
    case 0x6D:
        for (INT nButton = 0; nButton < 12; nButton++) {
            m_buttonTypes[nButton] = 0x15 + nButton;
        }
        m_nState = nState;
        UpdateButtons();
        return TRUE;
    case 0x72:
        // Single PC action bar.  Matches Ghidra default layout: protect,
        // weapon pair, then the nine custom button slots copied from sprite
        // offset 0x3D14 by SelectToolbar.
        m_buttonTypes[0] = 7;
        m_buttonTypes[1] = m_nQuickWeaponSlot * 2 + 0x3C;
        m_buttonTypes[2] = m_nQuickWeaponSlot * 2 + 0x3D;
        m_buttonTypes[3] = m_customButtonTypes[0];
        m_buttonTypes[4] = m_customButtonTypes[1];
        m_buttonTypes[5] = m_customButtonTypes[2];
        m_buttonTypes[6] = m_customButtonTypes[3];
        m_buttonTypes[7] = m_customButtonTypes[4];
        m_buttonTypes[8] = m_customButtonTypes[5];
        m_buttonTypes[9] = m_customButtonTypes[6];
        m_buttonTypes[10] = m_customButtonTypes[7];
        m_buttonTypes[11] = m_customButtonTypes[8];
        m_nState = nState;
        UpdateButtons();
        return TRUE;
    case 0x75: {
        m_buttonTypes[0] = 0x23;

        CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
        LONG nCharacterId = pGame->GetGroup()->GetGroupLeader();
        CGameSprite* pSprite = NULL;
        BYTE rc = pGame->GetObjectArray()->GetShare(nCharacterId,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);

        BOOL bSpellcaster = rc == CGameObjectArray::SUCCESS && pSprite != NULL && pSprite->IsSpellcaster();
        BOOL bBard = rc == CGameObjectArray::SUCCESS && pSprite != NULL && pSprite->IsBard();

        if (rc == CGameObjectArray::SUCCESS) {
            pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }

        if (bSpellcaster) {
            m_buttonTypes[1] = 0x24;
            m_buttonTypes[2] = 0x25;
            m_buttonTypes[3] = 0x26;
            m_buttonTypes[4] = 0x27;
            m_buttonTypes[5] = bBard ? 0x28 : 100;
        } else {
            m_buttonTypes[1] = 0x25;
            m_buttonTypes[2] = 0x26;
            m_buttonTypes[3] = 0x27;
            m_buttonTypes[4] = bBard ? 0x28 : 100;
            m_buttonTypes[5] = 100;
        }

        m_buttonTypes[6] = 100;
        m_buttonTypes[7] = 100;
        m_buttonTypes[8] = 100;
        m_buttonTypes[9] = 100;
        m_buttonTypes[10] = 0x29;
        m_buttonTypes[11] = 0x2A;
        m_nState = nState;
        UpdateButtons();
        return TRUE;
    }
    case 0x76:
    case 0x77: {
        // Spell class picker.  Ghidra probes classes 2,3,4,7,8,10,11 and
        // maps them to buttons 0x32..0x38 (+ 0x39 domain for class 3).
        CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
        LONG nCharacterId = pGame->GetGroup()->GetGroupLeader();
        CGameSprite* pSprite = NULL;
        BYTE rc = pGame->GetObjectArray()->GetShare(nCharacterId,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);

        INT nButton = 0;
        if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
            const BYTE classes[] = { 2, 3, 4, 7, 8, 10, 11 };
            const INT buttons[] = { 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
            for (INT nIndex = 0; nIndex < 7 && nButton < 12; nIndex++) {
                BYTE nClass = classes[nIndex];
                CGameSpriteGroupedSpellList* pSpells = pSprite->GetSpells(nClass);
                if (pSpells->m_nHighestLevel != 0) {
                    m_buttonTypes[nButton++] = buttons[nIndex];
                    if (nClass == 3 && nButton < 12) {
                        m_buttonTypes[nButton++] = 0x39;
                    }
                }
            }

            pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }

        while (nButton < 12) {
            m_buttonTypes[nButton++] = 100;
        }

        m_nState = nState;
        UpdateButtons();
        return TRUE;
    }
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

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    CGameSprite* pSprite = NULL;
    LONG nLeaderId = CGameObjectArray::INVALID_INDEX;
    BYTE rc = CGameObjectArray::INVALID_INDEX;
    if (pGame->GetGroup()->GetCount() != 0) {
        nLeaderId = pGame->GetGroup()->GetGroupLeader();
        do {
            rc = pGame->GetObjectArray()->GetShare(nLeaderId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
    }

    for (INT nButton = 0; nButton < 12; nButton++) {
        CUIControlBase* pControl = pPanel->GetControl(nButton + 6);
        if (pControl == NULL) {
            continue;
        }

        CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
        CInfButtonSettings& settings = m_buttonArray[nButton];

        CResRef cIconResRef("GUIBTACT");
        SHORT nIconNormalFrame = -1;
        SHORT nIconSelectedFrame = -1;
        STRREF nToolTip = -1;
        USHORT nHotKey = 0xFFFF;
        BOOL bEnabled = TRUE;
        BOOL bActive = TRUE;
        BOOL bGreyOut = FALSE;

        settings.field_1CC = m_nSelectedButton == m_buttonTypes[nButton];
        settings.m_bGreyOut = FALSE;

        switch (m_buttonTypes[nButton]) {
        case 7:
            nIconNormalFrame = 0;
            nIconSelectedFrame = 2;
            nToolTip = 0x3E35;
            break;
        case 8:
            nIconNormalFrame = 0x0C;
            nIconSelectedFrame = 0x0E;
            nToolTip = 0x123A;
            nHotKey = 0x12;
            break;
        case 0x0F:
            nIconNormalFrame = 0x2C;
            nIconSelectedFrame = 0x2C;
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
                cIconResRef = CResRef("STONFORM");
            } else if (nFormation < 10) {
                CString sResRef;
                sResRef.Format("FORM%d", nFormation);
                cIconResRef = CResRef(sResRef);
            } else {
                CString sResRef;
                sResRef.Format("FORM%c", static_cast<char>(nFormation + '7'));
                cIconResRef = CResRef(sResRef);
            }
            nIconNormalFrame = 0;
            nIconSelectedFrame = 0;
            nToolTip = 0x1347;
            nHotKey = static_cast<USHORT>(0x20 + nFormationButton);
            break;
        }
        case 3:
            nIconNormalFrame = 8;
            nIconSelectedFrame = 10;
            nToolTip = 0x1250;
            break;
        case 5:
            nIconNormalFrame = 0x60;
            nIconSelectedFrame = 0x62;
            break;
        case 10:
            nIconNormalFrame = 0x28;
            nIconSelectedFrame = 0x2A;
            nToolTip = 0x135A;
            nHotKey = 0x13;
            break;
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
        case 0x1F:
        case 0x20: {
            if (m_nState == 0x6C || m_nState == 0x6D) {
                INT nFormation = m_buttonTypes[nButton] - 0x15;
                CString sResRef;
                if (nFormation < 10) {
                    sResRef.Format("FORM%d", nFormation);
                } else {
                    sResRef.Format("FORM%c", static_cast<char>('A' + nFormation - 10));
                }
                cIconResRef = CResRef(sResRef);
                nIconNormalFrame = 0;
                nIconSelectedFrame = 0;
            } else {
                bActive = FALSE;
                bEnabled = FALSE;
                cIconResRef = CResRef("");
            }
            break;
        }
        case 0x23:
            nIconNormalFrame = 0x60;
            nIconSelectedFrame = 0x62;
            break;
        case 0x24:
            nIconNormalFrame = 8;
            nIconSelectedFrame = 10;
            break;
        case 0x25:
            nIconNormalFrame = 0x10;
            nIconSelectedFrame = 0x12;
            break;
        case 0x26:
            nIconNormalFrame = 0x70;
            nIconSelectedFrame = 0x72;
            break;
        case 0x27:
            nIconNormalFrame = 0x28;
            nIconSelectedFrame = 0x2A;
            break;
        case 0x28:
            nIconNormalFrame = 0x14;
            nIconSelectedFrame = 0x16;
            break;
        case 0x29:
            nIconNormalFrame = 0x74;
            nIconSelectedFrame = 0x76;
            break;
        case 0x2A:
            nIconNormalFrame = 0x78;
            nIconSelectedFrame = 0x7A;
            break;
        case 0x32:
            nIconNormalFrame = 0x38;
            nIconSelectedFrame = 0x3A;
            break;
        case 0x33:
            nIconNormalFrame = 0x3C;
            nIconSelectedFrame = 0x3E;
            break;
        case 0x34:
            nIconNormalFrame = 0x40;
            nIconSelectedFrame = 0x42;
            break;
        case 0x35:
            nIconNormalFrame = 0x44;
            nIconSelectedFrame = 0x46;
            break;
        case 0x36:
            nIconNormalFrame = 0x48;
            nIconSelectedFrame = 0x4A;
            break;
        case 0x37:
            nIconNormalFrame = 0x4C;
            nIconSelectedFrame = 0x4E;
            break;
        case 0x38:
            nIconNormalFrame = 0x50;
            nIconSelectedFrame = 0x52;
            break;
        case 0x39:
            nIconNormalFrame = 0x54;
            nIconSelectedFrame = 0x56;
            break;
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43: {
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->GetQuickWeapon(static_cast<BYTE>(m_buttonTypes[nButton] - 0x3C), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconNormalFrame = 0;
                nIconSelectedFrame = 0;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                nIconNormalFrame = 0x68;
                nIconSelectedFrame = 0x6A;
            }
            break;
        }
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4A:
        case 0x4B:
        case 0x4C:
        case 0x4D:
        case 0x4E: {
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->GetQuickSpell(static_cast<BYTE>(m_buttonTypes[nButton] - 0x46), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconNormalFrame = 0;
                nIconSelectedFrame = 0;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                bActive = FALSE;
                cIconResRef = CResRef("");
            }
            break;
        }
        case 0x50:
        case 0x51:
        case 0x52: {
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->GetQuickItem(static_cast<BYTE>(m_buttonTypes[nButton] - 0x50), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconNormalFrame = 0;
                nIconSelectedFrame = 0;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                bActive = FALSE;
                cIconResRef = CResRef("");
            }
            break;
        }
        case 0x5A:
        case 0x5B:
        case 0x5C:
        case 0x5D:
        case 0x5E:
        case 0x5F:
        case 0x60:
        case 0x61:
        case 0x62: {
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->GetQuickAbility(static_cast<BYTE>(m_buttonTypes[nButton] - 0x5A), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconNormalFrame = 0;
                nIconSelectedFrame = 0;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                bActive = FALSE;
                cIconResRef = CResRef("");
            }
            break;
        }
        case 0x6E:
        case 0x6F:
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76: {
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->GetQuickSong(static_cast<BYTE>(m_buttonTypes[nButton] - 0x6E), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconNormalFrame = 0;
                nIconSelectedFrame = 0;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                nIconNormalFrame = 0x14;
                nIconSelectedFrame = 0x16;
            }
            break;
        }
        case 100:
        default:
            bActive = FALSE;
            bEnabled = FALSE;
            cIconResRef = CResRef("");
            break;
        }

        settings.field_0 = bActive ? 1 : 0;
        settings.field_4 = bActive ? 1 : 0;
        settings.field_8 = 1;
        settings.field_C = nIconNormalFrame;
        settings.field_10 = nIconSelectedFrame;
        settings.field_1C8 = 0;
        settings.field_1D0 = 0;
        settings.field_1D8 = 0;
        settings.m_bGreyOut = !bEnabled || bGreyOut;
        settings.field_14.SetResRef(cIconResRef, pPanel->m_pManager->m_bDoubleSize, TRUE, TRUE);
        settings.field_14.SequenceSet(0);
        if (nIconNormalFrame >= 0) {
            settings.field_14.FrameSet(nIconNormalFrame);
        }
        settings.field_EE.SetResRef(CResRef(""), pPanel->m_pManager->m_bDoubleSize, FALSE, FALSE);

        // CHUI draws the stone slot.  RenderButton overlays the action icon.
        pButton->m_nNormalFrame = static_cast<SHORT>(nButton * 2);
        pButton->m_nPressedFrame = static_cast<SHORT>(nButton * 2 + 1);
        pButton->m_nDisabledFrame = static_cast<SHORT>(nButton * 2);
        pButton->m_cVidCell.SetResRef(CResRef("GUIBTBUT"), pPanel->m_pManager->m_bDoubleSize, TRUE, TRUE);
        pButton->m_cVidCell.SequenceSet(0);
        pButton->m_cVidCell.FrameSet(pButton->m_nNormalFrame);
        pButton->SetToolTipStrRef(nToolTip, -1, -1);
        pButton->SetToolTipHotKey(nHotKey, 0xFFFF, CString(""));
        // Keep empty/custom slots mouse-active: original permits right-click
        // customization even when no action icon is assigned.
        pButton->SetEnabled(TRUE);
        pButton->InvalidateRect();
    }

    if (rc == CGameObjectArray::SUCCESS) {
        pGame->GetObjectArray()->ReleaseShare(nLeaderId,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }

    pPanel->InvalidateRect(NULL);
}

// 0x5950F0 / 0x5957C0
BOOL CInfButtonArray::RenderButton(CPoint pt, const CRect& rClip, BOOL bPressed, INT nButton)
{
    // TODO: Incomplete.

    if (nButton < 0 || nButton >= 12) {
        return TRUE;
    }

    CInfButtonSettings& settings = m_buttonArray[nButton];
    if (settings.field_0 == 0 || settings.field_14.pRes == NULL) {
        return TRUE;
    }

    INT nScale = g_pBaldurChitin->field_4A2C != 0 ? 2 : 1;
    CPoint ptIcon(pt.x + 3 * nScale, pt.y + 3 * nScale);

    SHORT nFrame = settings.field_C;
    if (settings.field_1CC != 0 && settings.field_10 >= 0) {
        nFrame = settings.field_10;
    }
    if (bPressed && nFrame >= 0 && settings.field_C != settings.field_10) {
        nFrame++;
    }

    if (nFrame >= 0) {
        settings.field_14.SequenceSet(0);
        settings.field_14.FrameSet(nFrame);
    }

    DWORD dwFlags = settings.m_bGreyOut ? 0xA0000 : 0;
    settings.field_14.Render(0, ptIcon.x, ptIcon.y, rClip, NULL, 0, dwFlags, -1);

    return TRUE;
}

// 0x595E70
void CInfButtonArray::SetCustomButtonTypes(const INT* pButtonList)
{
    // TODO: Incomplete.

    if (pButtonList == NULL) {
        return;
    }

    for (INT nButton = 0; nButton < 9; nButton++) {
        m_customButtonTypes[nButton] = pButtonList[nButton];
    }
}

// 0x595F70
void CInfButtonArray::SetQuickWeaponSlot(BYTE nSlot)
{
    // TODO: Incomplete.

    if (nSlot > 3) {
        nSlot = 0;
    }

    m_nQuickWeaponSlot = nSlot;
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

    if (buttonID < 0 || buttonID >= 12 || m_buttonArray[buttonID].m_bGreyOut) {
        return;
    }

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    INT nButtonType = m_buttonTypes[buttonID];

    switch (m_nState) {
    case 0x6C:
    case 0x6D:
        if (buttonID < 12) {
            pGame->GetGameSave()->m_curFormation = static_cast<SHORT>(buttonID);
            SetState(0x6E, 0);
        }
        return;
    case 0x6E:
        switch (nButtonType) {
        case 7:
            if (pGame->GetState() == 3) {
                pGame->SetState(0);
                SetSelectedButton(100);
            } else {
                pGame->SetState(3);
                SetSelectedButton(7);
            }
            UpdateButtons();
            return;
        case 8:
            if (pGame->GetState() == 2 && pGame->GetIconIndex() == 0x0C) {
                pGame->SetState(0);
                SetSelectedButton(100);
            } else {
                pGame->SetState(2);
                pGame->SetIconIndex(0x0C);
                SetSelectedButton(8);
            }
            UpdateButtons();
            return;
        case 0x0F:
            pGame->SetState(0);
            SetSelectedButton(100);
            pGame->GetGroup()->ClearActions();
            UpdateButtons();
            return;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14: {
            INT nFormationButton = nButtonType - 0x10;
            pGame->GetGameSave()->m_curFormation = pGame->GetGameSave()->m_quickFormations[nFormationButton];
            SetState(0x6E, 0);
            return;
        }
        default:
            break;
        }
        break;
    default:
        switch (nButtonType) {
        case 3:
            // Cast Spell.  Ghidra helper FUN_00594280 routes to spell-class
            // picker 0x76 when more than one class is available; use it as the
            // first recovered step.
            pGame->SetState(0);
            SetSelectedButton(100);
            UpdateButtons();
            SetState(0x76, 1);
            return;
        case 7:
            if (pGame->GetState() == 3) {
                pGame->SetState(0);
                SetSelectedButton(100);
            } else {
                pGame->SetState(3);
                SetSelectedButton(7);
            }
            UpdateButtons();
            return;
        case 8:
            if (pGame->GetState() == 2 && pGame->GetIconIndex() == 0x0C) {
                pGame->SetState(0);
                SetSelectedButton(100);
            } else {
                pGame->SetState(2);
                pGame->SetIconIndex(0x0C);
                SetSelectedButton(8);
            }
            UpdateButtons();
            return;
        case 0x0F:
            pGame->SetState(0);
            SetSelectedButton(100);
            pGame->GetGroup()->ClearActions();
            UpdateButtons();
            return;
        case 0x32:
            m_nCurrentSelectedSpellClass = 2;
            SetState(0x67, 1);
            return;
        case 0x33:
        case 0x39:
            m_nCurrentSelectedSpellClass = 3;
            SetState(0x67, 1);
            return;
        case 0x34:
            m_nCurrentSelectedSpellClass = 4;
            SetState(0x67, 1);
            return;
        case 0x35:
            m_nCurrentSelectedSpellClass = 7;
            SetState(0x67, 1);
            return;
        case 0x36:
            m_nCurrentSelectedSpellClass = 8;
            SetState(0x67, 1);
            return;
        case 0x37:
            m_nCurrentSelectedSpellClass = 10;
            SetState(0x67, 1);
            return;
        case 0x38:
            m_nCurrentSelectedSpellClass = 11;
            SetState(0x67, 1);
            return;
        default:
            break;
        }
        break;
    }
}

// 0x594720
void CInfButtonArray::OnRButtonPressed(int buttonID)
{
    // TODO: Incomplete.

    if (buttonID < 0 || buttonID >= 12) {
        return;
    }

    INT nButtonType = m_buttonTypes[buttonID];

    if (m_nState == 0x6E && nButtonType >= 0x10 && nButtonType <= 0x14) {
        // Configure quick formations.
        SetState(0x6C, 1);
        return;
    }

    if (m_nState == 0x72 && buttonID >= 3) {
        // Main PC custom slot menu.  The follow-up per-choice assignment is
        // still TODO, but this restores the first Ghidra-visible right-click
        // transition to state 0x75.
        SetState(0x75, 1);
        return;
    }
}

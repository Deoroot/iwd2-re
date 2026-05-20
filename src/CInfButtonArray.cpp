#include "CInfButtonArray.h"

#include "CBaldurChitin.h"
#include "CButtonData.h"
#include "CGameButtonList.h"
#include "CGameSave.h"
#include "CGameSprite.h"
#include "CGameSpriteEquipment.h"
#include "CInfGame.h"
#include "CItem.h"
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
    m_nIconSequence = 0;
    m_bSelected = 0;
    m_nCount = 0;
    field_0 = 0;
    m_bActive = 0;
    m_bHasOverlay = 0;
    m_bActiveWeaponSet = 0;
    m_bGreyOut = FALSE;
    m_nIconNormalFrame = -1;
    m_nIconSelectedFrame = -1;
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

    m_nCustomizeSlot = 0;
    m_nSelectedButton = 100;
    m_nState = STATE_NONE;
    memset(field_1986, 0, sizeof(field_1986));
    m_nStateStackDepth = 0;

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
    m_pPickerList = NULL;
    m_nPickerPage = 0;
}

void CInfButtonArray::ClearPickerList()
{
    if (m_pPickerList != NULL) {
        while (!m_pPickerList->IsEmpty()) {
            delete m_pPickerList->RemoveHead();
        }
        delete m_pPickerList;
        m_pPickerList = NULL;
    }
    m_nPickerPage = 0;
}

// Builds the picker list for the current state.  Called from SetState when
// entering 0x66/0x67/0x68/0x69/0x6A/0x6B/0x70/0x71/0x7A.  Mirrors the
// per-type dispatch in Ghidra FUN_00587c20.  Currently implements type 2
// (spellbook) only; the other branches still return an empty list.
void CInfButtonArray::RebuildPickerList()
{
    ClearPickerList();

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->GetGroup()->GetCount() == 0) {
        return;
    }

    LONG nLeader = pGame->GetGroup()->GetGroupLeader();
    CGameSprite* pSprite = NULL;
    BYTE rc;
    do {
        rc = pGame->GetObjectArray()->GetShare(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS || pSprite == NULL) {
        return;
    }

    switch (m_nState) {
    case 0x65:
        // Weapon-equip picker.  Ghidra SetState 0x65 passes
        // (m_nCurrentSelectedSpellLevel + 0x2B) as slot index to FUN_00587c20
        // case 1 â†’ GetItemUsages(slot, 1, -1).  Slot 0x2B = 43 is
        // SLOT_WEAPON; the offset converts the quick-weapon button index
        // (0..7) into the actual inventory slot (43..50).
        m_pPickerList = pSprite->GetItemUsages(
            static_cast<SHORT>(m_nCurrentSelectedSpellLevel + CGameSpriteEquipment::SLOT_WEAPON),
            1,
            -1);
        break;
    case 0x66:
    case 0x67:
        // Spellbook.  Matches Ghidra FUN_00587c20 case 2 dispatch:
        //   class == 3 && level != 0 â†’ domain spells (FUN_007155c0)
        //   otherwise              â†’ regular class spells (FUN_00714f70)
        if (m_nCurrentSelectedSpellClass == 3 && m_nCurrentSelectedSpellLevel != 0) {
            m_pPickerList = pSprite->GetDomainSpellsButtonList();
        } else {
            m_pPickerList = pSprite->GetSpellsButtonList(m_nCurrentSelectedSpellClass);
        }
        break;
    case 0x68:
    case 0x69:
        // Item-ability picker.  Ghidra FUN_00587c20 case 3 with non-zero
        // alt-flag â†’ GetItemUsages(slot + 0xF, 3, -1).  Slot 0xF = 15 is
        // SLOT_MISC; the offset converts the quick-item button index
        // (0..2) into the inventory slot (15..17).
        m_pPickerList = pSprite->GetItemUsages(
            static_cast<SHORT>(m_nCurrentSelectedSpellLevel + CGameSpriteEquipment::SLOT_MISC),
            3,
            -1);
        break;
    case 0x70:
    case 0x71:
    case 0x7A:
        m_pPickerList = pSprite->GetSongsButtonList();
        break;
    case 0x6A:
    case 0x6B:
        m_pPickerList = pSprite->GetInternalButtonList();
        break;
    default:
        break;
    }

    pGame->GetObjectArray()->ReleaseShare(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);
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

    // Clear any picker list when transitioning out of a picker state.  The
    // picker-case branch below will rebuild it if the new state is itself
    // a picker.
    BOOL bIsPickerState = nState == 0x65 || nState == 0x66 || nState == 0x67
        || nState == 0x68 || nState == 0x69 || nState == 0x6A || nState == 0x6B
        || nState == 0x70 || nState == 0x71 || nState == 0x7A || nState == 0x7B;
    if (!bIsPickerState) {
        ClearPickerList();
    }

    switch (nState) {
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6A:
    case 0x6B:
    case 0x70:
    case 0x71:
    case 0x7A:
    case 0x7B: {
        // Picker states (weapon / spell / item / ability / song).  Per Ghidra
        // SetState (0x589110) + FUN_00587c20: build the dynamic list of
        // available entries.  When N â‰¤ 12 the slots use formation-submenu
        // types 0x15..0x20 (entry = buttonType - 0x15).  When N > 12 the
        // layout switches to paging buttons: slot 0 = 0x21 (page-up arrow),
        // slots 1..10 = 0x15..0x1E (entries), slot 11 = 0x22 (page-down).
        // m_nPickerPage holds the current page offset (in units of 10
        // entries).
        m_nState = nState;
        RebuildPickerList();
        m_nPickerPage = 0;
        INT nEntries = (m_pPickerList != NULL)
            ? static_cast<INT>(m_pPickerList->GetCount())
            : 0;
        if (nEntries > 12) {
            // Paging layout â€” slot 0 = page up, slots 1-10 = entries
            // (filled by UpdateButtons via m_nPickerPage), slot 11 = page
            // down.  Type 0x21 / 0x22 already in UpdateButtons.
            m_buttonTypes[0] = 0x21;
            for (INT nButton = 1; nButton < 11; nButton++) {
                m_buttonTypes[nButton] = 0x15 + (nButton - 1);
            }
            m_buttonTypes[11] = 0x22;
        } else {
            for (INT nButton = 0; nButton < 12; nButton++) {
                if (nButton < nEntries) {
                    m_buttonTypes[nButton] = 0x15 + nButton;
                } else {
                    m_buttonTypes[nButton] = 100;
                }
            }
        }
        UpdateButtons();
        return TRUE;
    }
    case 0x73:
    case 0x74:
        // Action submenu (Stealth / Berserk / Turn / Weapon flip / Trapfind).
        m_buttonTypes[0] = 0x0B;
        m_buttonTypes[1] = 4;
        m_buttonTypes[2] = 0x0C;
        m_buttonTypes[3] = 0x77;
        m_buttonTypes[4] = 0x0D;
        for (INT nButton = 5; nButton < 12; nButton++) {
            m_buttonTypes[nButton] = 100;
        }
        m_nState = nState;
        UpdateButtons();
        return TRUE;
    case 0x78:
        // Quick-item picker (3 slots + empty).
        m_buttonTypes[0] = 0x50;
        m_buttonTypes[1] = 0x51;
        m_buttonTypes[2] = 0x52;
        for (INT nButton = 3; nButton < 12; nButton++) {
            m_buttonTypes[nButton] = 100;
        }
        m_nState = nState;
        UpdateButtons();
        return TRUE;
    case 0x79:
        // Quick-weapon picker â€” 4 weapon-set rows, each (main, off, empty).
        m_buttonTypes[0] = 0x3C;
        m_buttonTypes[1] = 0x3D;
        m_buttonTypes[2] = 100;
        m_buttonTypes[3] = 0x3E;
        m_buttonTypes[4] = 0x3F;
        m_buttonTypes[5] = 100;
        m_buttonTypes[6] = 0x40;
        m_buttonTypes[7] = 0x41;
        m_buttonTypes[8] = 100;
        m_buttonTypes[9] = 0x42;
        m_buttonTypes[10] = 0x43;
        m_buttonTypes[11] = 100;
        m_nState = nState;
        UpdateButtons();
        return TRUE;
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
    case 0x6F:
        // Empty action bar (used while certain modes are transitioning).
        for (INT nButton = 0; nButton < 12; nButton++) {
            m_buttonTypes[nButton] = 100;
        }
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
    BYTE rc = 0xFF;
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
        SHORT nIconSequence = 0;
        STRREF nToolTip = -1;
        USHORT nHotKey = 0xFFFF;
        BOOL bEnabled = TRUE;
        BOOL bActive = TRUE;
        BOOL bGreyOut = FALSE;
        BOOL bActiveWeaponSet = FALSE;
        SHORT nCount = 0;
        // m_bHasOverlay selects the render path in CUIControlButtonAction::Render:
        //   1 = GUIBTACT-style overlay (Protect/Attack/Cast etc.) â€” the BAM
        //       bakes its own stone bezel, so CUIControlButton::Render base
        //       must be skipped.
        //   0 = STON*-style icon (small silhouette over a regular GUIBTBUT
        //       bezel) â€” base GUIBTBUT must be painted underneath.
        // Original UpdateButtons at 0x58A340 sets m_bHasOverlay per case.
        BOOL bHasOverlay = TRUE;
        // Item icon BAMs (IBLUN/ISHD/SW1H/IPOTN...) carry two cycles:
        //   cycle 0 = inventory icon (large, e.g. 53x43)
        //   cycle 1 = action-bar icon (small, e.g. 26x27)
        // STON*/FORMx/GUIBTACT have one cycle so 0 is the safe default.
        // Item-icon cases below override to 1.

        settings.m_bSelected = m_nSelectedButton == m_buttonTypes[nButton];
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
            // Quick formation slot.  Per Ghidra UpdateButtons (case 0x10-0x14
            // at 0x58A340) m_bHasOverlay = 0: FORMx is a small ~24x22 icon
            // that sits inside a GUIBTBUT bezel, not a full-button overlay.
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
            bHasOverlay = FALSE;
            // Ghidra `piVar8[0x73] = (uint)(piStack_51c == apiStack_4e8[0])`:
            // formation slot's selection highlight is driven by whether its
            // configured formation matches CGameSave::m_curFormation (NOT the
            // generic m_nSelectedButton match).  Override here.
            settings.m_bSelected = (nFormation == g_pBaldurChitin->GetObjectGame()->GetGameSave()->m_curFormation) ? 1 : 0;
            break;
        }
        case 2:
            // Bard song.  Ghidra UpdateButtons case 2 sets m_nIconSequence (=
            // settings.m_nIconSequence) and may set m_bSelected if currently in song
            // modal. Tooltip 0x1336, hotkey 0xA.
            nIconNormalFrame = 0x14;
            nIconSelectedFrame = 0x16;
            nToolTip = 0x1336;
            nHotKey = 0xA;
            break;
        case 3:
            nIconNormalFrame = 8;
            nIconSelectedFrame = 10;
            nToolTip = 0x1250;
            nHotKey = 0xB;
            break;
        case 4:
            // Berserk modal â€” Ghidra case 4 frames 0x24/0x26, tooltip 0x133F.
            nIconNormalFrame = 0x24;
            nIconSelectedFrame = 0x26;
            nToolTip = 0x133F;
            nHotKey = 0x10;
            break;
        case 5:
            // Bard alternate ?  Ghidra case 5 frame 0x60/0x62.
            nIconNormalFrame = 0x60;
            nIconSelectedFrame = 0x62;
            nToolTip = 0x1345;
            nHotKey = 0xD;
            break;
        case 9:
            // Shapeshift â€” frame 0x28, tooltip 0x135E.
            nIconNormalFrame = 0x28;
            nIconSelectedFrame = 0x28;
            nToolTip = 0x135E;
            break;
        case 10:
            nIconNormalFrame = 0x28;
            nIconSelectedFrame = 0x2A;
            nToolTip = 0x135A;
            nHotKey = 0x13;
            break;
        case 0x0B:
            // Stealth / Cleric class spell tab â€” frame 0x1C/0x1E.
            nIconNormalFrame = 0x1C;
            nIconSelectedFrame = 0x1E;
            nToolTip = 0x1368;
            nHotKey = 0xF;
            break;
        case 0x0C:
            // Turn Undead / Mage class spell tab â€” frame 0x18/0x1A.
            nIconNormalFrame = 0x18;
            nIconSelectedFrame = 0x1A;
            nToolTip = 0x136B;
            nHotKey = 0xE;
            break;
        case 0x0D:
            // Trapfinding / Druid class spell tab â€” frame 0x7C/0x7E.
            nIconNormalFrame = 0x7C;
            nIconSelectedFrame = 0x7E;
            nToolTip = 0x136E;
            nHotKey = 0x9;
            break;
        case 0x0E:
            // Quick-spell tab â€” frame 0x10/0x12.
            nIconNormalFrame = 0x10;
            nIconSelectedFrame = 0x12;
            nToolTip = 0x1372;
            nHotKey = 0xC;
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
            // Formation picker sub-grid (state 0x6C / 0x6D) â€” Ghidra sets
            // m_bHasOverlay = 0 here too: small FORMx icon over GUIBTBUT.
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
                bHasOverlay = FALSE;
            } else if (m_pPickerList != NULL) {
                // Picker list entry â€” pull icon + tooltip from the
                // CGameButtonList built in RebuildPickerList.  Two layouts:
                //   * â‰¤ 12 entries: nEntry = buttonType - 0x15 (slot maps
                //     directly to list index).
                //   * > 12 entries (paging): nEntry = page * 10 + (buttonType
                //     - 0x15); slots 0 + 11 hold the 0x21/0x22 arrows and
                //     fall through to their own UpdateButtons cases.
                INT nListCount = static_cast<INT>(m_pPickerList->GetCount());
                INT nEntry;
                if (nListCount > 12) {
                    nEntry = m_nPickerPage * 10 + (m_buttonTypes[nButton] - 0x15);
                } else {
                    nEntry = m_buttonTypes[nButton] - 0x15;
                }
                POSITION pos = (nEntry >= 0 && nEntry < nListCount)
                    ? m_pPickerList->FindIndex(nEntry)
                    : NULL;
                CButtonData* pEntry = (pos != NULL) ? m_pPickerList->GetAt(pos) : NULL;
                if (pEntry != NULL && pEntry->m_icon != "") {
                    cIconResRef = pEntry->m_icon;
                    nIconNormalFrame = 0;
                    nIconSelectedFrame = 0;
                    nIconSequence = 1;  // item-style icon BAM
                    nToolTip = pEntry->m_name;
                    bGreyOut = pEntry->m_bDisabled;
                    bHasOverlay = FALSE;
                    if (pEntry->m_bDisplayCount) {
                        nCount = pEntry->m_count;
                    }
                } else {
                    bActive = FALSE;
                    bEnabled = FALSE;
                    cIconResRef = CResRef("");
                }
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
        case 0x21:
            // Page-up arrow (submenu paging) â€” frame 0x30.
            nIconNormalFrame = 0x30;
            nIconSelectedFrame = 0x30;
            break;
        case 0x22:
            // Page-down arrow â€” frame 0x34.
            nIconNormalFrame = 0x34;
            nIconSelectedFrame = 0x34;
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
        case 0x77:
            // Quick-weapon flip / "swap weapon set" â€” frame 0x5C/0x5E, tooltip
            // 0x7DBA, hotkey 0x36 ('6').
            nIconNormalFrame = 0x5C;
            nIconSelectedFrame = 0x5E;
            nToolTip = 0x7DBA;
            nHotKey = 0x36;
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
            BYTE nWeaponSlot = static_cast<BYTE>(m_buttonTypes[nButton] - 0x3C);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL && pGame->m_bGameLoaded) {
                pSprite->GetQuickWeapon(nWeaponSlot, buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconNormalFrame = 0;
                nIconSelectedFrame = 0;
                nIconSequence = 1;  // small action-bar cycle
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                // Off-hand slot (odd index) â†’ STONSHIL.  Main hand â†’ STONWEAP.
                cIconResRef = (m_buttonTypes[nButton] & 1) ? CResRef("STONSHIL") : CResRef("STONWEAP");
                nIconNormalFrame = 0;
                nIconSelectedFrame = 0;
                nToolTip = 0x1356;
            }
            nHotKey = static_cast<USHORT>(0x19 + (m_buttonTypes[nButton] - 0x3C));
            // Green border: this slot matches the sprite's currently-active
            // weapon set (sprite.m_quickWeaponSet stores the set index, each
            // set occupies a main+off pair).  Drives settings.m_bActiveWeaponSet.
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL
                && static_cast<BYTE>(nWeaponSlot >> 1) == pSprite->m_nWeaponSet) {
                bActiveWeaponSet = TRUE;
            }
            bHasOverlay = FALSE;
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
            // Quick spell.  Empty slot keeps STONSPEL stone visible so the user
            // can right-click to assign â€” original UpdateButtons at 0x58A340
            // never marks these inactive.  Ghidra default tooltip 0x1250 ("Cast
            // Spell").
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL && pGame->m_bGameLoaded) {
                pSprite->GetQuickSpell(static_cast<BYTE>(m_buttonTypes[nButton] - 0x46), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconSequence = 1;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
                if (buttonData.m_bDisplayCount) {
                    nCount = buttonData.m_count;
                }
            } else {
                cIconResRef = CResRef("STONSPEL");
                nToolTip = 0x1250;
            }
            nIconNormalFrame = 0;
            nIconSelectedFrame = 0;
            bHasOverlay = FALSE;
            break;
        }
        case 0x50:
        case 0x51:
        case 0x52: {
            // Quick item.  STONITEM fallback; tooltip 0x1372.
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL && pGame->m_bGameLoaded) {
                pSprite->GetQuickItem(static_cast<BYTE>(m_buttonTypes[nButton] - 0x50), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconSequence = 1;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
                if (buttonData.m_bDisplayCount) {
                    nCount = buttonData.m_count;
                }
            } else {
                cIconResRef = CResRef("STONITEM");
                nToolTip = 0x1372;
            }
            nIconNormalFrame = 0;
            nIconSelectedFrame = 0;
            nHotKey = static_cast<USHORT>(0x2D + (m_buttonTypes[nButton] - 0x50));
            bHasOverlay = FALSE;
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
            // Quick ability (innate / feat / special).  STONSPEC fallback;
            // tooltip 0x135A ("Special Abilities").
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL && pGame->m_bGameLoaded) {
                pSprite->GetQuickAbility(static_cast<BYTE>(m_buttonTypes[nButton] - 0x5A), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconSequence = 1;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
                if (buttonData.m_bDisplayCount) {
                    nCount = buttonData.m_count;
                }
            } else {
                cIconResRef = CResRef("STONSPEC");
                nToolTip = 0x135A;
            }
            nIconNormalFrame = 0;
            nIconSelectedFrame = 0;
            bHasOverlay = FALSE;
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
            // Quick song (bard).  STONSONG fallback; tooltip 0x923C.
            CButtonData buttonData;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL && pGame->m_bGameLoaded) {
                pSprite->GetQuickSong(static_cast<BYTE>(m_buttonTypes[nButton] - 0x6E), buttonData);
            }
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nIconSequence = 1;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                cIconResRef = CResRef("STONSONG");
                nToolTip = 0x923C;
            }
            nIconNormalFrame = 0;
            nIconSelectedFrame = 0;
            bHasOverlay = FALSE;
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
        settings.m_bActive = bActive ? 1 : 0;
        settings.m_bHasOverlay = bHasOverlay ? 1 : 0;
        settings.m_nIconNormalFrame = nIconNormalFrame;
        settings.m_nIconSelectedFrame = nIconSelectedFrame;
        settings.m_bActiveWeaponSet = bActiveWeaponSet ? 1 : 0;
        settings.m_nCount = nCount;
        settings.m_bGreyOut = !bEnabled || bGreyOut;
        settings.m_iconCell.SetResRef(cIconResRef, pPanel->m_pManager->m_bDoubleSize, TRUE, TRUE);
        settings.m_iconCell.SequenceSet(nIconSequence);
        if (nIconNormalFrame >= 0) {
            settings.m_iconCell.FrameSet(nIconNormalFrame);
        }
        // Stash sequence in m_nIconSequence so RenderButton can re-apply it when
        // swapping between normal/selected frames without resetting to 0.
        settings.m_nIconSequence = nIconSequence;
        settings.m_countCell.SetResRef(CResRef(""), pPanel->m_pManager->m_bDoubleSize, FALSE, FALSE);

        // Selection highlight: GUIBTBUT cycle entries 0x18+ map to frame 2
        // (red border) per the BAM lookup table.  Original RenderButton at
        // 0x5957C0 swaps to these frames when settings.m_bSelected is set
        // (m_nSelectedButton matches this button's type).
        SHORT nNormalFrame = static_cast<SHORT>(nButton * 2);
        SHORT nPressedFrame = static_cast<SHORT>(nButton * 2 + 1);
        if (settings.m_bSelected != 0) {
            nNormalFrame = static_cast<SHORT>(nButton * 2 + 0x18);
            nPressedFrame = static_cast<SHORT>(nButton * 2 + 0x19);
        }

        pButton->m_nNormalFrame = nNormalFrame;
        pButton->m_nPressedFrame = nPressedFrame;
        pButton->m_nDisabledFrame = static_cast<SHORT>(nButton * 2);
        pButton->m_cVidCell.SetResRef(CResRef("GUIBTBUT"), pPanel->m_pManager->m_bDoubleSize, TRUE, TRUE);
        pButton->m_cVidCell.SequenceSet(0);
        pButton->m_cVidCell.FrameSet(pButton->m_nNormalFrame);
        pButton->SetToolTipStrRef(nToolTip, -1, -1);
        pButton->SetToolTipHotKey(nHotKey, 0xFFFF, CString(""));
        // Visibility is driven by settings.field_0: when 0 the original
        // FUN_005957C0 + FUN_005950F0 both no-op, so CUIControlButtonAction::
        // Render checks that to short-circuit. Keep m_bEnabled = TRUE so
        // right-click customization works on slots the user could populate.
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
    if (settings.field_0 == 0 || settings.m_iconCell.pRes == NULL) {
        return TRUE;
    }

    INT nScale = g_pBaldurChitin->field_4A2C != 0 ? 2 : 1;
    // Two render origins per Ghidra:
    //   - FUN_005957C0 (GUIBTACT overlay, m_bHasOverlay != 0): draws at pt+0 with
    //     full 38x38 (scaled) button rect â€” the action BAM bakes its bezel.
    //   - FUN_005950F0 (STON*/item BG, m_bHasOverlay == 0): draws at pt+3 (scaled)
    //     with 32x32 frames that sit inside the GUIBTBUT bezel.
    BOOL bOverlay = settings.m_bHasOverlay != 0;
    CPoint ptIcon = bOverlay ? pt : CPoint(pt.x + 3 * nScale, pt.y + 3 * nScale);

    // Active-weapon-set green ring overlay.  Original FUN_005950F0 loads BAM
    // "HIGHLGHT" inline when settings.m_bActiveWeaponSet != 0 && settings.m_bSelected == 0
    // (the selection-highlight always takes precedence).  We construct the
    // CVidCell inline here too â€” this is a UI render path called only when the
    // bar is visible, so the cost is amortized.
    if (settings.m_bActiveWeaponSet != 0 && settings.m_bSelected == 0) {
        CVidCell cHighlight;
        cHighlight.SetResRef(CResRef("HIGHLGHT"), nScale == 2, TRUE, FALSE);
        cHighlight.SequenceSet(0);
        cHighlight.FrameSet(0);
        cHighlight.Render(0, pt.x + 3 * nScale, pt.y + 3 * nScale, rClip, NULL, 0, 0, -1);
    }

    SHORT nFrame = settings.m_nIconNormalFrame;
    if (settings.m_bSelected != 0 && settings.m_nIconSelectedFrame >= 0) {
        nFrame = settings.m_nIconSelectedFrame;
    }
    if (bPressed && nFrame >= 0 && settings.m_nIconNormalFrame != settings.m_nIconSelectedFrame) {
        nFrame++;
    }

    if (nFrame >= 0) {
        settings.m_iconCell.SequenceSet(static_cast<SHORT>(settings.m_nIconSequence));
        settings.m_iconCell.FrameSet(nFrame);
    }

    DWORD dwFlags = settings.m_bGreyOut ? 0xA0000 : 0;
    settings.m_iconCell.Render(0, ptIcon.x, ptIcon.y, rClip, NULL, 0, dwFlags, -1);

    // Memorize / charge count badge.  Matches Ghidra FUN_005950F0 spell &
    // ability branch and the count-render loop inside CIcon::RenderIcon
    // (0x4E66E0): right-justified digits drawn at LAST_DIGIT_OFFSET (25,25
    // scaled), each digit a frame in NUMBER.BAM, walking left while the
    // remainder is non-zero.  Only quick spells / abilities / songs and
    // picker entries set m_nCount > 0; everyone else gets 0 and skips.
    if (settings.m_nCount > 0) {
        CVidCell cNumber;
        cNumber.SetResRef(CResRef("NUMBER"), nScale == 2, TRUE, TRUE);
        if (cNumber.pRes != NULL) {
            cNumber.SequenceSet(0);
            LONG x = pt.x + 25 * nScale;
            LONG y = pt.y + 25 * nScale;
            WORD wRemaining = static_cast<WORD>(settings.m_nCount);
            do {
                SHORT digit = static_cast<SHORT>(wRemaining % 10);
                wRemaining = wRemaining / 10;
                if (digit > 0 || wRemaining > 0) {
                    cNumber.FrameSet(digit);
                    cNumber.Render(0, x, y, rClip, NULL, 0, 0, -1);
                }
                x -= 5 * nScale;
            } while (wRemaining > 0);
        }
    }

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

    if (buttonID < 0 || buttonID >= 12) {
        return;
    }

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    INT nButtonType = m_buttonTypes[buttonID];

    // Greyout gate per Ghidra OnLButtonPressed entry: a greyed-out slot
    // only blocks the click in state 0x72 (action bar) and in state 0x66
    // for non-picker button types.  In every other submenu state the
    // click must reach the handler â€” its default case is what pops the
    // submenu back to the action bar when the user clicks an empty slot.
    if (m_buttonArray[buttonID].m_bGreyOut) {
        if (m_nState == 0x72) {
            return;
        }
        if (m_nState == 0x66 && (nButtonType < 0x15 || nButtonType > 0x20)) {
            return;
        }
    }

    switch (m_nState) {
    case 0x6C:
    case 0x6D:
        if (buttonID < 12) {
            pGame->GetGameSave()->m_curFormation = static_cast<SHORT>(buttonID);
            SetState(0x6E, 0);
        }
        return;
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6A:
    case 0x6B:
    case 0x70:
    case 0x71:
    case 0x7A:
    case 0x7B:
        // Page-up / page-down clicks â€” adjust m_nPickerPage and re-render
        // without changing the state.  Ghidra OnLButton state 0x66/0x67
        // case 0x21 / 0x22 do the same bounds-checked increment.
        if (nButtonType == 0x21) {
            if (m_nPickerPage > 0) {
                m_nPickerPage--;
                UpdateButtons();
            }
            return;
        }
        if (nButtonType == 0x22) {
            INT nMax = (m_pPickerList != NULL)
                ? (static_cast<INT>(m_pPickerList->GetCount()) - 10) / 10 + 1
                : 0;
            if (m_nPickerPage + 1 <= nMax) {
                m_nPickerPage++;
                UpdateButtons();
            }
            return;
        }
        // Picker click â€” dispatch the selected entry via the appropriate
        // CGameSprite method.  Matches Ghidra OnLButtonPressed state
        // 0x66/0x67/0x68/0x69/0x70/0x71/0x7A switch:
        //   0x66 / 0x67           â†’ FUN_005886a0 â†’ UseButtonAction
        //   0x68 / 0x69           â†’ FUN_005884b0 â†’ ReadyOffInternalList
        //   0x70 / 0x71 / 0x7A    â†’ FUN_00588820 (song play, AI action)
        //   0x6A / 0x6B (default) â†’ FUN_00588760 (innate use)
        // We don't yet have ports of the song-play / innate-use helpers, so
        // those states fall back to UseButtonAction which is the underlying
        // generic dispatcher used by all four wrappers in the binary.
        if (nButtonType >= 0x15 && nButtonType <= 0x20 && m_pPickerList != NULL) {
            INT nListCount = static_cast<INT>(m_pPickerList->GetCount());
            INT nEntry = (nListCount > 12)
                ? (m_nPickerPage * 10 + (nButtonType - 0x15))
                : (nButtonType - 0x15);
            POSITION pos = (nEntry >= 0 && nEntry < nListCount)
                ? m_pPickerList->FindIndex(nEntry)
                : NULL;
            CButtonData* pEntry = (pos != NULL) ? m_pPickerList->GetAt(pos) : NULL;
            if (pEntry != NULL && !pEntry->m_bDisabled) {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc;
                do {
                    rc = pGame->GetObjectArray()->GetDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        reinterpret_cast<CGameObject**>(&pSprite),
                        INFINITE);
                } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    // States 0x66 / 0x68 / 0x71 are "customise" variants â€” the
                    // user picked a target to ASSIGN to a quick slot rather
                    // than to fire.  Ghidra calls FUN_00588cb0 in those
                    // states to copy buttonData into m_quickSpells/Items/Songs
                    // and update m_customButtonTypes.  TODO: port the full
                    // save+update path; for now record the resref so the
                    // settings round-trip and skip the dispatch.
                    BOOL bCustomize = (m_nState == 0x66 || m_nState == 0x68 || m_nState == 0x71);
                    if (bCustomize) {
                        INT nSlot = m_nCustomizeSlot;
                        if (nSlot >= 0 && nSlot < 9) {
                            switch (m_nState) {
                            case 0x66:
                                pSprite->SetQuickSpell(static_cast<BYTE>(nSlot), *pEntry);
                                m_customButtonTypes[nSlot] = nSlot + 0x46;
                                break;
                            case 0x68:
                                pSprite->SetQuickItem(static_cast<BYTE>(nSlot), *pEntry);
                                m_customButtonTypes[nSlot] = nSlot + 0x50;
                                break;
                            case 0x71:
                                pSprite->SetQuickSong(static_cast<BYTE>(nSlot), *pEntry);
                                m_customButtonTypes[nSlot] = nSlot + 0x6E;
                                break;
                            }
                        }
                    } else if (m_nState == 0x68 || m_nState == 0x69) {
                        pSprite->ReadyOffInternalList(*pEntry, 0);
                    } else if (m_nState == 0x6A || m_nState == 0x6B) {
                        pSprite->UseButtonItem(*pEntry, 0);
                    } else {
                        // Spells (0x67), songs (0x70/0x7A) and the 0x7B
                        // catch-all all funnel into the generic AI action
                        // dispatcher.
                        pSprite->UseButtonAction(*pEntry, 0);
                    }
                    pGame->GetObjectArray()->ReleaseDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                m_currentAbilityResRef = pEntry->m_abilityId.m_res;
            }
        }
        pGame->SetState(0);
        SetSelectedButton(100);
        SetState(0x72, 0);
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
    case 0x75:
        // Customize menu â€” left click writes the chosen button type into
        // m_customButtonTypes[m_nCustomizeSlot] and mirrors it onto the
        // sprite via SetCustomButtonValue.  Per Ghidra OnLButtonPressed
        // state 0x75 (FUN_0058FF20 around 0x592b14).  No state change after
        // write â€” the menu stays open and UpdateButtons re-paints the slot
        // with the new icon.  Slot index was stashed by OnRButtonPressed
        // state 0x72 customize-entry.
        {
            INT nNewType = -1;
            switch (nButtonType) {
            case 0x23: nNewType = 5;   break; // Attack
            case 0x24: nNewType = 3;   break; // Cast Spell
            case 0x25: nNewType = 0xE; break;
            case 0x26:
                SetState(0x78, 1);
                return;
            case 0x27: nNewType = 10;  break; // Innate ability
            case 0x28: nNewType = 2;   break; // Bard Song
            case 0x29: nNewType = 100; break; // No action
            case 0x2A: {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc;
                do {
                    rc = pGame->GetObjectArray()->GetDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        reinterpret_cast<CGameObject**>(&pSprite),
                        INFINITE);
                } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    for (BYTE i = 0; i < 9; i++) {
                        pSprite->SetCustomButtonValue(i, 0);
                    }
                    pSprite->ResetQuickSlots();
                    for (BYTE i = 0; i < 9; i++) {
                        m_customButtonTypes[i] = pSprite->GetCustomButtonValue(i);
                    }
                    pGame->GetObjectArray()->ReleaseDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                SetState(0x72, 0);
                return;
            }
            default:
                // Empty / unknown click in customize menu â€” pop back to
                // the action bar like every other submenu default exit.
                SetState(0x72, 0);
                return;
            }

            if (m_nCustomizeSlot >= 0 && m_nCustomizeSlot < 9) {
                m_customButtonTypes[m_nCustomizeSlot] = nNewType;
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc;
                do {
                    rc = pGame->GetObjectArray()->GetDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        reinterpret_cast<CGameObject**>(&pSprite),
                        INFINITE);
                } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nNewType);
                    pGame->GetObjectArray()->ReleaseDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
            }
            // Return to single-PC action bar (state 0x72) so the new icon
            // shows and the menu closes.  Ghidra uses a saved-state stack
            // (FUN_00589ff0 + m_nStateStackDepth) that pops the prior state; we
            // hardcode 0x72 because customize entry comes from 0x72.
            SetState(0x72, 0);
        }
        return;
    case 0x73:
        // Skills submenu (entered from state 0x72 button 5).  Per Ghidra
        // OnLButtonPressed state 0x73: clicking Stealth/Berserk/Turn/Trap/
        // Weapon-flip toggles the corresponding modal on the sprite, then
        // pops back to state 0x72 via the state-stack mechanism.  Any
        // other click (empty slot, unknown type) just exits the submenu.
        {
            LONG nLeader = pGame->GetGroup()->GetGroupLeader();
            CGameSprite* pSprite = NULL;
            BYTE rc;
            do {
                rc = pGame->GetObjectArray()->GetDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
            } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                BYTE modal = pSprite->GetModalState();
                switch (nButtonType) {
                case 4: // Berserk
                    if (modal == 2) {
                        pSprite->SetModalState(0, 0);
                        SetSelectedButton(100);
                    } else {
                        pSprite->SetModalState(2, 0);
                        SetSelectedButton(5);
                    }
                    break;
                case 0xB: // Stealth
                    if (modal == 3) {
                        pSprite->SetModalState(0, 0);
                        SetSelectedButton(100);
                    } else {
                        pSprite->SetModalState(3, 0);
                        SetSelectedButton(5);
                    }
                    break;
                case 0xC: // Turn Undead
                    if (pGame->GetState() == 2
                        && (pGame->GetIconIndex() == 0x24 || pGame->GetIconIndex() == 0x28)) {
                        pGame->SetState(0);
                        SetSelectedButton(100);
                    } else {
                        pGame->SetState(2);
                        pGame->SetIconIndex(0x24);
                        SetSelectedButton(0xC);
                    }
                    break;
                case 0xD: // Detect Traps
                    if (modal == 4) {
                        pSprite->SetModalState(0, 0);
                        SetSelectedButton(100);
                    } else {
                        pSprite->SetModalState(4, 0);
                        SetSelectedButton(5);
                    }
                    break;
                case 0x77: { // Weapon flip
                    BYTE nNext = static_cast<BYTE>((pSprite->m_nWeaponSet + 1) & 0x3);
                    pSprite->SetWeaponSet(nNext);
                    m_nQuickWeaponSlot = nNext;
                    break;
                }
                default:
                    break;
                }
                pGame->GetObjectArray()->ReleaseDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
            SetState(0x72, 0);
        }
        return;
    case 0x74:
        // Skills submenu in customize mode (entered from state 0x75 case
        // 0x23 right-click).  Click writes the chosen button type into
        // m_customButtonTypes[m_nCustomizeSlot] and the sprite mirror,
        // then exits to state 0x72.  Empty / unknown clicks just exit.
        if ((nButtonType == 4 || nButtonType == 0xB || nButtonType == 0xC
                || nButtonType == 0xD || nButtonType == 0x77)
            && m_nCustomizeSlot >= 0 && m_nCustomizeSlot < 9) {
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            LONG nLeader = pGame->GetGroup()->GetGroupLeader();
            CGameSprite* pSprite = NULL;
            BYTE rc;
            do {
                rc = pGame->GetObjectArray()->GetDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
            } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
                pGame->GetObjectArray()->ReleaseDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
        }
        SetState(0x72, 0);
        return;
    case 0x77:
        // Customize class picker â€” entered from state 0x75 case 0x24 via
        // OnRButtonPressed.  Per Ghidra OnLButtonPressed state 0x77: each
        // class button (0x32-0x39) writes its own type into the customize
        // slot's m_customButtonTypes entry + sprite mirror, so the action
        // bar slot becomes a "per-class quick spell" launcher.  Other
        // clicks (empty slot, etc.) just pop back to 0x72.
        if (nButtonType >= 0x32 && nButtonType <= 0x39
            && m_nCustomizeSlot >= 0 && m_nCustomizeSlot < 9) {
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            LONG nLeader = pGame->GetGroup()->GetGroupLeader();
            CGameSprite* pSprite = NULL;
            BYTE rc;
            do {
                rc = pGame->GetObjectArray()->GetDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
            } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
                pGame->GetObjectArray()->ReleaseDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
        }
        SetState(0x72, 0);
        return;
    case 0x79:
        // Quick-weapon picker (entered from state 0x72 right-click on a
        // weapon slot).  Per Ghidra OnLButtonPressed state 0x79: a click
        // on a 0x3C-0x43 button calls CGameSprite::SetWeaponSet with the
        // set index (buttonType - 0x3C) / 2, updates the array's tracked
        // quick-weapon slot, then exits to 0x72.  Any other click just
        // exits.
        if (nButtonType >= 0x3C && nButtonType <= 0x43) {
            LONG nLeader = pGame->GetGroup()->GetGroupLeader();
            CGameSprite* pSprite = NULL;
            BYTE rc;
            do {
                rc = pGame->GetObjectArray()->GetDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
            } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                BYTE nNewSet = static_cast<BYTE>((nButtonType - 0x3C) / 2);
                pSprite->SetWeaponSet(nNewSet);
                m_nQuickWeaponSlot = pSprite->m_nWeaponSet;
                pGame->GetObjectArray()->ReleaseDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
        }
        SetState(0x72, 0);
        return;
    case 0x78:
        // Quick-item picker sub-menu reached from state 0x75 case 0x26.
        // Per Ghidra OnLButtonPressed state 0x78: a click on a 0x50-0x52
        // button writes that button type into m_customButtonTypes[slot]
        // (and the sprite mirror) and pops back to the action bar.  Any
        // other click just dismisses the picker.
        if (nButtonType >= 0x50 && nButtonType <= 0x52
            && m_nCustomizeSlot >= 0 && m_nCustomizeSlot < 9) {
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            LONG nLeader = pGame->GetGroup()->GetGroupLeader();
            CGameSprite* pSprite = NULL;
            BYTE rc;
            do {
                rc = pGame->GetObjectArray()->GetDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
            } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
                pGame->GetObjectArray()->ReleaseDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
        }
        SetState(0x72, 0);
        return;
    default:
        switch (nButtonType) {
        case 3:
            // Cast Spell.  Per Ghidra FUN_00594280 case 3: count classes that
            // have memorised spells, then add domain pool to the count when
            // m_domainSpells.m_nHighestLevel != 0.  Counts > 1 â†’ class picker
            // (0x76).  Count == 1 â†’ direct spellbook (0x67) with the only
            // class.  Count == 0 â†’ nothing happens.  This matches the
            // original sorcerer single-class fast-path AND keeps the
            // Cleric/Domain picker visible whenever the cleric has memorised
            // at least one domain spell.
            {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                INT nCasterCount = 0;
                BYTE nOnlyClass = 0;
                BOOL bDomainContributed = FALSE;
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    static const BYTE classes[] = { 2, 3, 4, 7, 8, 10, 11 };
                    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
                        CGameSpriteGroupedSpellList* grouped = pSprite->GetSpells(classes[i]);
                        if (grouped != NULL && grouped->m_nHighestLevel != 0) {
                            nCasterCount++;
                            nOnlyClass = classes[i];
                        }
                    }
                    if (pSprite->m_domainSpells.m_nHighestLevel != 0) {
                        if (nCasterCount == 0) {
                            // Cleric with domain pool only: fall through to
                            // the single-class fast-path with class 3 +
                            // non-zero level so RebuildPickerList picks the
                            // domain branch.
                            nOnlyClass = 3;
                            bDomainContributed = TRUE;
                        }
                        nCasterCount++;
                    }
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                pGame->SetState(0);
                SetSelectedButton(100);
                UpdateButtons();
                if (nCasterCount == 1) {
                    m_nCurrentSelectedSpellClass = nOnlyClass;
                    m_nCurrentSelectedSpellLevel = bDomainContributed ? 1 : 0;
                    SetState(0x67, 1);
                } else if (nCasterCount >= 2) {
                    SetState(0x76, 1);
                }
            }
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
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43: {
            // Quick weapon click â€” Ghidra 0x58FF20 default + button-type fall-
            // through.  Two behaviors: full picker (state 0x65) if the active
            // engine reports "weapon swap allowed", otherwise just select the
            // weapon set and call the equip helper.  We approximate by always
            // doing the lightweight path: if a different button is clicked,
            // make it the selected one; clicking the active one clears the
            // selection.
            INT nEffective = nButtonType;
            // Off-hand collapses to its main when the main is empty or holds a
            // 2H/ranged/sling/crossbow (ITEMTYPE 0x29/0x2F/0x31/0x35).  Ghidra
            // address calc: m_equipment.m_items[(button - 0x3C) + 43] for the
            // odd off-hand slot resolves to the same offset as the main slot.
            if ((nButtonType & 1) != 0) {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    INT nProbeSlot = (nButtonType - 0x3C) + 43;
                    CItem* pMain = pSprite->m_equipment.m_items[nProbeSlot];
                    if (pMain == NULL) {
                        nEffective = nButtonType - 1;
                    } else {
                        WORD itemType = pMain->GetItemType();
                        if (itemType == 0x2F || itemType == 0x35
                            || itemType == 0x31 || itemType == 0x29) {
                            nEffective = nButtonType - 1;
                        }
                    }
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
            }
            if (m_nSelectedButton != nEffective) {
                pGame->SetState(0);
                SetSelectedButton(nEffective);
                UpdateButtons();
            } else {
                pGame->SetState(0);
                SetSelectedButton(100);
                UpdateButtons();
            }
            return;
        }
        case 2:
            // Bard song toggle.  Per Ghidra 0x58FF20 default case 2: read
            // sprite's modal state, if already singing (state 1) clear it,
            // otherwise enter song-select state 0x7A.  Simplified: toggle
            // modal state directly.
            {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    if (pSprite->GetModalState() == 1) {
                        pSprite->SetModalState(0, 0);
                        SetSelectedButton(100);
                    } else {
                        pGame->SetState(0);
                        UpdateButtons();
                        SetState(0x7A, 1);
                    }
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                UpdateButtons();
            }
            return;
        case 4:
            // Berserk modal toggle.
            {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    pSprite->SetModalState(pSprite->GetModalState() == 2 ? 0 : 2, 0);
                    SetSelectedButton(pSprite->GetModalState() == 2 ? 5 : 100);
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                UpdateButtons();
            }
            return;
        case 0xB:
            // Stealth modal toggle.
            {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    if (pSprite->GetModalState() == 3) {
                        pSprite->SetModalState(0, 0);
                        SetSelectedButton(100);
                    } else {
                        pSprite->SetModalState(3, 0);
                        SetSelectedButton(5);
                    }
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                UpdateButtons();
            }
            return;
        case 0xC:
            // Turn Undead modal â€” Ghidra default case 0xC: SetState(2) and
            // IconIndex 0x24 (turn) or toggle off when already in that mode.
            if (pGame->GetState() == 2
                && (pGame->GetIconIndex() == 0x24 || pGame->GetIconIndex() == 0x28)) {
                pGame->SetState(0);
                SetSelectedButton(100);
            } else {
                pGame->SetState(2);
                pGame->SetIconIndex(0x24);
                SetSelectedButton(0xC);
            }
            UpdateButtons();
            return;
        case 5:
            // Skills button.  Per Ghidra FUN_00594280 case 5: if the sprite
            // is already in any modal state, clear it and reset the selected
            // button; otherwise open the skills submenu (state 0x73).  The
            // submenu surfaces Stealth / Berserk / Turn / Weapon flip /
            // Trapfind based on class.
            {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                BYTE modal = 0;
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    modal = pSprite->GetModalState();
                    if (modal != 0) {
                        pSprite->SetModalState(0, 0);
                    }
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                if (modal != 0) {
                    SetSelectedButton(100);
                    UpdateButtons();
                } else {
                    UpdateButtons();
                    SetState(0x73, 1);
                }
            }
            return;
        case 10:
            // Find Traps â€” Ghidra FUN_00594280(10): SetState 0x6A (innate
            // picker).  We route to 0x6A directly; picker state shows
            // placeholder formation buttons until FUN_00587c20 is ported.
            pGame->SetState(0);
            SetSelectedButton(100);
            UpdateButtons();
            SetState(0x6A, 1);
            return;
        case 0xE:
            // Quick-spell tab â€” Ghidra FUN_00594280(0xE): SetState 0x69.
            pGame->SetState(0);
            SetSelectedButton(0xE);
            UpdateButtons();
            SetState(0x69, 1);
            return;
        case 0x6E:
        case 0x6F:
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
            // Quick song click.  Ghidra default case 0x6E-0x76 reads the
            // assigned CButtonData via GetQuickSong, then calls
            // FUN_00588820(buttonData, 1) which dispatches through the AI
            // action table.  UseButtonAction is our generic equivalent â€” it
            // handles spell / item / ability / song uniformly.
            {
                CButtonData buttonData;
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc;
                do {
                    rc = pGame->GetObjectArray()->GetDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        reinterpret_cast<CGameObject**>(&pSprite),
                        INFINITE);
                } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    pSprite->GetQuickSong(static_cast<BYTE>(nButtonType - 0x6E), buttonData);
                    if (buttonData.m_icon != "") {
                        pSprite->UseButtonAction(buttonData, 0);
                    } else if (pSprite->GetModalState() == 1) {
                        // No assigned song â€” toggle out of song-modal mode.
                        pSprite->SetModalState(0, 0);
                    }
                    pGame->GetObjectArray()->ReleaseDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                pGame->SetState(0);
                SetSelectedButton(100);
                UpdateButtons();
            }
            return;
        case 0x77:
            // Weapon flip â€” cycle to next quick-weapon set.  Uses
            // CGameSprite::SetWeaponSet which also updates the sprite's
            // m_selectedWeapon equipment slot and emits the equip animation.
            {
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc;
                do {
                    rc = pGame->GetObjectArray()->GetDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        reinterpret_cast<CGameObject**>(&pSprite),
                        INFINITE);
                } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    BYTE nNext = static_cast<BYTE>((pSprite->m_nWeaponSet + 1) & 0x3);
                    pSprite->SetWeaponSet(nNext);
                    m_nQuickWeaponSlot = nNext;
                    pGame->GetObjectArray()->ReleaseDeny(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                pGame->SetState(0);
                SetSelectedButton(100);
                UpdateState();
            }
            return;
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4A:
        case 0x4B:
        case 0x4C:
        case 0x4D:
        case 0x4E: {
            // Quick spell cast.  Ghidra default case 0x46 (line 858) calls
            // FUN_00588570(slot, 2) which itself invokes ReadySpell(slot, 2, 0)
            // on the leader sprite, then LAB_00592e59: SetState(0) +
            // SetSelectedButton(100).
            LONG nLeader = pGame->GetGroup()->GetGroupLeader();
            CGameSprite* pSprite = NULL;
            BYTE rc = pGame->GetObjectArray()->GetDeny(nLeader,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->ReadySpell(static_cast<SHORT>(nButtonType - 0x46), 2, 0);
                pGame->GetObjectArray()->ReleaseDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
            pGame->SetState(0);
            SetSelectedButton(100);
            UpdateButtons();
            return;
        }
        case 0x50:
        case 0x51:
        case 0x52: {
            // Quick item use â€” FUN_00588570(slot, 3) â†’ ReadyItem.
            LONG nLeader = pGame->GetGroup()->GetGroupLeader();
            CGameSprite* pSprite = NULL;
            BYTE rc = pGame->GetObjectArray()->GetDeny(nLeader,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->ReadyItem(static_cast<SHORT>(nButtonType - 0x50), 0);
                pGame->GetObjectArray()->ReleaseDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
            pGame->SetState(0);
            SetSelectedButton(100);
            UpdateButtons();
            return;
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
            // Quick ability use â€” FUN_00588570(slot, 4) â†’ ReadySpell type 4.
            LONG nLeader = pGame->GetGroup()->GetGroupLeader();
            CGameSprite* pSprite = NULL;
            BYTE rc = pGame->GetObjectArray()->GetDeny(nLeader,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->ReadySpell(static_cast<SHORT>(nButtonType - 0x5A), 4, 0);
                pGame->GetObjectArray()->ReleaseDeny(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
            pGame->SetState(0);
            SetSelectedButton(100);
            UpdateButtons();
            return;
        }
        case 0x32:
            m_nCurrentSelectedSpellClass = 2;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x67, 1);
            return;
        case 0x33:
            // Cleric â€” regular spells (m_nCurrentSelectedSpellLevel = 0
            // signals the picker dispatcher to use m_spells.m_spellsByClass).
            m_nCurrentSelectedSpellClass = 3;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x67, 1);
            return;
        case 0x39:
            // Cleric DOMAIN.  Per Ghidra OnLButton case 0x39:
            //   m_nCurrentSelectedSpellClass = 3
            //   m_nCurrentSelectedSpellLevel = FUN_005940d0() == sprite[0x80C]
            // Any non-zero level acts as the "domain" sentinel for FUN_00587c20
            // case 2's dispatch; we use 1 here since the picker iterates ALL
            // levels of m_domainSpells anyway.
            m_nCurrentSelectedSpellClass = 3;
            m_nCurrentSelectedSpellLevel = 1;
            SetState(0x67, 1);
            return;
        case 0x34:
            m_nCurrentSelectedSpellClass = 4;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x67, 1);
            return;
        case 0x35:
            m_nCurrentSelectedSpellClass = 7;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x67, 1);
            return;
        case 0x36:
            m_nCurrentSelectedSpellClass = 8;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x67, 1);
            return;
        case 0x37:
            m_nCurrentSelectedSpellClass = 10;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x67, 1);
            return;
        case 0x38:
            m_nCurrentSelectedSpellClass = 11;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x67, 1);
            return;
        default:
            // Unhandled button click.  In submenu states (anything other
            // than the main action bar 0x72 or the group bar 0x6E), this
            // is how Ghidra exits â€” empty/unknown slot click pops the
            // state stack back to 0x72 via the default branch.  We don't
            // implement the state stack, so SetState(0x72, 0) directly.
            if (m_nState != 0x72 && m_nState != 0x6E) {
                SetState(0x72, 0);
            }
            return;
        }
        break;
    }
}

// 0x594720
void CInfButtonArray::OnRButtonPressed(int buttonID)
{
    // TODO: Incomplete â€” picker states (0x66/0x71/0x78/0x79) need SetState
    // helpers that build the per-class spell / item / ability lists.  When a
    // sub-list cannot be built, the right-click is currently silent.

    if (buttonID < 0 || buttonID >= 12) {
        return;
    }

    INT nButtonType = m_buttonTypes[buttonID];

    // Empty quick-slot (type 100) renders inactive so its bezel can stay
    // transparent, but right-clicking it in state 0x72 must still bring up
    // the customize menu â€” otherwise erased slots become permanent dead
    // zones.  UpdateButtons forces m_bGreyOut for type 100; bypass that
    // gate for the customize entry path.
    BOOL bCustomizeEntry = (m_nState == 0x72 && nButtonType == 100);
    if (m_buttonArray[buttonID].m_bGreyOut && !bCustomizeEntry) {
        return;
    }

    switch (m_nState) {
    case 0x6E:
        if (nButtonType >= 0x10 && nButtonType <= 0x14) {
            m_nCurrentSelectedSpellLevel = nButtonType - 0x10;
            SetState(0x6C, 1);
        }
        return;
    case 0x72:
        switch (nButtonType) {
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
            // Ghidra also calls CheckWeaponUsability() + sprite[0x4B80] check
            // first.  Both denials produce a feedback string then no state
            // change.  We skip the check and unconditionally enter state 0x79
            // (the quick-weapon picker); the picker itself isn't built yet so
            // this is a no-op until SetState(0x79) lands.
            m_nCurrentSelectedSpellLevel = nButtonType - 0x3C;
            SetState(0x79, 1);
            return;
        case 2:
        case 3:
        case 4:
        case 5:
        case 10:
        case 0xB:
        case 0xC:
        case 0xD:
        case 0xE:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4A:
        case 0x4B:
        case 0x4C:
        case 0x4D:
        case 0x4E:
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x5A:
        case 0x5B:
        case 0x5C:
        case 0x5D:
        case 0x5E:
        case 0x5F:
        case 0x60:
        case 0x61:
        case 0x62:
        case 100:
        case 0x6E:
        case 0x6F:
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
            // Customize the slot.  Ghidra stores (buttonID - 3) at
            // offset 0x1976 so state 0x75 can write back to the right
            // m_customButtonTypes entry.
            m_nCustomizeSlot = buttonID - 3;
            SetState(0x75, 1);
            return;
        }
        return;
    case 0x75:
        switch (nButtonType) {
        case 0x23:
            SetState(0x74, 1);
            return;
        case 0x26:
            SetState(0x78, 1);
            return;
        case 0x27:
            // Innate ability customize â€” Ghidra OnR state 0x75 case 0x27
            // checks sprite[0x4A94] (m_innateSpells internal head pointer)
            // is non-zero before opening the picker.  We use the
            // std::vector emptiness check on m_innateSpells.m_List.
            {
                CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                BOOL bHasInnate = FALSE;
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    bHasInnate = !pSprite->m_innateSpells.m_List.empty();
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                if (bHasInnate) {
                    SetState(0x6B, 1);
                }
            }
            return;
        case 0x28:
            // Bard Song customize â€” Ghidra OnR state 0x75 case 0x28 checks
            // the song list head + (end - head) / 16 (entry size).  We
            // approximate with std::vector emptiness on m_songs.m_List.
            {
                CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                BOOL bHasSong = FALSE;
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    bHasSong = !pSprite->m_songs.m_List.empty();
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                if (bHasSong) {
                    SetState(0x71, 1);
                }
            }
            return;
        case 0x24:
            // Cast Spell â€” pick state 0x66 (customize spellbook) for a
            // single-class caster and state 0x77 (customize-class-picker)
            // for multi-class.  Mirrors Ghidra OnRButtonPressed state 0x75
            // case 0x24 which iterates classes 2/3/4/7/8/10/11 and counts
            // those with memorised spells (plus the cleric domain pool).
            {
                CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
                LONG nLeader = pGame->GetGroup()->GetGroupLeader();
                CGameSprite* pSprite = NULL;
                BYTE rc = pGame->GetObjectArray()->GetShare(nLeader,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&pSprite),
                    INFINITE);
                INT nCasterCount = 0;
                BYTE nOnlyClass = 0;
                BOOL bDomainContributed = FALSE;
                if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                    static const BYTE classes[] = { 2, 3, 4, 7, 8, 10, 11 };
                    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
                        CGameSpriteGroupedSpellList* grouped = pSprite->GetSpells(classes[i]);
                        if (grouped != NULL && grouped->m_nHighestLevel != 0) {
                            nCasterCount++;
                            nOnlyClass = classes[i];
                        }
                    }
                    if (pSprite->m_domainSpells.m_nHighestLevel != 0) {
                        if (nCasterCount == 0) {
                            nOnlyClass = 3;
                            bDomainContributed = TRUE;
                        }
                        nCasterCount++;
                    }
                    pGame->GetObjectArray()->ReleaseShare(nLeader,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
                if (nCasterCount == 1) {
                    m_nCurrentSelectedSpellClass = nOnlyClass;
                    m_nCurrentSelectedSpellLevel = bDomainContributed ? 1 : 0;
                    SetState(0x66, 1);
                } else if (nCasterCount >= 2) {
                    SetState(0x77, 1);
                }
            }
            return;
        }
        return;
    case 0x77:
        switch (nButtonType) {
        case 0x32:
            m_nCurrentSelectedSpellClass = 2;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x66, 1);
            return;
        case 0x33:
            m_nCurrentSelectedSpellClass = 3;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x66, 1);
            return;
        case 0x34:
            m_nCurrentSelectedSpellClass = 4;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x66, 1);
            return;
        case 0x35:
            m_nCurrentSelectedSpellClass = 7;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x66, 1);
            return;
        case 0x36:
            m_nCurrentSelectedSpellClass = 8;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x66, 1);
            return;
        case 0x37:
            m_nCurrentSelectedSpellClass = 10;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x66, 1);
            return;
        case 0x38:
            m_nCurrentSelectedSpellClass = 11;
            m_nCurrentSelectedSpellLevel = 0;
            SetState(0x66, 1);
            return;
        case 0x39:
            m_nCurrentSelectedSpellClass = 3;
            SetState(0x66, 1);
            return;
        }
        return;
    }
}

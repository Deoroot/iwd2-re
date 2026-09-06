#include "CInfButtonArray.h"

#include "CBaldurChitin.h"
#include "CButtonData.h"
#include "CGameButtonList.h"
#include "CGameEffect.h"
#include "CGameObjectArray.h"
#include "CGameOptions.h"
#include "CGameSave.h"
#include "CGameSprite.h"
#include "CGameSpriteEquipment.h"
#include "CIcon.h"
#include "CInfGame.h"
#include "IcewindCGameEffects.h"
#include "CMessage.h"
#include "CSound.h"
#include "CSpell.h"
#include "CItem.h"
#include "CScreenWorld.h"
#include "CUIControlBase.h"
#include "CUIControlButton.h"
#include "CUIManager.h"
#include "CUIPanel.h"
#include "CUtil.h"

// 0x851700
const BYTE CInfButtonArray::STATE_NONE = 0;

// 0x8E6820
static CGameButtonList* g_pButtonArrayPickerList = NULL;

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
    field_16E0 = -1;
    field_16E4 = 0;
    m_nState = STATE_NONE;

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
    m_nListStartIndex = 0;
}

// 0x587BD0
void CInfButtonArray::ClearPickerList()
{
    if (g_pButtonArrayPickerList != NULL) {
        while (!g_pButtonArrayPickerList->IsEmpty()) {
            delete g_pButtonArrayPickerList->RemoveHead();
        }
        g_pButtonArrayPickerList->RemoveAll();
        delete g_pButtonArrayPickerList;
        g_pButtonArrayPickerList = NULL;
    }
}

// Build one of the six picker lists for the party's first member.  SetState
// (0x589110) calls this on every picker state and stores the result in
// g_pButtonArrayPickerList; the caller owns the returned list.
//
// nSlot is already biased by the caller for kind 1 (quick-weapon button index
// + SLOT_WEAPON); kind 3 applies its own SLOT_MISC bias here.  a5 selects
// between the per-slot list and the whole-inventory list, and its polarity is
// deliberately opposite in the two item-backed kinds -- that is what the
// binary does.
//
// 0x587C20
CGameButtonList* CInfButtonArray::BuildPickerList(INT nSlot, INT nListType, const BYTE& nClass,
    DWORD nSpecialization, BOOL a5)
{
    CGameButtonList* pButtons = NULL;

    if (nListType != CINFBUTTONARRAY_PICKER_SPELL
        && nListType != CINFBUTTONARRAY_PICKER_QUICK_WEAPON
        && nListType != CINFBUTTONARRAY_PICKER_QUICK_ITEM
        && nListType != CINFBUTTONARRAY_PICKER_INNATE
        && nListType != CINFBUTTONARRAY_PICKER_INTERNAL
        && nListType != CINFBUTTONARRAY_PICKER_SONG) {
        return NULL;
    }

    if (g_pBaldurChitin->GetObjectGame()->GetGroup()->GetCount() == 0) {
        return NULL;
    }

    LONG* pGroupList = g_pBaldurChitin->GetObjectGame()->GetGroup()->GetGroupList();
    LONG nCharacterId = pGroupList[0];
    delete pGroupList;

    CGameSprite* pSprite;

    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(nCharacterId,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return NULL;
    }

    switch (nListType) {
    case CINFBUTTONARRAY_PICKER_QUICK_WEAPON:
        if (a5) {
            pButtons = pSprite->GetItemUsages(static_cast<SHORT>(nSlot),
                static_cast<WORD>(nListType),
                -1);
        } else {
            // NOTE: The binary calls 0x717250 here, a byte-identical copy of
            // GetAllItemUsages (0x716E80) the linker did not fold.
            pButtons = pSprite->GetAllItemUsages(FALSE);
        }
        break;
    case CINFBUTTONARRAY_PICKER_SPELL:
        if (nClass == CAIOBJECTTYPE_C_CLERIC && nSpecialization != 0) {
            pButtons = pSprite->GetDomainSpellsButtonList(nClass, nSpecialization);
        } else {
            pButtons = pSprite->GetSpellsButtonList(nClass);
        }
        break;
    case CINFBUTTONARRAY_PICKER_QUICK_ITEM:
        if (g_pBaldurChitin->GetObjectGame()->GetOptions()->m_bQuickItemMapping && !a5) {
            pButtons = pSprite->GetItemUsages(
                static_cast<SHORT>(nSlot + CGameSpriteEquipment::SLOT_MISC),
                static_cast<WORD>(nListType),
                -1);
        } else {
            pButtons = pSprite->GetAllItemUsages(FALSE);
        }
        break;
    case CINFBUTTONARRAY_PICKER_INNATE:
        pButtons = pSprite->GetInnateSpellsButtonList();
        break;
    case CINFBUTTONARRAY_PICKER_INTERNAL:
        pButtons = pSprite->GetInternalButtonList();
        break;
    case CINFBUTTONARRAY_PICKER_SONG:
        pButtons = pSprite->GetSongsButtonList();
        break;
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(nCharacterId,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return pButtons;
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

// Dispatch a picked item ability onto the party leader.  `bUseNow` inverts into
// CGameSprite::ReadyOffInternalList's `firstCall`: a "use now" click readies the
// ability for an immediate target pick, a customise click only records it.
//
// 0x5884B0
BOOLEAN CInfButtonArray::UseItemAction(const CButtonData* pButtonData, BOOL bUseNow)
{
    CInfGame* pGame = g_pBaldurChitin->m_pObjectGame;
    if (pGame->m_group.m_memberList.GetCount() == 0) {
        return FALSE;
    }

    LONG nLeader = pGame->m_group.GetGroupLeader();
    CGameSprite* pSprite;
    BYTE rc;
    do {
        rc = pGame->m_cObjectArray.GetDeny(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return FALSE;
    }

    pSprite->ReadyOffInternalList(*pButtonData, !bUseNow);
    pGame->m_cObjectArray.ReleaseDeny(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return TRUE;
}

// Fire quick slot `nButton` of the party leader, picking the action from the
// quick-slot family `nMode` (1 weapon, 2 spell, 3 item, 4 innate, 6 song).
// Mode 5 -- and any other value -- takes the leader lock and releases it again
// without doing anything.
//
// 0x588570
void CInfButtonArray::ReadyQuickSlotByMode(SHORT nButton, INT nMode)
{
    CInfGame* pGame = g_pBaldurChitin->m_pObjectGame;
    if (pGame->m_group.m_memberList.GetCount() == 0) {
        return;
    }

    LONG nLeader = pGame->m_group.GetGroupLeader();

    CGameSprite* pSprite;
    BYTE rc;
    do {
        rc = pGame->m_cObjectArray.GetDeny(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }

    switch (nMode) {
    case 1:
        pSprite->SetSelectedWeaponButton(nButton);
        break;
    case 2:
        pSprite->ReadySpell(nButton, 2, 0);
        break;
    case 3:
        pSprite->ReadyItem(nButton, 0);
        break;
    case 4:
        pSprite->ReadySpell(nButton, 4, 0);
        break;
    case 6:
        pSprite->ReadySpell(nButton, 6, 0);
        break;
    }

    pGame->m_cObjectArray.ReleaseDeny(nLeader, CGameObjectArray::THREAD_ASYNCH, INFINITE);
}

// Dispatch a picked spell onto the party leader.  Same shape as UseItemAction,
// through CGameSprite::UseButtonAction.
//
// 0x5886A0
BOOLEAN CInfButtonArray::UseSpellAction(const CButtonData* pButtonData, BOOL bUseNow)
{
    CInfGame* pGame = g_pBaldurChitin->m_pObjectGame;
    if (pGame->m_group.m_memberList.GetCount() == 0) {
        return FALSE;
    }

    LONG nLeader = pGame->m_group.GetGroupLeader();
    CGameSprite* pSprite;
    BYTE rc;
    do {
        rc = pGame->m_cObjectArray.GetDeny(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return FALSE;
    }

    pSprite->UseButtonAction(*pButtonData, !bUseNow);
    pGame->m_cObjectArray.ReleaseDeny(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return TRUE;
}

// Dispatch a picked innate ability onto the party leader.  Same shape as
// UseItemAction, through CGameSprite::UseButtonItem.
//
// 0x588760
BOOLEAN CInfButtonArray::UseInnateAction(const CButtonData* pButtonData, BOOL bUseNow)
{
    CInfGame* pGame = g_pBaldurChitin->m_pObjectGame;
    if (pGame->m_group.m_memberList.GetCount() == 0) {
        return FALSE;
    }

    LONG nLeader = pGame->m_group.GetGroupLeader();
    CGameSprite* pSprite;
    BYTE rc;
    do {
        rc = pGame->m_cObjectArray.GetDeny(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return FALSE;
    }

    pSprite->UseButtonItem(*pButtonData, !bUseNow);
    pGame->m_cObjectArray.ReleaseDeny(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return TRUE;
}

// Toggle the group leader's bard song from a song CButtonData: locate the song,
// and -- unless the leader is silenced -- either stop an active song (modal
// state 1) or start one (set the last-song index, enter modal state 1, play the
// ACT_01 cue and queue a SmallWait so the singer pauses).  A silenced leader
// just gets the "cannot sing" feedback and the bar resets.
//
// `bUseNow` is the same picker flag the three static Use*Action helpers take: a
// customise click (state 0x71) passes 0 and only takes and drops the lock, a
// play click (state 0x7A) passes 1 and reaches the toggle.
//
// 0x588820
BOOL CInfButtonArray::UseSongAction(const CButtonData* pButtonData, BOOL bUseNow)
{
    CInfGame* pGame = g_pBaldurChitin->m_pObjectGame;
    if (pGame->m_group.m_memberList.GetCount() == 0) {
        return FALSE;
    }

    LONG nLeader = pGame->m_group.GetGroupLeader();
    CGameSprite* pSprite;
    BYTE rc;
    do {
        rc = pGame->m_cObjectArray.GetDeny(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return FALSE;
    }

    UINT nSongID = 0;
    if (pGame->m_songs.Find(pButtonData->m_abilityId.m_res, nSongID) && bUseNow) {
        if ((pSprite->m_derivedStats.m_generalState & STATE_SILENCED) == 0
            && (pSprite->m_baseStats.m_generalState & STATE_SILENCED) == 0) {
            if (pSprite->m_nModalState == 1) {
                pSprite->SetModalState(0, FALSE);
                m_nSelectedButton = 100;
            } else {
                pSprite->m_nLastSong = static_cast<BYTE>(nSongID);
                pSprite->SetModalState(1, FALSE);
                m_nSelectedButton = 2;

                CSound sound;
                sound.SetResRef(CResRef("ACT_01"), TRUE, TRUE);
                sound.SetFireForget(TRUE);
                sound.SetChannel(5, 0);
                sound.Play(FALSE);

                CAIAction action(CAIAction::SMALLWAIT,
                    CAIObjectType(0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0),
                    1, -1, -1);
                CMessage* pMessage = new CMessageAddAction(action, pSprite->m_id, pSprite->m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
            }
        } else {
            pSprite->FeedBack(51 /* 0x85C10C */, 0, 0, 0, -1, 0, 0);
            pSprite->SetModalState(0, FALSE);
            m_nSelectedButton = 100;
            pGame->m_nState = 0;
            UpdateButtons();
        }
    }

    pGame->m_cObjectArray.ReleaseDeny(nLeader, CGameObjectArray::THREAD_ASYNCH, INFINITE);
    return TRUE;
}

// Store `buttonData` into quick slot `nButton` of the first party member,
// picking the destination array from `nMode` (1 weapon, 2 spell, 3 item,
// 4 innate, 6 song).  Mode 5 -- and any other value -- takes the member lock
// and releases it again without doing anything.
//
// Unlike ReadyQuickSlotByMode this resolves the member through GetGroupList()[0]
// rather than GetGroupLeader().
//
// 0x588CB0
void CInfButtonArray::CustomizeQuickSlot(const CButtonData* pButtonData, BYTE nButton, INT nMode)
{
    CInfGame* pGame = g_pBaldurChitin->m_pObjectGame;
    if (pGame->m_group.m_memberList.GetCount() == 0) {
        return;
    }

    LONG* pGroupList = pGame->m_group.GetGroupList();
    LONG nCharacterId = pGroupList[0];
    delete pGroupList;

    CGameSprite* pSprite;
    BYTE rc;
    do {
        rc = pGame->m_cObjectArray.GetDeny(nCharacterId,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }

    switch (nMode) {
    case 1:
        pSprite->SetQuickWeapon(nButton, *pButtonData);

        // The clamped store below is dead: the unconditional one that follows
        // it writes the same field from the same source.  Both are in the
        // binary (three inlined SetQuickWeapon(BYTE, BYTE) bodies, all from
        // ObjCreature.h line 2031).
        if (pButtonData->m_abilityId.m_itemNum >= CGameSpriteEquipment::SLOT_AMMO
            && pButtonData->m_abilityId.m_itemNum <= CGameSpriteEquipment::SLOT_AMMO + 3) {
            pSprite->SetQuickWeapon(nButton, static_cast<BYTE>(pButtonData->m_abilityId.m_itemNum));
        } else {
            pSprite->SetQuickWeapon(nButton, static_cast<BYTE>(0));
        }

        pSprite->SetQuickWeapon(nButton, static_cast<BYTE>(pButtonData->m_abilityId.m_itemNum));
        break;
    case 2:
        pSprite->SetQuickSpell(nButton, *pButtonData);
        break;
    case 3:
        pSprite->SetQuickItem(nButton, *pButtonData);
        break;
    case 4:
        pSprite->SetQuickAbility(nButton, *pButtonData);
        break;
    case 6:
        pSprite->SetQuickSong(nButton, *pButtonData);
        break;
    }

    pGame->m_cObjectArray.ReleaseDeny(nCharacterId, CGameObjectArray::THREAD_ASYNCH, INFINITE);
}

// 0x588FF0
BOOL CInfButtonArray::ResetState()
{
    // Pops one level off the action-bar state stack and re-applies it.  With
    // an empty stack it leaves the current bar untouched: it must NOT force
    // SetState(STATE_NONE), which blanked the bar when WorldEngineActivated
    // ran ResetState after the bar had already been built on load / new game.
    if (!m_stateStack.empty()) {
        INT nState = m_stateStack.front();
        m_stateStack.pop_front();
        SetState(nState, 0);
    }

    return TRUE;
}

// Walk the action-bar state stack back.  a3 == 1 unwinds the whole sequence in
// one go, landing on the bar it started from; anything else steps back a single
// level.  Both re-apply the state they land on, and neither pushes.
//
// 0x589FF0
void CInfButtonArray::PopState(int a2, char a3)
{
    if (m_stateStack.empty()) {
        return;
    }

    if (a3 == 1) {
        // The binary spells this out as an erase of the whole deque, choosing
        // whichever end is cheaper to rotate; the state it lands on is the one
        // at the bottom.
        INT nState = m_stateStack.back();
        m_stateStack.clear();
        SetState(nState, 0);
        return;
    }

    INT nState = m_stateStack.front();
    m_stateStack.pop_front();
    SetState(nState, a2);
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

    // The picker list is torn down on every transition; the picker branch
    // below rebuilds it when the new state is itself a picker.
    ClearPickerList();

    // a2 asks for the state being replaced to be remembered, so that a submenu
    // or picker can walk back to it.
    if (a2 == 1 && nState != m_nState && m_nState != 0) {
        m_stateStack.push_front(m_nState);
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
        // Picker states (weapon / spell / item / innate / song / feat points).
        // Each state selects one builder; the layout that follows is shared.
        // When N <= 12 the slots use the submenu types 0x15..0x20 (entry =
        // buttonType - 0x15).  When N > 12 the layout switches to paging
        // buttons: slot 0 = 0x21 (page-up arrow), slots 1..10 = 0x15..0x1E
        // (entries), slot 11 = 0x22 (page-down).  m_nListStartIndex holds the
        // index of the first displayed entry.  0x7B never reaches the paging
        // layout: its list is at most MAX_FEAT_POINTS + 1 entries long.
        BYTE nNoClass = CAIOBJECTTYPE_C_NONE;

        switch (nState) {
        case 0x65:
            g_pButtonArrayPickerList = BuildPickerList(
                m_nCustomizeSlot + CGameSpriteEquipment::SLOT_WEAPON,
                CINFBUTTONARRAY_PICKER_QUICK_WEAPON, nNoClass, 0, TRUE);
            break;
        case 0x66:
        case 0x67:
            UTIL_ASSERT(m_nCurrentSelectedSpellClass != CAIOBJECTTYPE_C_NONE);

            g_pButtonArrayPickerList = BuildPickerList(m_nCustomizeSlot,
                CINFBUTTONARRAY_PICKER_SPELL, m_nCurrentSelectedSpellClass,
                m_nCurrentSelectedSpellLevel, FALSE);
            break;
        case 0x68:
        case 0x69:
            g_pButtonArrayPickerList = BuildPickerList(m_nCustomizeSlot,
                CINFBUTTONARRAY_PICKER_QUICK_ITEM, nNoClass, 0, nState == 0x69);
            break;
        case 0x6A:
        case 0x6B:
            g_pButtonArrayPickerList = BuildPickerList(m_nCustomizeSlot,
                CINFBUTTONARRAY_PICKER_INNATE, nNoClass, 0, FALSE);
            break;
        case 0x70:
            g_pButtonArrayPickerList = BuildPickerList(m_nCustomizeSlot,
                CINFBUTTONARRAY_PICKER_INTERNAL, nNoClass, 0, FALSE);
            break;
        case 0x71:
        case 0x7A:
            g_pButtonArrayPickerList = BuildPickerList(m_nCustomizeSlot,
                CINFBUTTONARRAY_PICKER_SONG, nNoClass, 0, FALSE);
            break;
        case 0x7B:
            g_pButtonArrayPickerList = BuildFeatPointsPickerList(m_currentAbilityResRef);
            break;
        }

        if (g_pButtonArrayPickerList != NULL
            && g_pButtonArrayPickerList->GetCount() > 12) {
            // Paging layout - slot 0 = page up, slots 1-10 = entries
            // (filled by UpdateButtons via m_nListStartIndex), slot 11 = page
            // down.  Type 0x21 / 0x22 already in UpdateButtons.
            m_buttonTypes[0] = 0x21;
            for (INT nButton = 1; nButton < 11; nButton++) {
                m_buttonTypes[nButton] = 0x15 + (nButton - 1);
            }
            m_buttonTypes[11] = 0x22;
        } else {
            for (INT nButton = 0; nButton < 12; nButton++) {
                m_buttonTypes[nButton] = 0x15 + nButton;
            }
        }

        if (m_nState != nState) {
            m_nListStartIndex = 0;
        }

        m_nState = nState;
        UpdateButtons();
        return TRUE;
    }
    case 0x73:
    case 0x74:
        // Skills submenu (Stealth / Search / Thieving / Wilderness Lore / Animal Empathy).
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

    // The binary asks for the leader unconditionally -- there is no test of the
    // group's count in front of it -- takes the share ONCE, and RETURNS when
    // that share does not succeed (0x58A4E2 falls into the two CString
    // destructors and out).  So nothing past this point ever runs with a leader
    // it could not share: the rc/pSprite tests the arms below still carry are
    // redundant rather than wrong, and are left where they are for now.
    LONG nLeaderId = pGame->GetGroup()->GetGroupLeader();
    BYTE rc = pGame->GetObjectArray()->GetShare(nLeaderId,
        CGameObjectArray::THREAD_ASYNCH,
        reinterpret_cast<CGameObject**>(&pSprite),
        INFINITE);
    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }

    // __FILE__: C:\Projects\Icewind2\src\Baldur\InfButtonArray.cpp
    // __LINE__: 1800
    UTIL_ASSERT(pSprite != NULL);

    // Shared array-level overlay cells.  GUIBTACT (field_17C2) supplies every
    // m_bHasOverlay slot's 38x38 action icon -- RenderButtonOverlay picks the
    // per-button frame (m_nIconNormalFrame) into this one cell.  GUIBTBUT
    // (field_16E8) supplies the per-button selection marker.  Loaded here
    // rather than in the ctor because m_bDoubleSize is only known once the
    // panel exists.
    field_17C2.SetResRef(CResRef("GUIBTACT"), pPanel->m_pManager->m_bDoubleSize, TRUE, TRUE);
    field_16E8.SetResRef(CResRef("GUIBTBUT"), pPanel->m_pManager->m_bDoubleSize, TRUE, TRUE);

    // Computed once for the shared leader at 0x58A7BB, immediately before the
    // button loop, and read by the two arms that draw special abilities: the
    // Special Abilities button itself (type 0x0A) and the nine quick-ability
    // slots (0x5A-0x62).  Three unsigned "> 0" tests, not a count.
    BOOL bHasSpecialAbility = FALSE;
    if (rc == CGameObjectArray::SUCCESS && pSprite != NULL
        && (pSprite->m_innateSpells.m_nSharedCurrent > 0
            || pSprite->m_shapeshifts.m_nSharedTotal > 0
            || pSprite->m_shapeshifts.m_nSharedCurrent > 0)) {
        bHasSpecialAbility = TRUE;
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
        // settings.field_0.  "This slot is populated"; the shared tail and
        // RenderButton both gate on it.
        BOOL bActive = TRUE;
        // settings.m_bActive (+4) and settings.m_bHasOverlay (+8) are
        // INDEPENDENT stores in the binary, not two faces of one flag.
        // Surveyed over all 43 arms of the switch: the overlay arms leave
        // m_bActive 0 while m_bHasOverlay is 1, the arms that carry their own
        // icon BAM set m_bActive 1 and m_bHasOverlay 0, the 0x28 arm sets BOTH
        // to 1, and the empty-slot arm clears all three.
        BOOL bActiveIcon = FALSE;
        // The empty-slot arm makes no SetToolTipHotKey call at all; every other
        // arm makes one, most of them with 0xFFFF.
        BOOL bSetHotKey = TRUE;
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

        // The label the tooltip shows for this slot's hot key.  Built once per
        // slot in the loop head and handed to every SetToolTipHotKey call.
        CString sHotKeyLabel;
        sHotKeyLabel.Format(_T("F%d"), nButton + 1);

        switch (m_buttonTypes[nButton]) {
        // 0x58A9CA and 0x58AAB8 are two bodies, not one: they differ only in
        // the GUIBTACT frame they load, which goes to BOTH the normal and the
        // selected slot, and they converge at 0x58ABA1.  Neither sets a tooltip
        // or a hot key.  The two button types have no name yet -- nothing in the
        // recovered dispatch or picker code produces them.
        case 0:
            nIconNormalFrame = 0x30;
            nIconSelectedFrame = 0x30;
            bSetHotKey = FALSE;
            break;
        case 1:
            nIconNormalFrame = 0x34;
            nIconSelectedFrame = 0x34;
            bSetHotKey = FALSE;
            break;
        case 7:
            nIconNormalFrame = 0;
            nIconSelectedFrame = 2;
            nToolTip = 0x3E35;
            nIconSequence = 1;
            break;
        case 8:
            nIconNormalFrame = 0x0C;
            nIconSelectedFrame = 0x0E;
            nToolTip = 0x123A;
            nHotKey = 0x12;
            nIconSequence = 1;
            break;
        case 0x0F:
            nIconNormalFrame = 0x2C;
            nIconSelectedFrame = 0x2C;
            nToolTip = 0x3E34;
            nHotKey = 0x11;
            nIconSequence = 1;
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
            // 0x58B02B tests against -1 exactly, not against "negative": any
            // other negative value falls through to the FORM%d branch.
            if (nFormation == -1) {
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
            // The frames stay at -1: a formation slot paints its FORMx BAM
            // at whatever frame the cell already holds, and the arm at
            // 0x58AF5F stores -1 into both frame slots before the resref.
            nIconSequence = 1;
            nToolTip = 0x1347;
            // 0x58B24B reads the button TYPE as a word and adds 0x20, so the
            // five slots take 0x30..0x34 -- not 0x20 plus the slot index.
            nHotKey = static_cast<USHORT>(m_buttonTypes[nButton] + 0x20);
            bHasOverlay = FALSE;
            bActiveIcon = TRUE;
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
            nIconSequence = 1;
            // The song button's selection highlight tracks the PERSISTENT
            // song-modal state of the group leader, not the transient
            // m_nSelectedButton match -- override the generic m_bSelected set
            // above (0x58A340: clear piVar8[0x73], then set it to 1 when the
            // shared leader's m_nModalState == 1).
            settings.m_bSelected = (GetSelectedModalMode() == 1);
            break;
        case 3:
            nIconNormalFrame = 8;
            nIconSelectedFrame = 10;
            nToolTip = 0x1250;
            nHotKey = 0xB;
            break;
        case 4:
            // Search modal. Ghidra case 4 frames 0x24/0x26, tooltip 0x133F.
            nIconNormalFrame = 0x24;
            nIconSelectedFrame = 0x26;
            nToolTip = 0x133F;
            nHotKey = 0x10;
            nIconSequence = 1;
            // Selection highlight tracks the leader's persistent search modal
            // (state 2), not the transient m_nSelectedButton match -- override
            // the generic set above (0x58A340 case 4: clear piVar8[0x73], set it
            // to 1 only when the shared leader's m_nModalState == 2).
            settings.m_bSelected = (GetSelectedModalMode() == 2);
            break;
        case 5:
            // Skills button. Ghidra case 5 frames 0x60/0x62, tooltip 0x1345.
            nIconNormalFrame = 0x60;
            nIconSelectedFrame = 0x62;
            nToolTip = 0x1345;
            nHotKey = 0xD;
            nIconSequence = 1;
            break;
        case 9:
            // Shapeshift â€” frame 0x28, tooltip 0x135E.
            nIconNormalFrame = 0x28;
            nIconSelectedFrame = 0x28;
            nToolTip = 0x135E;
            break;
        case 10:
            // Special Abilities.  Frames 0x28/0x2A, hot key 0x13, sequence 0.
            nIconNormalFrame = 0x28;
            nIconSelectedFrame = 0x2A;
            nToolTip = 0x135A;
            nHotKey = 0x13;
            // Greyed, and re-labelled "No Special Abilities" (strref 0x9243),
            // when the leader has neither innates nor shapeshifts left or the
            // innate spell type is disabled outright.  The arm at 0x58CCD4
            // runs this test FIRST, before it writes a single field.
            if (!bHasSpecialAbility
                || (rc == CGameObjectArray::SUCCESS && pSprite != NULL
                    && pSprite->GetDerivedStats()->m_disabledSpellTypes[2] == 1)) {
                bGreyOut = TRUE;
                nToolTip = 0x9243;
            }
            // A modal feat that is dialled in shows as selected and re-labels
            // the button "Using Special Ability" (0x9B93), overriding either
            // tooltip above (0x58CE0E).
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL
                && IsUsingModalFeat(pSprite) == 1) {
                nToolTip = 0x9B93;
                settings.m_bSelected = 1;
            }
            break;
        case 0x0B:
            // Stealth. Ghidra case 0x0B frames 0x1C/0x1E, tooltip 0x1368.
            nIconNormalFrame = 0x1C;
            nIconSelectedFrame = 0x1E;
            nToolTip = 0x1368;
            nHotKey = 0xF;
            nIconSequence = 1;
            // Grey the button out while the Stealth slot of the disabled-buttons
            // array is set (m_disabledButtons[0], at +0x16CC) or the post-reveal
            // grey-out timer is still ticking (m_nStealthGreyOut > 0), so stealth
            // cannot be re-armed mid-cooldown (0x58A340 case 0xB: set piVar8[0x77]
            // / m_bGreyOut).  Confirmed on the original via Frida: re-clicking
            // Stealth during the ~90-tick cooldown is swallowed by
            // OnLButtonPressed's grey-out gate (no SetModalState).  AIUpdate counts
            // the timer down and calls UpdateState() at 0, which re-runs
            // SetState(m_nState) -> UpdateButtons and clears the grey.
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL
                && (pSprite->GetDerivedStats()->m_disabledButtons[0] == TRUE
                    || pSprite->m_nStealthGreyOut > 0)) {
                bGreyOut = TRUE;
            }
            // Selection highlight tracks the leader's persistent stealth modal
            // (state 3), not the transient m_nSelectedButton match -- override
            // the generic set above (0x58A340 case 0xB: clear piVar8[0x73], set
            // it to 1 only when the shared leader's m_nModalState == 3).
            settings.m_bSelected = (GetSelectedModalMode() == 3);
            break;
        case 0x0C:
            // Thieving. Ghidra case 0x0C frames 0x18/0x1A, tooltip 0x136B.
            nIconNormalFrame = 0x18;
            nIconSelectedFrame = 0x1A;
            nToolTip = 0x136B;
            nHotKey = 0xE;
            nIconSequence = 1;
            break;
        case 0x0D:
            // Animal Empathy. These are GUIBTACT logical frames; the BAM cycle
            // lookup maps 0x7C/0x7E to the animal head physical frames.
            nIconNormalFrame = 0x7C;
            nIconSelectedFrame = 0x7E;
            nToolTip = 0x136E;
            nHotKey = 0x9;
            nIconSequence = 1;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL
                && static_cast<signed char>(pSprite->GetDerivedStats()->m_nSkills[CGAMESPRITE_SKILL_ANIMAL_EMPATHY]) < 1) {
                bGreyOut = TRUE;
            }
            break;
        case 0x0E:
            // Use Item. Ghidra case 0x0E frames 0x10/0x12, tooltip 0x1372.
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
        case 0x20:
            // The twelve picker cells.  In the binary (0x58E25F) this arm is
            // nine unconditional stores plus two empty cells: it does NOT test
            // m_nState and it does NOT read the picker list.  Every icon,
            // count, tooltip and selection a picker shows is written
            // afterwards by the SECOND switch at the end of this function,
            // which is where all the state-dependent work lives.  This arm
            // sets no tooltip hot key and hands SetToolTipStrRef three -1s.
            nIconNormalFrame = -1;
            nIconSelectedFrame = -1;
            nIconSequence = 0;
            bHasOverlay = FALSE;
            bActiveIcon = TRUE;
            bSetHotKey = FALSE;
            cIconResRef = CResRef("");
            settings.m_bSelected = 0;
            break;
        case 0x23:
            // Customize: Skills.  The arm at 0x58DDC3 opens `mov ebp, 1`
            // and stores ebp into m_nIconSequence at 0x58DE29 -- one of the
            // four customize arms that do, against the four below that store
            // ebx.  s40's per-arm map reads this as a register NAME, which is
            // how it stayed 0 here until state 0x75 was finally measured.
            nIconNormalFrame = 0x60;
            nIconSelectedFrame = 0x62;
            nIconSequence = 1;
            nToolTip = 0x1345;
            break;
        case 0x24:
            // Customize: Cast Spell. Original greys it and shows "No Spells"
            // when the leader is not a caster.
            nIconNormalFrame = 8;
            nIconSelectedFrame = 10;
            nToolTip = 0x1250;
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL && !pSprite->IsSpellcaster()) {
                nToolTip = 0x924A;
                bGreyOut = TRUE;
            }
            break;
        case 0x25:
            // Customize: Use Item.
            nIconNormalFrame = 0x10;
            nIconSelectedFrame = 0x12;
            nToolTip = 0x1372;
            break;
        case 0x26:
            // Customize: Quick Item.
            nIconNormalFrame = 0x70;
            nIconSelectedFrame = 0x72;
            nToolTip = 0x1349;
            break;
        case 0x27:
            // Customize: Special Abilities.
            nIconNormalFrame = 0x28;
            nIconSelectedFrame = 0x2A;
            nToolTip = 0x135A;
            break;
        case 0x28:
            // Customize: Battle Song.  The one arm of the 43 that sets
            // m_bActive AND m_bHasOverlay together (0x58E0B8).
            bActiveIcon = TRUE;
            nIconNormalFrame = 0x14;
            nIconSelectedFrame = 0x16;
            nIconSequence = 1;   // ebp, set at 0x58E0B8, stored 0x58E11E
            nToolTip = 0x1336;
            break;
        case 0x29:
            // Customize: Clear Button.
            nIconNormalFrame = 0x74;
            nIconSelectedFrame = 0x76;
            nIconSequence = 1;   // ebp, set at 0x58E145, stored 0x58E1AB
            nToolTip = 0x9B2B;
            break;
        case 0x2A:
            // Customize: Restore Default Buttons.
            nIconNormalFrame = 0x78;
            nIconSelectedFrame = 0x7A;
            nIconSequence = 1;   // ebp, set at 0x58E1D2, stored 0x58E238
            nToolTip = 0x9B2C;
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
            // Class picker: Bard Spell.
            nIconNormalFrame = 0x38;
            nIconSelectedFrame = 0x3A;
            nToolTip = 0x9B1D;
            nHotKey = 0xB;
            break;
        case 0x33:
            // Class picker: Cleric Spell.
            nIconNormalFrame = 0x3C;
            nIconSelectedFrame = 0x3E;
            nToolTip = 0x9B1E;
            nHotKey = 0xB;
            break;
        case 0x34:
            // Class picker: Druid Spell.
            nIconNormalFrame = 0x40;
            nIconSelectedFrame = 0x42;
            nToolTip = 0x9B1F;
            nHotKey = 0xB;
            break;
        case 0x35:
            // Class picker: Paladin Spell.
            nIconNormalFrame = 0x44;
            nIconSelectedFrame = 0x46;
            nToolTip = 0x9B20;
            nHotKey = 0xB;
            break;
        case 0x36:
            // Class picker: Ranger Spell.
            nIconNormalFrame = 0x48;
            nIconSelectedFrame = 0x4A;
            nToolTip = 0x9B21;
            nHotKey = 0xB;
            break;
        case 0x37:
            // Class picker: Sorcerer Spell.
            nIconNormalFrame = 0x4C;
            nIconSelectedFrame = 0x4E;
            nToolTip = 0x9B22;
            nHotKey = 0xB;
            break;
        case 0x38:
            // Class picker: Wizard Spell.
            nIconNormalFrame = 0x50;
            nIconSelectedFrame = 0x52;
            nToolTip = 0x9B23;
            nHotKey = 0xB;
            break;
        case 0x39:
            // Class picker: Domain Spell.
            nIconNormalFrame = 0x54;
            nIconSelectedFrame = 0x56;
            nToolTip = 0x9C04;
            nHotKey = 0xB;
            break;
        case 0x77:
            // Wilderness Lore. Ghidra case 0x77 frames 0x5C/0x5E, tooltip
            // 0x7DBA, hotkey 0x36 ('6').
            nIconNormalFrame = 0x5C;
            nIconSelectedFrame = 0x5E;
            nToolTip = 0x7DBA;
            nHotKey = 0x36;
            nIconSequence = 1;
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
            // 0x58CE5D stores the frames and the sequence before the arm
            // has looked at the slot at all, so the empty branch keeps them.
            nIconNormalFrame = 0x68;
            nIconSelectedFrame = 0x6A;
            nIconSequence = 1;
            if (buttonData.m_icon != "") {
                cIconResRef = buttonData.m_icon;
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                // Off-hand slot (odd index) â†’ STONSHIL.  Main hand â†’ STONWEAP.
                cIconResRef = (m_buttonTypes[nButton] & 1) ? CResRef("STONSHIL") : CResRef("STONWEAP");
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
            bActiveIcon = TRUE;
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
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
                if (buttonData.m_bDisplayCount) {
                    nCount = buttonData.m_count;
                }
            } else {
                cIconResRef = CResRef("STONSPEL");
                nToolTip = 0x1250;
            }
            // 0x58D89D, the last word of the arm: a slot showing no count is
            // greyed out whatever the branches above decided.
            if (nCount == 0) {
                bGreyOut = TRUE;
            }
            // 0x58D5BD stores -1 into both frame slots and 0 into the sequence,
            // whatever the slot holds.
            bHasOverlay = FALSE;
            bActiveIcon = TRUE;
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
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
                if (buttonData.m_bDisplayCount) {
                    nCount = buttonData.m_count;
                }
            } else {
                cIconResRef = CResRef("STONITEM");
                nToolTip = 0x1372;
            }
            // 0x58D519: a quick slot with nothing to count greys out.  The
            // exemption in between compares settings.field_1D4 against strref
            // 0x6097, "Tiernon's Hearthstone" -- against a field that NOTHING in
            // the binary ever writes and no constructor initialises, so the test
            // never fires.  Reproduced, not corrected.
            if (nCount <= 0 && settings.field_1D4 != 0x6097) {
                bGreyOut = TRUE;
            }
            // 0x58D364 stores -1 into both frame slots and 0x58D381 stores 1
            // into the sequence, before the arm looks at the slot.
            nIconSequence = 1;
            nHotKey = static_cast<USHORT>(0x2D + (m_buttonTypes[nButton] - 0x50));
            bHasOverlay = FALSE;
            bActiveIcon = TRUE;
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
                nToolTip = buttonData.m_name;
                if (buttonData.m_bDisplayCount) {
                    nCount = buttonData.m_count;
                }
            } else {
                cIconResRef = CResRef("STONSPEC");
                nToolTip = 0x135A;
            }
            // The grey-out is decided at the very END of the arm (0x58DC32),
            // outside the icon branch, and takes the same "No Special
            // Abilities" label the Special Abilities button takes: a slot is
            // greyed when the leader has no special abilities left at all, or
            // when the entry the quick slot resolved to is itself disabled.
            if (!bHasSpecialAbility || buttonData.m_bDisabled) {
                bGreyOut = TRUE;
                nToolTip = 0x9243;
            }
            // 0x58D9DA stores -1 into both frame slots and 0 into the sequence.
            bHasOverlay = FALSE;
            bActiveIcon = TRUE;
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
                nToolTip = buttonData.m_name;
                bGreyOut = buttonData.m_bDisabled;
            } else {
                cIconResRef = CResRef("STONSONG");
                nToolTip = 0x923C;
            }
            // 0x58DC8C: the song slots keep the bard-song GUIBTACT frames even
            // though they draw their own icon, and the sequence stays 0.
            nIconNormalFrame = 0x14;
            nIconSelectedFrame = 0x16;
            bHasOverlay = FALSE;
            bActiveIcon = TRUE;
            break;
        }
        case 100:
        default:
            // Empty slot (0x58A8A4, and the shared default at 0x58E4AD).  All
            // three of field_0 / m_bActive / m_bHasOverlay are cleared, the
            // loop head's m_bSelected compare is overwritten with 0, and
            // m_bGreyOut goes to 1.  The only thing the arm varies is the
            // tooltip, and it varies on the ARRAY's state, not on the slot.
            bActive = FALSE;
            bEnabled = FALSE;
            bHasOverlay = FALSE;
            cIconResRef = CResRef("");
            settings.m_bSelected = 0;
            nToolTip = m_nState == 0x72 ? 0xA010 : -1;
            bSetHotKey = FALSE;
            break;
        }

        settings.field_0 = bActive ? 1 : 0;
        settings.m_bActive = bActiveIcon ? 1 : 0;
        settings.m_bHasOverlay = bHasOverlay ? 1 : 0;
        settings.m_nIconNormalFrame = nIconNormalFrame;
        settings.m_nIconSelectedFrame = nIconSelectedFrame;
        settings.m_nIconSequence = nIconSequence;
        settings.m_bActiveWeaponSet = bActiveWeaponSet ? 1 : 0;
        settings.m_nCount = nCount;
        settings.m_bGreyOut = !bEnabled || bGreyOut;
        // Every arm writes BOTH cells, and both writes take bSetAutoRequest and
        // bWarningIfMissing TRUE.  bDoubleSize is the discriminator, surveyed
        // over the 62 SetResRef sites of the switch: a cell that receives a
        // real resref is given CBaldurChitin::GetDoubleSize(), a cell that
        // receives "" is given FALSE.  Only a slot carrying its own icon BAM
        // ever receives one, so the overlay arms and the empty-slot arm both
        // land in the first branch.  No SequenceSet and no FrameSet: the
        // binary's UpdateButtons never touches either cell's frame cursor.
        if (bHasOverlay || cIconResRef == "") {
            settings.m_iconCell.SetResRef(CResRef(""), FALSE, TRUE, TRUE);
        } else {
            settings.m_iconCell.SetResRef(cIconResRef, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        }
        settings.m_countCell.SetResRef(CResRef(""), FALSE, TRUE, TRUE);

        // The binary's UpdateButtons touches pButton through exactly two vtable
        // slots -- +0x44 SetToolTipStrRef and +0x48 SetToolTipHotKey.  It sets
        // no frame, no cell and no enabled flag; those belong to whoever built
        // the panel.
        pButton->SetToolTipStrRef(nToolTip, -1, -1);
        if (bSetHotKey) {
            pButton->SetToolTipHotKey(nHotKey, 0xFFFF, sHotKeyLabel);
        }

        // 0x58E52F, the tail every arm falls into: a slot that is present and
        // not already greyed out by its own arm takes its grey-out from
        // CheckActivation.
        if (settings.field_0 != 0 && settings.m_bGreyOut == 0) {
            settings.m_bGreyOut = CheckActivation(m_buttonTypes[nButton]) == 0;
        }
    }

    // ------------------------------------------------------------------
    // The SECOND switch, at 0x58E56D.  Once every slot has been through the
    // per-type switch above, a switch on `m_nState - 0x65` over 23 values --
    // index table at 0x58FC54, jumptable at 0x58FC28 -- lets a PICKER state
    // rewrite the slots it owns.  Fourteen of the 23 values have an arm; the
    // rest (0x6E, 0x6F, 0x72 and 0x74-0x79) take the default and change
    // nothing, which is why no bar reachable before session 43 proved a line
    // of it.
    //
    // Ten arms are the same walk over g_pButtonArrayPickerList and the binary
    // gives each its own body; they differ in the slot range, the fallback
    // resref, the tooltip field and fallback, and in whether they write a
    // grey-out or a selection at all, so they are written out separately here
    // too.  The two formation arms are one body the compiler duplicated: they
    // are identical instruction for instruction bar the stack slots of their
    // temporaries.
    //
    // Common to all ten: the label is rebuilt per slot, the control is fetched
    // before anything is written (and a missing control leaves the slot
    // untouched), a POSITION that has run out clears field_0, an entry that is
    // NULL sets field_0 and stops, and every cell takes GetDoubleSize() with
    // bSetAutoRequest and bWarningIfMissing TRUE.
    switch (m_nState) {
    case 0x65: {
        // Quick-weapon picker (0x58EA6A).  The only arm that walks from the
        // HEAD of the list rather than from FindIndex(m_nListStartIndex), the
        // only one with no count cap, and the only one whose selection comes
        // from the leader's equipment rather than from the entry.  It writes
        // no grey-out.
        if (g_pButtonArrayPickerList == NULL) {
            break;
        }

        POSITION pos = g_pButtonArrayPickerList->GetHeadPosition();
        for (INT nSlot = 0; nSlot < 12; nSlot++) {
            CInfButtonSettings& s = m_buttonArray[nSlot];
            CUIControlBase* pControl = pPanel->GetControl(nSlot + 6);
            if (pControl == NULL) {
                continue;
            }

            CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
            CString sLabel;
            sLabel.Format(_T("F%d"), nSlot + 1);

            if (pos == NULL) {
                s.field_0 = 0;
                continue;
            }

            CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
            if (pEntry == NULL) {
                s.field_0 = 1;
                continue;
            }

            if (pEntry->m_icon != "") {
                s.m_iconCell.SetResRef(pEntry->m_icon, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            } else {
                s.m_iconCell.SetResRef(CResRef("STONWEAP"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            }
            if (pEntry->m_bDisplayCount) {
                s.m_nCount = pEntry->m_count;
            }

            s.m_bSelected = pEntry->m_abilityId.m_itemNum == pSprite->GetEquipment()->m_selectedWeapon
                && pEntry->m_abilityId.m_abilityNum == pSprite->GetEquipment()->m_selectedWeaponAbility;

            STRREF nTip = pEntry->m_abilityId.m_strDescription;
            pButton->SetToolTipStrRef(nTip == -1 ? 0x1356 : nTip, -1, -1);
            pButton->SetToolTipHotKey(0xFFFF, 0xFFFF, sLabel);
            s.field_0 = 1;
        }
        break;
    }
    case 0x66:
    case 0x67: {
        // Spellbook picker (0x58ED52).  The only arm that hands
        // SetToolTipStrRef a third argument off the entry.
        if (g_pButtonArrayPickerList == NULL) {
            break;
        }

        INT nFirst = 0;
        INT nLast = 0x0B;
        if (g_pButtonArrayPickerList->GetCount() > 12) {
            nFirst = 1;
            nLast = 0x0A;
        }

        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        for (INT nSlot = nFirst; nSlot <= nLast; nSlot++) {
            CInfButtonSettings& s = m_buttonArray[nSlot];
            CUIControlBase* pControl = pPanel->GetControl(nSlot + 6);
            if (pControl == NULL) {
                continue;
            }

            CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
            CString sLabel;
            sLabel.Format(_T("F%d"), nSlot + 1);

            if (pos == NULL) {
                s.field_0 = 0;
                continue;
            }

            CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
            if (pEntry == NULL) {
                s.field_0 = 1;
                continue;
            }

            if (pEntry->m_icon != "") {
                s.m_iconCell.SetResRef(pEntry->m_icon, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            } else {
                s.m_iconCell.SetResRef(CResRef("STONSPEL"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            }
            if (pEntry->m_bDisplayCount) {
                s.m_nCount = pEntry->m_count;
            }
            s.m_bGreyOut = pEntry->m_bDisabled;

            if (pEntry->m_name == -1) {
                pButton->SetToolTipStrRef(0x134A, -1, -1);
            } else {
                pButton->SetToolTipStrRef(pEntry->m_name, -1, pEntry->m_abilityId.m_strTooltipDesc);
            }
            pButton->SetToolTipHotKey(0xFFFF, 0xFFFF, sLabel);
            s.field_0 = 1;
        }
        break;
    }
    case 0x68:
    case 0x69: {
        // Item picker (0x58EED8).  Writes no grey-out.
        if (g_pButtonArrayPickerList == NULL) {
            break;
        }

        INT nFirst = 0;
        INT nLast = 0x0B;
        if (g_pButtonArrayPickerList->GetCount() > 12) {
            nFirst = 1;
            nLast = 0x0A;
        }

        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        for (INT nSlot = nFirst; nSlot <= nLast; nSlot++) {
            CInfButtonSettings& s = m_buttonArray[nSlot];
            CUIControlBase* pControl = pPanel->GetControl(nSlot + 6);
            if (pControl == NULL) {
                continue;
            }

            CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
            CString sLabel;
            sLabel.Format(_T("F%d"), nSlot + 1);

            if (pos == NULL) {
                s.field_0 = 0;
                continue;
            }

            CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
            if (pEntry == NULL) {
                s.field_0 = 1;
                continue;
            }

            if (pEntry->m_icon != "") {
                s.m_iconCell.SetResRef(pEntry->m_icon, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            } else {
                s.m_iconCell.SetResRef(CResRef("STONITEM"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            }
            if (pEntry->m_bDisplayCount) {
                s.m_nCount = pEntry->m_count;
            }

            STRREF nTip = pEntry->m_abilityId.m_strDescription;
            pButton->SetToolTipStrRef(nTip == -1 ? 0x1356 : nTip, -1, -1);
            pButton->SetToolTipHotKey(0xFFFF, 0xFFFF, sLabel);
            s.field_0 = 1;
        }
        break;
    }
    case 0x6A:
    case 0x6B: {
        // Innate picker (0x58F056).  The five modal feats appear in the innate
        // list as SPIN275..SPIN279; each shows selected while any of the spell
        // states that stand for its ranks is set, and greys out entirely when
        // the leader does not have the feat.  An entry that is none of the
        // five keeps the selection and grey-out the per-type arm left it with.
        if (g_pButtonArrayPickerList == NULL) {
            break;
        }

        INT nFirst = 0;
        INT nLast = 0x0B;
        if (g_pButtonArrayPickerList->GetCount() > 12) {
            nFirst = 1;
            nLast = 0x0A;
        }

        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        for (INT nSlot = nFirst; nSlot <= nLast; nSlot++) {
            CInfButtonSettings& s = m_buttonArray[nSlot];
            CUIControlBase* pControl = pPanel->GetControl(nSlot + 6);
            if (pControl == NULL) {
                continue;
            }

            CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
            CString sLabel;
            sLabel.Format(_T("F%d"), nSlot + 1);

            if (pos == NULL) {
                s.field_0 = 0;
                continue;
            }

            CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
            if (pEntry == NULL) {
                s.field_0 = 1;
                continue;
            }

            const CDerivedStats* pStats = pSprite->GetDerivedStats();
            if (pEntry->m_abilityId.m_res == CGameSprite::SPIN275) {
                if (!pSprite->HasFeat(CGAMESPRITE_FEAT_POWER_ATTACK)) {
                    s.m_bSelected = 0;
                    s.m_bGreyOut = 1;
                } else {
                    if (pStats->m_spellStates[SPLSTATE_FEAT_POWER_ATTACK_1]
                        || pStats->m_spellStates[SPLSTATE_FEAT_POWER_ATTACK_2]
                        || pStats->m_spellStates[SPLSTATE_FEAT_POWER_ATTACK_3]
                        || pStats->m_spellStates[SPLSTATE_FEAT_POWER_ATTACK_4]
                        || pStats->m_spellStates[SPLSTATE_FEAT_POWER_ATTACK_5]) {
                        s.m_bSelected = 1;
                    }
                    s.m_bGreyOut = 0;
                }
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN276) {
                if (!pSprite->HasFeat(CGAMESPRITE_FEAT_EXPERTISE)) {
                    s.m_bSelected = 0;
                    s.m_bGreyOut = 1;
                } else {
                    if (pStats->m_spellStates[SPLSTATE_FEAT_EXPERTISE_1]
                        || pStats->m_spellStates[SPLSTATE_FEAT_EXPERTISE_2]
                        || pStats->m_spellStates[SPLSTATE_FEAT_EXPERTISE_3]
                        || pStats->m_spellStates[SPLSTATE_FEAT_EXPERTISE_4]
                        || pStats->m_spellStates[SPLSTATE_FEAT_EXPERTISE_5]) {
                        s.m_bSelected = 1;
                    }
                    s.m_bGreyOut = 0;
                }
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN277) {
                if (!pSprite->HasFeat(CGAMESPRITE_FEAT_ARTERIAL_STRIKE)) {
                    s.m_bSelected = 0;
                    s.m_bGreyOut = 1;
                } else {
                    if (pStats->m_spellStates[SPLSTATE_FEAT_ARTERIAL_STRIKE]) {
                        s.m_bSelected = 1;
                    }
                    s.m_bGreyOut = 0;
                }
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN278) {
                if (!pSprite->HasFeat(CGAMESPRITE_FEAT_HAMSTRING)) {
                    s.m_bSelected = 0;
                    s.m_bGreyOut = 1;
                } else {
                    if (pStats->m_spellStates[SPLSTATE_FEAT_HAMSTRING]) {
                        s.m_bSelected = 1;
                    }
                    s.m_bGreyOut = 0;
                }
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN279) {
                if (!pSprite->HasFeat(CGAMESPRITE_FEAT_RAPID_SHOT)) {
                    s.m_bSelected = 0;
                    s.m_bGreyOut = 1;
                } else {
                    if (pStats->m_spellStates[SPLSTATE_FEAT_RAPID_SHOT]) {
                        s.m_bSelected = 1;
                    }
                    s.m_bGreyOut = 0;
                }
            }

            if (pEntry->m_icon != "") {
                s.m_iconCell.SetResRef(pEntry->m_icon, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            } else {
                s.m_iconCell.SetResRef(CResRef("STONSPEC"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            }
            if (pEntry->m_bDisplayCount) {
                s.m_nCount = pEntry->m_count;
            }

            STRREF nTip = pEntry->m_abilityId.m_strDescription;
            pButton->SetToolTipStrRef(nTip == -1 ? 0x923B : nTip, -1, -1);
            pButton->SetToolTipHotKey(0xFFFF, 0xFFFF, sLabel);
            s.field_0 = 1;
        }
        break;
    }
    case 0x6C:
        // Formation picker (0x58E7F8).  Twelve icon cells, FORM0 through FORMB
        // in slot order, and nothing else at all -- no count, no tooltip, no
        // selection, no grey-out.  0x6D below is a SEPARATE body in the binary
        // and identical to this one instruction for instruction, bar the stack
        // slots of its temporaries; the two are kept apart here because the
        // jumptable really does send the two states to different addresses.
        m_buttonArray[0].m_iconCell.SetResRef(CResRef("FORM0"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[1].m_iconCell.SetResRef(CResRef("FORM1"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[2].m_iconCell.SetResRef(CResRef("FORM2"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[3].m_iconCell.SetResRef(CResRef("FORM3"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[4].m_iconCell.SetResRef(CResRef("FORM4"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[5].m_iconCell.SetResRef(CResRef("FORM5"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[6].m_iconCell.SetResRef(CResRef("FORM6"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[7].m_iconCell.SetResRef(CResRef("FORM7"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[8].m_iconCell.SetResRef(CResRef("FORM8"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[9].m_iconCell.SetResRef(CResRef("FORM9"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[10].m_iconCell.SetResRef(CResRef("FORMA"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[11].m_iconCell.SetResRef(CResRef("FORMB"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        break;
    case 0x6D:
        // Formation picker (0x58E592).  The twin of 0x6C above.
        m_buttonArray[0].m_iconCell.SetResRef(CResRef("FORM0"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[1].m_iconCell.SetResRef(CResRef("FORM1"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[2].m_iconCell.SetResRef(CResRef("FORM2"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[3].m_iconCell.SetResRef(CResRef("FORM3"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[4].m_iconCell.SetResRef(CResRef("FORM4"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[5].m_iconCell.SetResRef(CResRef("FORM5"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[6].m_iconCell.SetResRef(CResRef("FORM6"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[7].m_iconCell.SetResRef(CResRef("FORM7"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[8].m_iconCell.SetResRef(CResRef("FORM8"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[9].m_iconCell.SetResRef(CResRef("FORM9"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[10].m_iconCell.SetResRef(CResRef("FORMA"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        m_buttonArray[11].m_iconCell.SetResRef(CResRef("FORMB"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
        break;
    case 0x70: {
        // Song picker (0x58F8E3).
        if (g_pButtonArrayPickerList == NULL) {
            break;
        }

        INT nFirst = 0;
        INT nLast = 0x0B;
        if (g_pButtonArrayPickerList->GetCount() > 12) {
            nFirst = 1;
            nLast = 0x0A;
        }

        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        for (INT nSlot = nFirst; nSlot <= nLast; nSlot++) {
            CInfButtonSettings& s = m_buttonArray[nSlot];
            CUIControlBase* pControl = pPanel->GetControl(nSlot + 6);
            if (pControl == NULL) {
                continue;
            }

            CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
            CString sLabel;
            sLabel.Format(_T("F%d"), nSlot + 1);

            if (pos == NULL) {
                s.field_0 = 0;
                continue;
            }

            CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
            if (pEntry == NULL) {
                s.field_0 = 1;
                continue;
            }

            if (pEntry->m_icon != "") {
                s.m_iconCell.SetResRef(pEntry->m_icon, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            } else {
                s.m_iconCell.SetResRef(CResRef("STONSPEL"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            }
            if (pEntry->m_bDisplayCount) {
                s.m_nCount = pEntry->m_count;
            }
            s.m_bGreyOut = pEntry->m_bDisabled;

            STRREF nTip = pEntry->m_abilityId.m_strDescription;
            pButton->SetToolTipStrRef(nTip == -1 ? 0x134A : nTip, -1, -1);
            pButton->SetToolTipHotKey(0xFFFF, 0xFFFF, sLabel);
            s.field_0 = 1;
        }
        break;
    }
    case 0x71:
    case 0x7A: {
        // Song pickers (0x58F758).
        if (g_pButtonArrayPickerList == NULL) {
            break;
        }

        INT nFirst = 0;
        INT nLast = 0x0B;
        if (g_pButtonArrayPickerList->GetCount() > 12) {
            nFirst = 1;
            nLast = 0x0A;
        }

        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        for (INT nSlot = nFirst; nSlot <= nLast; nSlot++) {
            CInfButtonSettings& s = m_buttonArray[nSlot];
            CUIControlBase* pControl = pPanel->GetControl(nSlot + 6);
            if (pControl == NULL) {
                continue;
            }

            CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
            CString sLabel;
            sLabel.Format(_T("F%d"), nSlot + 1);

            if (pos == NULL) {
                s.field_0 = 0;
                continue;
            }

            CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
            if (pEntry == NULL) {
                s.field_0 = 1;
                continue;
            }

            if (pEntry->m_icon != "") {
                s.m_iconCell.SetResRef(pEntry->m_icon, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            } else {
                s.m_iconCell.SetResRef(CResRef("STONSONG"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            }
            if (pEntry->m_bDisplayCount) {
                s.m_nCount = pEntry->m_count;
            }
            s.m_bGreyOut = pEntry->m_bDisabled;

            pButton->SetToolTipStrRef(pEntry->m_name == -1 ? 0x923C : pEntry->m_name, -1, -1);
            pButton->SetToolTipHotKey(0xFFFF, 0xFFFF, sLabel);
            s.field_0 = 1;
        }
        break;
    }
    case 0x73: {
        // Skills submenu (0x58EBEB).  The only arm that BAILS on a list too
        // long to fit rather than paging it, and the only one that starts part
        // way along the bar: the five skill buttons keep slots 0-4 and the
        // list fills 5-11.
        if (g_pButtonArrayPickerList == NULL) {
            break;
        }
        if (g_pButtonArrayPickerList->GetCount() > 9) {
            break;
        }

        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        for (INT nSlot = 5; nSlot < 12; nSlot++) {
            CInfButtonSettings& s = m_buttonArray[nSlot];
            CUIControlBase* pControl = pPanel->GetControl(nSlot + 6);
            if (pControl == NULL) {
                continue;
            }

            CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
            CString sLabel;
            sLabel.Format(_T("F%d"), nSlot + 1);

            if (pos == NULL) {
                s.field_0 = 0;
                continue;
            }

            CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
            if (pEntry == NULL) {
                s.field_0 = 1;
                continue;
            }

            if (pEntry->m_icon != "") {
                s.m_iconCell.SetResRef(pEntry->m_icon, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            } else {
                s.m_iconCell.SetResRef(CResRef("STONSPEL"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            }
            if (pEntry->m_bDisplayCount) {
                s.m_nCount = pEntry->m_count;
            }
            s.m_bGreyOut = pEntry->m_bDisabled;

            pButton->SetToolTipStrRef(pEntry->m_name == -1 ? 0x923A : pEntry->m_name, -1, -1);
            pButton->SetToolTipHotKey(0xFFFF, 0xFFFF, sLabel);
            s.field_0 = 1;
        }
        break;
    }
    case 0x7B: {
        // Modal-feat rank picker (0x58F4AF).  Same five entries as the innate
        // picker, but here the list holds one entry per RANK and the entry
        // whose count equals the rank the player has dialled in is the
        // selected one.  Arterial Strike, Hamstring and Rapid Shot have a
        // single rank, so they take the rank test alone.  This arm writes no
        // grey-out, and it is the second of the two that hand
        // SetToolTipStrRef a third argument off the entry.
        if (g_pButtonArrayPickerList == NULL) {
            break;
        }

        INT nFirst = 0;
        INT nLast = 0x0B;
        if (g_pButtonArrayPickerList->GetCount() > 12) {
            nFirst = 1;
            nLast = 0x0A;
        }

        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        for (INT nSlot = nFirst; nSlot <= nLast; nSlot++) {
            CInfButtonSettings& s = m_buttonArray[nSlot];
            CUIControlBase* pControl = pPanel->GetControl(nSlot + 6);
            if (pControl == NULL) {
                continue;
            }

            CUIControlButton* pButton = static_cast<CUIControlButton*>(pControl);
            CString sLabel;
            sLabel.Format(_T("F%d"), nSlot + 1);

            if (pos == NULL) {
                s.field_0 = 0;
                continue;
            }

            CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
            if (pEntry == NULL) {
                s.field_0 = 1;
                continue;
            }

            if (pEntry->m_abilityId.m_res == CGameSprite::SPIN275) {
                if (pSprite->HasFeat(CGAMESPRITE_FEAT_POWER_ATTACK)
                    && pSprite->GetFeatRank(CGAMESPRITE_FEAT_POWER_ATTACK) > 0
                    && pEntry->m_count == pSprite->GetFeatRank(CGAMESPRITE_FEAT_POWER_ATTACK)) {
                    s.m_bSelected = 1;
                }
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN276) {
                if (pSprite->HasFeat(CGAMESPRITE_FEAT_EXPERTISE)
                    && pSprite->GetFeatRank(CGAMESPRITE_FEAT_EXPERTISE) > 0
                    && pEntry->m_count == pSprite->GetFeatRank(CGAMESPRITE_FEAT_EXPERTISE)) {
                    s.m_bSelected = 1;
                }
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN277) {
                if (pSprite->HasFeat(CGAMESPRITE_FEAT_ARTERIAL_STRIKE)
                    && pSprite->GetFeatRank(CGAMESPRITE_FEAT_ARTERIAL_STRIKE) > 0) {
                    s.m_bSelected = 1;
                }
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN278) {
                if (pSprite->HasFeat(CGAMESPRITE_FEAT_HAMSTRING)
                    && pSprite->GetFeatRank(CGAMESPRITE_FEAT_HAMSTRING) > 0) {
                    s.m_bSelected = 1;
                }
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN279) {
                if (pSprite->HasFeat(CGAMESPRITE_FEAT_RAPID_SHOT)
                    && pSprite->GetFeatRank(CGAMESPRITE_FEAT_RAPID_SHOT) > 0) {
                    s.m_bSelected = 1;
                }
            }

            if (pEntry->m_icon != "") {
                s.m_iconCell.SetResRef(pEntry->m_icon, g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            } else {
                s.m_iconCell.SetResRef(CResRef("STONSPEC"), g_pBaldurChitin->GetDoubleSize(), TRUE, TRUE);
            }
            if (pEntry->m_bDisplayCount) {
                s.m_nCount = pEntry->m_count;
            }

            STRREF nTip = pEntry->m_abilityId.m_strDescription;
            if (nTip == -1) {
                pButton->SetToolTipStrRef(0x923B, -1, -1);
            } else {
                pButton->SetToolTipStrRef(nTip, -1, pEntry->m_abilityId.m_strTooltipDesc);
            }
            pButton->SetToolTipHotKey(0xFFFF, 0xFFFF, sLabel);
            s.field_0 = 1;
        }
        break;
    }
    default:
        // 0x6E, 0x6F, 0x72 and 0x74-0x79 reach the table and take its default;
        // every state outside 0x65-0x7B never reaches it at all (0x58E57A
        // range-checks first).  Neither changes anything.
        break;
    }

    if (rc == CGameObjectArray::SUCCESS) {
        pGame->GetObjectArray()->ReleaseShare(nLeaderId,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }

    if (g_pBaldurChitin->m_pEngineWorld->m_nPopupState == -1) {
        pPanel->InvalidateRect(NULL);
    }
}

// 0x5950F0
//
// Paint one action-bar slot.  Faithful reconstruction of the binary:
//   1. Gate on the active engine + its video mode being live.
//   2. Compute the icon rect (slot + 3px, + another 2px while pressed) and
//      clip it to the dirty rect.
//   3. Skip inactive slots (field_0 == 0); a live slot with no button
//      (m_bActive == 0) bails out.
//   4. Active-weapon-set highlight ring (HIGHLGHT) when selected-off.
//   5. Background cell (m_countCell, normally empty) painted first.
//   6. Foreground icon (m_iconCell) via CIcon::RenderIcon, branched on the
//      slot's button type (m_buttonTypes):
//        - spell / ability / song (0x46-0x4E, 0x5A-0x62): count badge, with a
//          forced count digit for the generic STONSPEL / STONSPEC slot art.
//        - quick item (0x50-0x52): resolve the looter's quick item, run
//          CheckItemUsable, then tint -- UMD-usable (==2) gets a yellow icon
//          tint plus a STORTIN4 overlay, unusable (==0) gets a STORTINT
//          overlay, freely usable (==1) gets neither.
//        - anything else: a plain icon.
// The greyout flag is the value 2, which CIcon::RenderIcon maps to its
// TINT_INVALID path -- it suppresses the yellow tint, so a greyed quick item
// reads grey, not yellow.
BOOL CInfButtonArray::RenderButton(CPoint pt, const CRect& rClip, BOOL bPressed, INT nButton)
{

    INT nScale = g_pBaldurChitin->field_4A2C != 0 ? 2 : 1;
    CSize size(nScale * CIcon::ICON_SIZE_SM.cx, nScale * CIcon::ICON_SIZE_SM.cy);

    if (nButton < 0 || nButton >= 12) {
        return TRUE;
    }

    CInfButtonSettings& settings = m_buttonArray[nButton];

    // Display not live yet -> paint nothing.
    if (g_pBaldurChitin->pActiveEngine == NULL
        || g_pBaldurChitin->pActiveEngine->pVidMode == NULL) {
        return FALSE;
    }

    CPoint ptIcon(pt.x + 3 * nScale, pt.y + 3 * nScale);
    if (bPressed) {
        ptIcon.x += 2 * nScale;
        ptIcon.y += 2 * nScale;
    }

    CRect rIcon(ptIcon, size);
    CRect rClipIcon;
    rClipIcon.IntersectRect(rIcon, rClip);

    if (settings.field_0 == 0) {
        return TRUE;
    }
    if (settings.m_bActive == 0) {
        return FALSE;
    }

    // Active-weapon-set highlight ring (HIGHLGHT), suppressed while selected.
    // The original also clears m_bCacheHeader on the loaded cell, but that is
    // always FALSE here (the resref is never empty), so it is a no-op.
    if (settings.m_bActiveWeaponSet != 0 && settings.m_bSelected == 0) {
        // dwFlags = m_bGreyOut ? 0x80000 : 0 (binary 0x5951a5: edi loaded from
        // m_bGreyOut, neg/sbb/and 0x80000, reused at the HIGHLGHT Render).
        DWORD dwHighlightFlags = settings.m_bGreyOut ? 0x80000 : 0;
        CVidCell cHighlight;
        // bDoubleSize tracks the display double-size mode (binary 0x5952d9 reads
        // g_pBaldurChitin->field_4A2C into the cell), NOT a constant -- hardcoding
        // TRUE loaded the 2x HIGHLGHT in single-size mode, so the border rendered
        // twice button size (only its top-left quarter visible inside the slot).
        cHighlight.SetResRef(CResRef("HIGHLGHT"), nScale == 2, TRUE, FALSE);
        cHighlight.Render(0, ptIcon.x, ptIcon.y, rClipIcon, NULL, 0, dwHighlightFlags, -1);
    }

    // m_bGreyOut -> CIcon::RenderIcon flag 2 (TINT_INVALID), else no flags.
    DWORD dwFlags = settings.m_bGreyOut ? 2 : 0;

    // Background cell, painted first.  m_countCell is the slot's secondary
    // layer (UpdateButtons leaves it empty for most slots, so this no-ops).
    CIcon::RenderIcon(0, ptIcon, size, rClipIcon, settings.m_countCell.GetResRef(),
        nScale == 2, dwFlags, 0, FALSE, 0, FALSE, 0);

    INT nType = m_buttonTypes[nButton];
    if ((nType >= 0x46 && nType <= 0x4E) || (nType >= 0x5A && nType <= 0x62)) {
        // Spell / ability / song.  The generic STONSPEL / STONSPEC slot art
        // forces the count digit to render even at zero.
        BOOL bForceCount = settings.m_iconCell.GetResRef() == CResRef("STONSPEL")
            || settings.m_iconCell.GetResRef() == CResRef("STONSPEC");
        CIcon::RenderIcon(0, ptIcon, size, rClipIcon, settings.m_iconCell.GetResRef(),
            nScale == 2, dwFlags, static_cast<WORD>(settings.m_nCount), bForceCount, 0, FALSE, 0);
    } else if (nType == 0x50 || nType == 0x51 || nType == 0x52) {
        // Quick item.  Resolve the looter (selected portrait's sprite),
        // fetch the quick item in this slot and check usability.
        CInfGame* pGame = g_pBaldurChitin->m_pObjectGame;
        SHORT nPortrait = g_pBaldurChitin->m_pEngineWorld->GetSelectedCharacter();
        LONG nLooterId = -1;
        if (nPortrait < pGame->m_nCharacters) {
            nLooterId = pGame->m_characterPortraits[nPortrait];
        }

        INT nUsable = 1;
        CGameSprite* pLooter;
        BYTE share;
        do {
            pLooter = NULL;
            share = pGame->GetObjectArray()->GetShare(nLooterId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pLooter), INFINITE);
        } while (share == CGameObjectArray::SHARED || share == CGameObjectArray::DENIED);

        if (share == CGameObjectArray::SUCCESS) {
            if (pLooter != NULL) {
                CItem* pItem = pLooter->GetQuickItem(static_cast<BYTE>(nType - 0x50));
                if (pItem != NULL) {
                    nUsable = pGame->CheckItemUsable(pLooter, pItem);
                }
            }
            pGame->GetObjectArray()->ReleaseShare(nLooterId,
                CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }

        // Binary 0x5955a6: rgbTint = (nUsable == 2) ? *(COLORREF*)0x84ebb0 : 0,
        // and *(COLORREF*)0x84ebb0 == 0x0000FFFF (yellow).  The UMD-usable icon
        // is tinted yellow; a normal/unusable icon is not.
        COLORREF rgbTint = (nUsable == 2) ? 0x0000FFFF : 0;
        CIcon::RenderIcon(0, ptIcon, size, rClipIcon, settings.m_iconCell.GetResRef(),
            nScale == 2, dwFlags, static_cast<WORD>(settings.m_nCount), FALSE, 0, FALSE, rgbTint);

        if (nUsable == 2 || nUsable == 0) {
            // Both overlays blit translucent (binary 0x595662 / 0x595714 both
            // push dwFlags=2=CVIDIMG_TRANSLUCENT, nTransVal=0xC0) -- the STORTINT
            // path hardcodes 2, not nUsable.
            CVidCell cTint(CResRef(nUsable == 2 ? "STORTIN4" : "STORTINT"),
                g_pBaldurChitin->m_pEngineWorld->GetManager()->m_bDoubleSize);
            cTint.Render(0, ptIcon.x, ptIcon.y, rClipIcon, NULL, 0, 2, 0xC0);
        }
    } else {
        CIcon::RenderIcon(0, ptIcon, size, rClipIcon, settings.m_iconCell.GetResRef(),
            nScale == 2, dwFlags, static_cast<WORD>(settings.m_nCount), FALSE, 0, FALSE, 0);
    }

    return TRUE;
}

// 0x5957C0
//
// Overlay-bezel companion to RenderButton, called first by
// CUIControlButtonAction::Render.  It paints the 38x38 bezel layer and
// reports (via the return value) whether the caller still needs to paint the
// plain GUIBTBUT base bezel underneath:
//   - GUIBTACT-style action slots (m_bHasOverlay, normal frame >= 0) bake
//     their own bezel into field_17C2 -> paint it, return TRUE (skip base).
//   - other slots paint nothing here unless selected, in which case the
//     selection marker (field_16E8) is drawn and TRUE returned (so the base
//     bezel -- which would otherwise show its own selection square -- is
//     skipped).  Unselected non-overlay slots return FALSE so the caller
//     paints the base bezel and RenderButton lays the icon on top.
// A greyed-out slot tints both overlay cells grey (0x00B4B4B4).
BOOL CInfButtonArray::RenderButtonOverlay(CPoint pt, const CRect& rClip, BOOL bPressed, INT nButton)
{
    if (nButton < 0 || nButton >= 12) {
        return TRUE;
    }

    CInfButtonSettings& settings = m_buttonArray[nButton];

    DWORD dwFlags;
    if (settings.m_bGreyOut != 0) {
        field_17C2.SetTintColor(0x00B4B4B4);
        field_16E8.SetTintColor(0x00B4B4B4);
        dwFlags = 0xA0000;
    } else {
        dwFlags = 0;
    }

    // Display not live yet -> paint nothing, let the caller draw the base.
    if (g_pBaldurChitin->pActiveEngine == NULL
        || g_pBaldurChitin->pActiveEngine->pVidMode == NULL) {
        return FALSE;
    }

    INT nScale = g_pBaldurChitin->field_4A2C != 0 ? 2 : 1;
    CRect rOverlay(pt, CSize(38 * nScale, 38 * nScale));
    CRect rClipOverlay;
    rClipOverlay.IntersectRect(rOverlay, rClip);

    if (settings.field_0 == 0) {
        return TRUE;
    }

    if (settings.m_bHasOverlay != 0 && settings.m_nIconNormalFrame >= 0) {
        // Action slot: the bezel + icon are one BAM in field_17C2.
        field_17C2.SequenceSet(0);
        INT nFrame = settings.m_bSelected != 0
            ? settings.m_nIconSelectedFrame
            : settings.m_nIconNormalFrame;
        if (bPressed) {
            nFrame++;
        }
        field_17C2.FrameSet(static_cast<SHORT>(nFrame));
        field_17C2.Render(0, pt.x, pt.y, rClipOverlay, NULL, 0, dwFlags, -1);
        return TRUE;
    }

    // Non-overlay slot: only the selection marker, and only while selected.
    if (settings.m_bSelected == 0) {
        return FALSE;
    }

    field_16E8.SequenceSet(0);
    INT nFrame = nButton * 2 + 0x18;
    if (bPressed) {
        nFrame++;
    }
    field_16E8.FrameSet(static_cast<SHORT>(nFrame));
    field_16E8.Render(0, pt.x, pt.y, rClipOverlay, NULL, 0, dwFlags, -1);
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

// Named for what it does: it has no BG2 counterpart -- IWD2's modal feats do
// not exist in Baldur's Gate II, and the BG2 PDB has no CInfButtonArray method
// of this shape.  Declared as a member because the call site at 0x58CE0E loads
// ecx with the array before the call, even though the body never touches it.
//
// 0x595EB0
BOOLEAN CInfButtonArray::IsUsingModalFeat(CGameSprite* pSprite)
{
    // __FILE__: C:\Projects\Icewind2\src\Baldur\InfButtonArray.cpp
    // __LINE__: 7136
    UTIL_ASSERT(pSprite != NULL);

    // The five feats CGameSprite::GetFeatRank knows a rank slot for, in the
    // order the binary tests them.  GetFeatRank returns the rank the player has
    // dialled in, so "> 0" is "this modal is running", and the HasFeat test
    // behind it re-checks the feat's own prerequisites.
    static const UINT nModalFeats[] = {
        CGAMESPRITE_FEAT_POWER_ATTACK,
        CGAMESPRITE_FEAT_EXPERTISE,
        CGAMESPRITE_FEAT_ARTERIAL_STRIKE,
        CGAMESPRITE_FEAT_HAMSTRING,
        CGAMESPRITE_FEAT_RAPID_SHOT,
    };

    for (INT nFeat = 0; nFeat < 5; nFeat++) {
        if (pSprite->GetFeatRank(nModalFeats[nFeat]) > 0
            && pSprite->HasFeat(nModalFeats[nFeat])) {
            return 1;
        }
    }

    return 0;
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

// Cap on how many attack-bonus points a modal feat may take.
//
// 0x85BCB4
static const INT MAX_FEAT_POINTS = 5;

// The state 0x7B picker: how many attack-bonus points to sink into Power
// Attack or Expertise.  Each feat offers "Off" plus one entry per point, up to
// the sprite's base attack bonus and never more than MAX_FEAT_POINTS.  Any
// other ability leaves the list empty.
//
// 0x587DF0
CGameButtonList* CInfButtonArray::BuildFeatPointsPickerList(const CResRef& resRef)
{
    CGameButtonList* pButtons = NULL;

    if (g_pBaldurChitin->GetObjectGame()->GetGroup()->GetCount() == 0) {
        return NULL;
    }

    LONG* pGroupList = g_pBaldurChitin->GetObjectGame()->GetGroup()->GetGroupList();
    LONG nCharacterId = pGroupList[0];
    delete pGroupList;

    CGameSprite* pSprite;

    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(nCharacterId,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return NULL;
    }

    // Only nBaseAttackBonus is used here; the other two outputs are scratch,
    // which is why the two calls pass them in opposite order.
    INT nBaseAttackBonus;
    INT nAttackCount;
    INT nAttackDivisor;
    BOOL bHasFeat = FALSE;

    if (resRef == CGameSprite::SPIN275) {
        if (pSprite->HasFeat(CGAMESPRITE_FEAT_POWER_ATTACK)) {
            g_pBaldurChitin->GetObjectGame()->GetRuleTables().GetBaseCombatValues(pSprite,
                nBaseAttackBonus, nAttackCount, nAttackDivisor, FALSE);
            bHasFeat = TRUE;
        }
    } else if (resRef == CGameSprite::SPIN276) {
        if (pSprite->HasFeat(CGAMESPRITE_FEAT_EXPERTISE)) {
            g_pBaldurChitin->GetObjectGame()->GetRuleTables().GetBaseCombatValues(pSprite,
                nBaseAttackBonus, nAttackDivisor, nAttackCount, FALSE);
            bHasFeat = TRUE;
        }
    }

    if (bHasFeat) {
        INT nPoints = nBaseAttackBonus;
        if (nPoints > MAX_FEAT_POINTS) {
            nPoints = MAX_FEAT_POINTS;
        }

        pButtons = pSprite->GetFeatPointsButtonList(resRef, nPoints + 1);
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(nCharacterId,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return pButtons;
}

// Keyboard route into the action bar: CScreenWorld::OnKeyDown maps a shortcut
// key to a button type and calls this.  It only acts while the bar is in its
// normal state, and only for the seven types DispatchActionBarClick owns.  The
// return value tells the caller whether the bar took the key.
//
// 0x5959A0
//
// Is this button type enabled for the group leader right now?  The switch maps
// the button type onto its slot in CDerivedStats::m_disabledButtons -- the
// table at 0x595C00 pairs the modal buttons and the three quick-slot banks with
// a slot and leaves every other type out.  A type with no slot answers TRUE,
// and so do an empty group and a leader the object array will not share:
// nothing is disabled unless the sprite says so.  UpdateButtons' tail turns a
// FALSE into that slot's grey-out.
BOOL CInfButtonArray::CheckActivation(LONG nButtonType)
{
    INT nDisabledButton;

    switch (nButtonType) {
    case 0x02: nDisabledButton = 0x0C; break;
    case 0x03: nDisabledButton = 0x02; break;
    case 0x0B: nDisabledButton = 0x00; break;
    case 0x0C: nDisabledButton = 0x01; break;
    case 0x46: nDisabledButton = 0x03; break;
    case 0x47: nDisabledButton = 0x04; break;
    case 0x48: nDisabledButton = 0x05; break;
    case 0x49: nDisabledButton = 0x06; break;
    case 0x4A: nDisabledButton = 0x07; break;
    case 0x4B: nDisabledButton = 0x08; break;
    case 0x4C: nDisabledButton = 0x09; break;
    case 0x4D: nDisabledButton = 0x0A; break;
    case 0x4E: nDisabledButton = 0x0B; break;
    case 0x5A: nDisabledButton = 0x1F; break;
    case 0x5B: nDisabledButton = 0x20; break;
    case 0x5C: nDisabledButton = 0x21; break;
    case 0x5D: nDisabledButton = 0x22; break;
    case 0x5E: nDisabledButton = 0x23; break;
    case 0x5F: nDisabledButton = 0x24; break;
    case 0x60: nDisabledButton = 0x25; break;
    case 0x61: nDisabledButton = 0x26; break;
    case 0x62: nDisabledButton = 0x27; break;
    case 0x6E: nDisabledButton = 0x0D; break;
    case 0x6F: nDisabledButton = 0x0E; break;
    case 0x70: nDisabledButton = 0x0F; break;
    case 0x71: nDisabledButton = 0x10; break;
    case 0x72: nDisabledButton = 0x11; break;
    case 0x73: nDisabledButton = 0x12; break;
    case 0x74: nDisabledButton = 0x13; break;
    case 0x75: nDisabledButton = 0x14; break;
    case 0x76: nDisabledButton = 0x15; break;
    default:
        return TRUE;
    }

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->GetGroup()->GetCount() == 0) {
        return TRUE;
    }

    LONG nLeader = pGame->GetGroup()->GetGroupLeader();
    CGameSprite* pSprite;

    BYTE rc;
    do {
        rc = pGame->GetObjectArray()->GetShare(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return TRUE;
    }

    // The binary picks the live stat block off m_bAllowEffectListCall rather
    // than going through GetDerivedStats.
    CDerivedStats* pStats = pSprite->m_bAllowEffectListCall != 0
        ? &pSprite->m_derivedStats
        : &pSprite->m_tempStats;
    BOOL bEnabled = pStats->m_disabledButtons[nDisabledButton] == 0;

    pGame->GetObjectArray()->ReleaseShare(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return bEnabled;
}

// 0x594170
//
// Run a button type's action when the bar is not showing a button for it
// -- the hot-key path out of CScreenWorld::OnKeyDown.  Named for what it
// does; the BG2 PDB's CheckActivation is the function at 0x5959A0.
BOOL CInfButtonArray::ActivateHiddenButton(LONG nButtonType)
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();

    if (m_nState != 0x72) {
        return FALSE;
    }

    LONG nLeader = pGame->GetGroup()->GetGroupLeader();
    CGameSprite* pSprite;

    BYTE rc;
    do {
        rc = pGame->GetObjectArray()->GetDeny(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED);

    if (rc != CGameObjectArray::SUCCESS) {
        return FALSE;
    }

    switch (nButtonType) {
    case 3:
    case 5:
    case 10:
    case 0xE:
    case 0x50:
    case 0x51:
    case 0x52:
        DispatchActionBarClick(nButtonType, pSprite);
        break;
    }

    pGame->GetObjectArray()->ReleaseDeny(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return TRUE;
}

// Handle the seven action-bar button types that need the party leader's sprite:
// Cast Spell, Skills, Special Abilities, Use Item and the three quick-item
// slots.  Reached from OnLButtonPressed and from the hotkey path at 0x594170.
//
// 0x594280
void CInfButtonArray::DispatchActionBarClick(INT nButtonType, CGameSprite* pSprite)
{
    UTIL_ASSERT(pSprite != NULL);

    switch (nButtonType) {
    case 3: {
        // Cast Spell.  Count the spellcasting classes that still have a
        // memorised level, plus the domain pool.  More than one caster source
        // opens the class picker (0x76); exactly one goes straight to that
        // class's spellbook (0x67); none does nothing at all.
        g_pBaldurChitin->GetObjectGame()->m_nState = 0;
        UpdateButtons();

        BYTE nClass = 0;
        UINT nCount = 0;

        for (UINT nClassIndex = 0; nClassIndex < CSPELLLIST_NUM_CLASSES; nClassIndex++) {
            if (nCount > 1) {
                break;
            }

            UINT nLevel = 0;
            BOOL bHasSpells = FALSE;
            while (nLevel < pSprite->m_spells.m_spellsByClass[nClassIndex].m_nHighestLevel) {
                if (pSprite->m_spells.Get(nClassIndex)->GetSpellsAtLevel(nLevel)->m_nSharedCurrent != 0) {
                    bHasSpells = TRUE;
                    break;
                }

                nLevel++;
            }

            if (bHasSpells) {
                nClass = g_pBaldurChitin->GetObjectGame()->GetSpellcasterClass(nClassIndex);
                nCount++;
            }
        }

        if (pSprite->m_domainSpells.m_nHighestLevel != 0) {
            for (UINT nLevel = 0; nLevel < pSprite->m_domainSpells.m_nHighestLevel; nLevel++) {
                if (pSprite->m_domainSpells.GetSpellsAtLevel(nLevel)->m_nSharedCurrent != 0) {
                    if (nCount == 0) {
                        // A cleric whose only memorised spells are domain
                        // spells still goes straight to the spellbook, with
                        // the specialization steering it to the domain list.
                        nClass = CAIOBJECTTYPE_C_CLERIC;
                        m_nCurrentSelectedSpellLevel = pSprite->m_baseStats.m_specialization;
                    }

                    nCount++;
                    break;
                }

                m_nCurrentSelectedSpellLevel = 0;
            }
        }

        if (nCount > 1) {
            SetState(0x76, 1);
            return;
        }

        if (nCount == 1) {
            m_nCurrentSelectedSpellClass = nClass;
            SetState(0x67, 1);
            return;
        }

        break;
    }
    case 5:
        // Skills.  While a modal skill is running the button just cancels it;
        // otherwise it opens the skills submenu.
        if (pSprite->m_nModalState != 0) {
            if (pSprite->m_nModalState == 3) {
                pSprite->SetModalState(0, 0);

                // 0x85BD1C
                pSprite->m_nStealthGreyOut = 90;
            } else {
                pSprite->SetModalState(0, 0);
            }

            m_nSelectedButton = 100;
            UpdateButtons();
            return;
        }

        g_pBaldurChitin->GetObjectGame()->m_nState = 0;
        UpdateButtons();
        SetState(0x73, 1);
        return;
    case 10:
        // Special Abilities.
        g_pBaldurChitin->GetObjectGame()->m_nState = 0;
        UpdateButtons();
        SetState(0x6A, 1);
        return;
    case 0xE:
        // Use Item.
        g_pBaldurChitin->GetObjectGame()->m_nState = 0;
        m_nSelectedButton = 0xE;
        pSprite->SetModalState(0, 0);
        UpdateButtons();
        SetState(0x69, 1);
        return;
    case 0x50:
    case 0x51:
    case 0x52: {
        // Quick item.  An item with two or more usable abilities opens the
        // ability picker; a single-ability item is readied straight away.
        CGameButtonList* pUsages = pSprite->GetItemUsages(
            static_cast<SHORT>(nButtonType - 0x41), 3, -1);

        if (pUsages != NULL && pUsages->GetCount() > 1) {
            m_nSelectedButton = nButtonType;
            m_nCustomizeSlot = nButtonType - 0x50;
            SetState(0x68, 1);
        } else {
            INT nPreviousButton = m_nSelectedButton;
            g_pBaldurChitin->GetObjectGame()->m_nState = 0;

            if (nPreviousButton == nButtonType) {
                m_nSelectedButton = 100;
                UpdateButtons();
            } else {
                m_nSelectedButton = nButtonType;
                pSprite->SetModalState(0, 0);
                ReadyQuickSlotByMode(static_cast<SHORT>(nButtonType - 0x50), 3);

                if (g_pBaldurChitin->GetObjectGame()->m_nState == 0) {
                    m_nSelectedButton = 100;
                }
            }

            // No UpdateButtons() on this arm.  The ready path sets
            // m_nSelectedButton but never re-syncs, so the slot's m_bSelected
            // stays clear and a quick item shows no red selection square
            // (Frida on the original 2026-06-20: m_nSelectedButton=0x50 yet the
            // button's m_bSelected=0).  The eventual cast-completion
            // UpdateState resets the selection.
        }

        if (pUsages != NULL) {
            while (pUsages->GetCount() != 0) {
                delete pUsages->RemoveHead();
            }

            pUsages->RemoveAll();
            delete pUsages;
        }

        break;
    }
    }
}

// Page the spellbook picker down by a whole memorised level instead of by ten
// entries: walk forward from the current page and stop on the first entry
// whose level differs from the page's own.  For spell entries m_bCanUse
// carries the level.  The list is passed in only to be checked; the walk
// itself runs on g_pButtonArrayPickerList.
//
// 0x595FB0
INT CInfButtonArray::GetNextPickerPage(CGameButtonList* pButtonList)
{
    UTIL_ASSERT(pButtonList != NULL);

    INT nSeen = 0;
    INT nPageLevel = 0;

    POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
    while (pos != NULL) {
        CButtonData* pButtonData = g_pButtonArrayPickerList->GetNext(pos);
        if (pButtonData == NULL) {
            continue;
        }

        if (nSeen == 0) {
            nPageLevel = pButtonData->m_abilityId.m_bCanUse;
        } else if (pButtonData->m_abilityId.m_bCanUse != nPageLevel) {
            return m_nListStartIndex + nSeen;
        }

        nSeen++;
    }

    return 0;
}

// Page the spellbook picker up by a whole memorised level instead of by ten
// entries: walk backwards from the current page until the entry level changes
// twice, and land on the first entry of that previous level.  For spell
// entries m_bCanUse carries the level.  The list is passed in only to be
// checked; the walk itself runs on g_pButtonArrayPickerList.
//
// 0x596040
INT CInfButtonArray::GetPreviousPickerPage(CGameButtonList* pButtonList)
{
    UTIL_ASSERT(pButtonList != NULL);

    INT nSeen = 0;
    INT nPageLevel = 0;
    INT nPreviousLevel = -1;

    POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
    while (pos != NULL) {
        CButtonData* pButtonData = g_pButtonArrayPickerList->GetPrev(pos);
        if (pButtonData == NULL) {
            continue;
        }

        if (nSeen == 0) {
            nPageLevel = pButtonData->m_abilityId.m_bCanUse;
        } else if (nPreviousLevel == -1
            && pButtonData->m_abilityId.m_bCanUse != nPageLevel) {
            nPreviousLevel = pButtonData->m_abilityId.m_bCanUse;
        } else if (pButtonData->m_abilityId.m_bCanUse != nPreviousLevel) {
            INT nPage = m_nListStartIndex - nSeen + 1;
            return nPage < 0 ? 0 : nPage;
        }

        nSeen++;
    }

    return 0;
}

// The binary holds ONE lock for the whole of this function and takes no other:
// a CGameObjectArray::GetDeny on the party leader at 0x590027, retried while
// the array answers SHARED, abandoned when it answers anything but SUCCESS, and
// released exactly once at 0x593A13 on the single exit every arm jumps to.
// There is no GetShare in the 3,996 instructions the function spans, and no arm
// takes a lock of its own -- so the leader's sprite the arms below use is the
// one this prologue obtained, not one they fetch for themselves.
//
// The state dispatch is a 23-entry table at 0x593A3C indexed by m_nState - 0x65,
// with fifteen distinct targets; states 0x6F and 0x72 share the switch default,
// which is why the action bar lives there.  Two states our source did not name
// at all have their own arms: 0x65 (0x590450) and 0x76 (0x5918F3).
//
// The binary returns 1 for a click it took and 0 for one it refused before the
// lock, but the call site at 0x687CAD discards eax, so the signature stays
// void, as OnRButtonPressed's does.
//
// 0x58FF20
void CInfButtonArray::OnLButtonPressed(int buttonID)
{
    // NOTE: the binary's bounds test at 0x58FF51 rejects buttonID < 0 and
    // buttonID > 12, so an id of exactly 12 reads one past m_buttonTypes[12]
    // and m_buttonArray[12].  Both land inside the object so the original does
    // not fault, but nothing calls it with 12 and reproducing the read would be
    // writing deliberate out-of-bounds C++ for no observable gain -- the same
    // call OnRButtonPressed already makes.
    if (buttonID < 0 || buttonID >= 12) {
        return;
    }

    INT nButtonType = m_buttonTypes[buttonID];

    // The grey-out gate at 0x58FF77.  A greyed slot refuses the click in the
    // action bar outright; in the spellbook it only lets the twelve picker
    // types through; and in every other state it lets through the spell-class
    // buttons, the quick items, and the empty-slot placeholder -- nothing else.
    // Unlike the corresponding chain in OnRButtonPressed this one is LIVE: each
    // range here is a real two-sided test, not the forward jump over its own
    // upper bound that made the right-button chain dead code.
    if (m_buttonArray[buttonID].m_bGreyOut) {
        if (m_nState == 0x72) {
            return;
        }
        if (m_nState == 0x66) {
            if (nButtonType < 0x15 || nButtonType > 0x20) {
                return;
            }
        } else if (!((nButtonType >= 0x32 && nButtonType <= 0x38)
                       || (nButtonType >= 0x50 && nButtonType <= 0x52)
                       || nButtonType == 0x64)) {
            return;
        }
    }

    CInfGame* pGame = g_pBaldurChitin->m_pObjectGame;

    // Written twice, in that order, at 0x58FFBF and again at 0x58FFDD.  The
    // duplicate is the binary's: both stores hit the same three fields with the
    // same values, and no call sits between them.
    pGame->m_lastClick = CPoint(-1, -1);
    pGame->m_lastTarget = CGameObjectArray::INVALID_INDEX;
    pGame->m_lastClick = CPoint(-1, -1);
    pGame->m_lastTarget = CGameObjectArray::INVALID_INDEX;

    LONG nLeader = pGame->GetGroup()->GetGroupLeader();
    CGameSprite* pSprite = NULL;
    BYTE rc;
    do {
        rc = pGame->GetObjectArray()->GetDeny(nLeader,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED);

    // The loop at 0x59002C retries on SHARED alone -- DENIED is not in the
    // compare -- and 0x59003A abandons the click on anything but SUCCESS.
    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }

    switch (m_nState) {
    case 0x65: {
        // The quick-weapon customise picker at 0x590450, opened by the action
        // bar's 0x3C..0x43 arm on a SHIFT-click.
        // The click resolves the buttonID-th entry of the picker list -- a
        // straight index, with none of the page-up offset the spellbook picker
        // applies -- and binds it to the slot stashed in m_nCustomizeSlot.
        // Whether anything was bound is what decides, at 0x59053F, if the
        // selection is cleared on the way out.
        BOOL bBound = FALSE;
        if (g_pButtonArrayPickerList != NULL) {
            INT nCount = static_cast<INT>(g_pButtonArrayPickerList->GetCount());
            POSITION pos = g_pButtonArrayPickerList->GetHeadPosition();
            for (INT nIndex = 0; nIndex < nCount && pos != NULL; nIndex++) {
                CButtonData* pEntry = g_pButtonArrayPickerList->GetNext(pos);
                if (nIndex == buttonID && pEntry != NULL) {
                    CustomizeQuickSlot(pEntry, static_cast<BYTE>(m_nCustomizeSlot), 1);
                    ReadyQuickSlotByMode(static_cast<SHORT>(m_nCustomizeSlot), 1);
                    bBound = TRUE;
                    break;
                }
            }
        }
        UpdateButtons();
        ClearPickerList();
        PopState(0, 0);
        if (!bBound) {
            m_nSelectedButton = 100;
        }
        break;
    }
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x70:
    case 0x71:
    case 0x7A: {
        // The spell, item and song pickers -- arm at 0x59138F.  Seven states
        // share this one body in the binary; the two innate pickers below are a
        // separate arm, which is why the merged paraphrase this replaces
        // carried a shift page step into states that have none and an innate
        // grey-out exemption into states that never needed one.
        //
        // Types 0x15..0x22 dispatch through a three-slot table at 0x593BD8
        // indexed by the bytes at 0x593BE4, derived from the binary rather than
        // transcribed: 0x15..0x20 are the twelve cells, 0x21 pages up, 0x22
        // pages down.
        //
        // bUseNow is what the four Use*Action helpers turn into CGameSprite's
        // `firstCall`, so a customise click readies the slot rather than firing
        // it: the three customising states are exactly the ones missing here.
        BOOL bUseNow = (m_nState == 0x67 || m_nState == 0x69
            || m_nState == 0x7A || m_nState == 0x70);
        BOOLEAN bUsed = FALSE;

        if (g_pButtonArrayPickerList == NULL) {
            // The clear-and-repaint at 0x5918DD writes the field rather than
            // calling the setter, and does not walk the state stack back.
            m_nSelectedButton = 100;
            UpdateButtons();
            break;
        }

        if (nButtonType < 0x15 || nButtonType > 0x22) {
            // The range assert inlined at 0x5918C4 is left out: ours is
            // __declspec(noreturn) and the original's is not.  Its fallthrough
            // is the same clear-and-repaint as the missing list above.
            m_nSelectedButton = 100;
            UpdateButtons();
            break;
        }

        if (nButtonType == 0x21) {
            // Page up, 0x5913E8.  m_nListStartIndex is an entry index, not a
            // page number, so a step is ten entries clamped at zero -- unless
            // this is a spellbook and shift is down, which steps by a whole
            // memorised level instead.
            if (m_nListStartIndex <= 0) {
                break;
            }

            if ((m_nState == 0x67 || m_nState == 0x66)
                && g_pBaldurChitin->pActiveEngine->GetShiftKey() == 1) {
                m_nListStartIndex = GetPreviousPickerPage(g_pButtonArrayPickerList);
                UpdateButtons();
                break;
            }

            INT nPage = m_nListStartIndex - 10;
            if (nPage < 0) {
                nPage = 0;
            }

            m_nListStartIndex = nPage;
            UpdateButtons();
            break;
        }

        if (nButtonType == 0x22) {
            // Page down, 0x59143F.
            INT nLastPage = static_cast<INT>(g_pButtonArrayPickerList->GetCount()) - 10;
            if (m_nListStartIndex >= nLastPage) {
                break;
            }

            if ((m_nState == 0x67 || m_nState == 0x66)
                && g_pBaldurChitin->pActiveEngine->GetShiftKey() == 1) {
                m_nListStartIndex = GetNextPickerPage(g_pButtonArrayPickerList);
                UpdateButtons();
                break;
            }

            if (nLastPage >= m_nListStartIndex + 10) {
                m_nListStartIndex = m_nListStartIndex + 10;
            } else {
                m_nListStartIndex = nLastPage;
            }

            UpdateButtons();
            break;
        }

        // The twelve cells, 0x5914C2.  The walk counts BUTTON slots, so it
        // starts at 1 whenever the list is long enough for slot 0 to be the
        // page-up arrow.  The binary inlines GetNext as a walk of the node's
        // pNext and data fields.
        INT nIndex = (g_pButtonArrayPickerList->GetCount() > 12) ? 1 : 0;
        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        CButtonData* pEntry = NULL;
        while (pos != NULL) {
            CButtonData* pCandidate = g_pButtonArrayPickerList->GetNext(pos);
            if (nIndex == buttonID && pCandidate != NULL) {
                pEntry = pCandidate;
                break;
            }
            nIndex++;
        }

        if (pEntry != NULL) {
            // The spellbook gate at 0x591502.  Only 0x67 pays for it, and only
            // on a cell that is not greyed out: the picked spell is demanded,
            // the caster's specialisation mask resolved, and the whole dispatch
            // refused with feedback when CanCast says no.
            BOOL bDispatch = TRUE;
            if (m_nState == 0x67 && !m_buttonArray[buttonID].m_bGreyOut) {
                CSpell cSpell(pEntry->m_abilityId.m_res);
                cSpell.Demand();

                BYTE nClass = pEntry->m_abilityId.m_nClass;

                // m_nTooltip is the specialisation index, not a tooltip:
                // BuildAbilityButtonData stores GetSpecializationIndex there.
                // The name is the layout's, and this read is at +0x38.
                DWORD nSpecialization = pGame->GetRuleTables().GetSpecializationMask(
                    nClass, static_cast<BYTE>(pEntry->m_abilityId.m_nTooltip));
                cSpell.Release();

                if (!pSprite->CanCast(nClass, nSpecialization, &cSpell)) {
                    pSprite->FeedBack(CGameSprite::FEEDBACK_89, 0, 0, 0,
                        pGame->GetRuleTables().GetClassBeyondCastingAbilityStringRef(nClass),
                        0, 0);
                    bDispatch = FALSE;
                } else if (g_pBaldurChitin->pActiveEngine->GetShiftKey() == 1
                    && bUseNow == 1
                    && pEntry->m_abilityId.m_nClass == 3
                    && pEntry->m_abilityId.m_nTooltip == 0
                    && !pEntry->m_bDisabled) {
                    // NOTE: unrecovered.  A shift-click on an unspecialised
                    // class-3 cell calls a CGameSprite method at 0x716770
                    // (ObjCreature.cpp, asserts on line 17075) with this entry,
                    // and then leaves through the same tail below without
                    // dispatching.  That callee walks a two-dimensional resref
                    // table hanging off CInfGame at +0x145C, sized by the words
                    // at +0x1464 and +0x1466 and column-picked by
                    // IcewindMisc::IsEvil, which is more than can be named from
                    // this arm alone.  The control flow here is faithful; only
                    // the call is missing, so a shift-click of that one cell
                    // does nothing rather than something wrong.
                    bDispatch = FALSE;
                }
            }

            if (bDispatch) {
                // Three of the seven states are customise pickers, entered
                // at 0x59163F: they bind the picked entry to the slot stashed
                // in m_nCustomizeSlot and record the button type it now shows.
                // SetCustomButtonValue is inlined at each site -- its own
                // assert, same __LINE__ 2036, is the only bound on the index,
                // and the sprite-side write truncates it to a BYTE.
                if (m_nState == 0x66) {
                    INT nSlot = m_nCustomizeSlot;
                    CustomizeQuickSlot(pEntry, static_cast<BYTE>(nSlot), 2);
                    m_customButtonTypes[nSlot] = nSlot + 0x46;
                    pSprite->SetCustomButtonValue(static_cast<BYTE>(nSlot), nSlot + 0x46);
                    bUsed = TRUE;
                } else if (m_nState == 0x68) {
                    INT nSlot = m_nCustomizeSlot;
                    CustomizeQuickSlot(pEntry, static_cast<BYTE>(nSlot), 3);
                    m_customButtonTypes[nSlot] = nSlot + 0x50;
                    pSprite->SetCustomButtonValue(static_cast<BYTE>(nSlot), nSlot + 0x50);
                    bUsed = TRUE;
                } else if (m_nState == 0x71) {
                    INT nSlot = m_nCustomizeSlot;
                    CustomizeQuickSlot(pEntry, static_cast<BYTE>(nSlot), 6);
                    m_customButtonTypes[nSlot] = nSlot + 0x6E;
                    pSprite->SetCustomButtonValue(static_cast<BYTE>(nSlot), nSlot + 0x6E);
                    bUsed = TRUE;
                }

                // At 0x59177B a greyed cell can still be bound to a quick
                // slot, but it never fires; the helper's return overwrites the
                // flag the customise above set.
                if (!m_buttonArray[buttonID].m_bGreyOut) {
                    switch (m_nState) {
                    case 0x66:
                    case 0x67:
                        bUsed = UseSpellAction(pEntry, bUseNow);
                        break;
                    case 0x68:
                    case 0x69:
                        bUsed = UseItemAction(pEntry, bUseNow);
                        break;
                    case 0x71:
                    case 0x7A:
                        bUsed = UseSongAction(pEntry, bUseNow);
                        break;
                    default:
                        bUsed = UseInnateAction(pEntry, bUseNow);
                        break;
                    }
                }
            }
        }

        // The tail at 0x5917E3, which every path that did not page reaches.
        // ClearPickerList is inlined there in full -- RemoveHead, delete,
        // RemoveAll, the list's own scalar deleting destructor, then the global
        // cleared -- and matches 0x587BD0 instruction for instruction.  So is
        // PopState: 0x591845 is its a3 == 1 half and 0x59187A its a3 == 0 half,
        // each guarded by an emptiness test our PopState already carries.
        ClearPickerList();

        if (bUsed) {
            PopState(0, 1);
            break;
        }

        PopState(0, 0);
        m_nSelectedButton = 100;
        UpdateButtons();
        break;
    }
    case 0x6A:
    case 0x6B: {
        // The two innate pickers -- arm at 0x592428, with its own three-slot
        // table at 0x593C34 indexed by the bytes at 0x593C40.  Same three
        // groups as the arm above, and its page-up shares that arm's code by
        // tail-merge; neither page step consults the shift key.
        //
        // 0x6A fires the picked innate, 0x6B binds it to a quick slot first.
        BOOL bUseNow = (m_nState == 0x6A);
        BOOLEAN bUsed = FALSE;
        BOOL bFeatPointPicker = FALSE;

        if (g_pButtonArrayPickerList == NULL) {
            // Shared with the feat-point arm below, at 0x592E65.
            SetSelectedButton(100);
            UpdateButtons();
            break;
        }

        if (nButtonType < 0x15 || nButtonType > 0x22) {
            // Assert at 0x59282A, left out for the same noreturn reason.
            SetSelectedButton(100);
            UpdateButtons();
            break;
        }

        if (nButtonType == 0x21) {
            if (m_nListStartIndex <= 0) {
                break;
            }

            INT nPage = m_nListStartIndex - 10;
            if (nPage < 0) {
                nPage = 0;
            }

            m_nListStartIndex = nPage;
            UpdateButtons();
            break;
        }

        if (nButtonType == 0x22) {
            INT nLastPage = static_cast<INT>(g_pButtonArrayPickerList->GetCount()) - 10;
            if (m_nListStartIndex >= nLastPage) {
                break;
            }

            if (nLastPage >= m_nListStartIndex + 10) {
                m_nListStartIndex = m_nListStartIndex + 10;
            } else {
                m_nListStartIndex = nLastPage;
            }

            UpdateButtons();
            break;
        }

        INT nIndex = (g_pButtonArrayPickerList->GetCount() > 12) ? 1 : 0;
        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        CButtonData* pEntry = NULL;
        while (pos != NULL) {
            CButtonData* pCandidate = g_pButtonArrayPickerList->GetNext(pos);
            if (nIndex == buttonID && pCandidate != NULL) {
                pEntry = pCandidate;
                break;
            }
            nIndex++;
        }

        if (pEntry != NULL) {
            if (m_nState == 0x6B) {
                // From 0x592521.  This arm has no grey-out gate of any
                // kind, which is what separates it from the seven above.
                INT nSlot = m_nCustomizeSlot;
                CustomizeQuickSlot(pEntry, static_cast<BYTE>(nSlot), 4);
                m_customButtonTypes[nSlot] = nSlot + 0x5A;
                pSprite->SetCustomButtonValue(static_cast<BYTE>(nSlot), nSlot + 0x5A);
            }

            BOOL bHandled = FALSE;
            if (m_nState == 0x6A) {
                // From 0x59256C, the five modal feats do not go through
                // UseSpellAction: two of them open a point picker and three
                // toggle their rank and post the effect that carries it.
                if ((pEntry->m_abilityId.m_res == CGameSprite::SPIN275
                        && pSprite->HasFeat(CGAMESPRITE_FEAT_POWER_ATTACK))
                    || (pEntry->m_abilityId.m_res == CGameSprite::SPIN276
                        && pSprite->HasFeat(CGAMESPRITE_FEAT_EXPERTISE))) {
                    m_currentAbilityResRef = pEntry->m_abilityId.m_res;
                    bFeatPointPicker = TRUE;
                    bUsed = TRUE;
                    bHandled = TRUE;
                } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN277) {
                    pSprite->SetFeatRank(CGAMESPRITE_FEAT_ARTERIAL_STRIKE,
                        pSprite->GetFeatRank(CGAMESPRITE_FEAT_ARTERIAL_STRIKE) > 0 ? 0 : 1);

                    ITEM_EFFECT effect;
                    CGameEffect::ClearItemEffect(&effect,
                        ICEWIND_CGAMEEFFECT_FEATARTERIALSTRIKE);
                    effect.durationType = 1;

                    CGameEffect* pEffect = CGameEffect::DecodeEffect(&effect,
                        pSprite->GetPos(), pSprite->GetId(), CPoint(-1, -1));
                    CMessage* pMsg = new CMessageAddEffect(pEffect,
                        pSprite->GetId(), pSprite->GetId());
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMsg, FALSE);
                    bUsed = TRUE;
                    bHandled = TRUE;
                } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN278) {
                    pSprite->SetFeatRank(CGAMESPRITE_FEAT_HAMSTRING,
                        pSprite->GetFeatRank(CGAMESPRITE_FEAT_HAMSTRING) > 0 ? 0 : 1);

                    ITEM_EFFECT effect;
                    CGameEffect::ClearItemEffect(&effect,
                        ICEWIND_CGAMEEFFECT_FEATHAMSTRING);
                    effect.durationType = 1;

                    CGameEffect* pEffect = CGameEffect::DecodeEffect(&effect,
                        pSprite->GetPos(), pSprite->GetId(), CPoint(-1, -1));
                    CMessage* pMsg = new CMessageAddEffect(pEffect,
                        pSprite->GetId(), pSprite->GetId());
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMsg, FALSE);
                    bUsed = TRUE;
                    bHandled = TRUE;
                } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN279) {
                    pSprite->SetFeatRank(CGAMESPRITE_FEAT_RAPID_SHOT,
                        pSprite->GetFeatRank(CGAMESPRITE_FEAT_RAPID_SHOT) > 0 ? 0 : 1);

                    ITEM_EFFECT effect;
                    CGameEffect::ClearItemEffect(&effect,
                        ICEWIND_CGAMEEFFECT_FEATRAPIDSHOT);
                    effect.durationType = 1;

                    CGameEffect* pEffect = CGameEffect::DecodeEffect(&effect,
                        pSprite->GetPos(), pSprite->GetId(), CPoint(-1, -1));
                    CMessage* pMsg = new CMessageAddEffect(pEffect,
                        pSprite->GetId(), pSprite->GetId());
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMsg, FALSE);
                    bUsed = TRUE;
                    bHandled = TRUE;
                }
            }

            // The state re-test at 0x592801 is the binary's; both arms of
            // it reach here, and 0x6B always does.
            if (!bHandled && (m_nState == 0x6B || m_nState == 0x6A)) {
                bUsed = UseSpellAction(pEntry, bUseNow);
            }
        }

        // The tail at 0x5924EC.  An empty cell or an exhausted list enters it
        // one instruction earlier, at the point that reloads the flag with the
        // FALSE it started at -- so both land here with bUsed clear.
        ClearPickerList();

        if (!bUsed) {
            PopState(0, 0);
            SetSelectedButton(100);
            UpdateButtons();
            break;
        }

        if (bFeatPointPicker) {
            SetState(0x7B, 1);
            break;
        }

        // Note that 0x5939DD is NOT a SetState: it is the second push of
        // PopState, so entering the shared tail there with a 1 already pushed
        // is PopState(0, 1), unwinding the whole picker sequence at once.
        PopState(0, 1);
        break;
    }
    case 0x7B: {
        // The feat-point confirm picker -- arm at 0x592858, the third and last
        // of the three the merged case hid.  Its table is at 0x593C50 with the
        // index bytes at 0x593C5C, the same shape as the other two, and like
        // the innate arm neither page step consults the shift key.
        if (g_pButtonArrayPickerList == NULL) {
            // A missing list clears the selection and repaints, at 0x592E65,
            // without touching the state stack.
            SetSelectedButton(100);
            UpdateButtons();
            break;
        }

        if (nButtonType < 0x15 || nButtonType > 0x22) {
            // Assert at 0x592AD3, left out for the same noreturn reason.
            SetSelectedButton(100);
            UpdateButtons();
            break;
        }

        if (nButtonType == 0x21) {
            if (m_nListStartIndex <= 0) {
                break;
            }

            INT nPage = m_nListStartIndex - 10;
            if (nPage < 0) {
                nPage = 0;
            }

            m_nListStartIndex = nPage;
            UpdateButtons();
            break;
        }

        if (nButtonType == 0x22) {
            INT nLastPage = static_cast<INT>(g_pButtonArrayPickerList->GetCount()) - 10;
            if (m_nListStartIndex >= nLastPage) {
                break;
            }

            if (nLastPage >= m_nListStartIndex + 10) {
                m_nListStartIndex = m_nListStartIndex + 10;
            } else {
                m_nListStartIndex = nLastPage;
            }

            UpdateButtons();
            break;
        }

        INT nIndex = (g_pButtonArrayPickerList->GetCount() > 12) ? 1 : 0;
        POSITION pos = g_pButtonArrayPickerList->FindIndex(m_nListStartIndex);
        CButtonData* pEntry = NULL;
        while (pos != NULL) {
            CButtonData* pCandidate = g_pButtonArrayPickerList->GetNext(pos);
            if (nIndex == buttonID && pCandidate != NULL) {
                pEntry = pCandidate;
                break;
            }
            nIndex++;
        }

        // The state re-test at 0x59296B is the binary's; nothing else can
        // reach this arm, but it guards the whole body all the same.
        if (pEntry != NULL && m_nState == 0x7B) {
            // BuildFeatPointsPickerList gave every entry the number of
            // attack-bonus points it stands for, in m_count, so the click
            // writes that straight into the feat rank and posts the modal
            // effect that carries it.
            WORD effectID = 0;
            if (pEntry->m_abilityId.m_res == CGameSprite::SPIN275
                && pSprite->HasFeat(CGAMESPRITE_FEAT_POWER_ATTACK)) {
                effectID = ICEWIND_CGAMEEFFECT_FEATPOWERATTACK;
                pSprite->SetFeatRank(CGAMESPRITE_FEAT_POWER_ATTACK, pEntry->m_count);
            } else if (pEntry->m_abilityId.m_res == CGameSprite::SPIN276
                && pSprite->HasFeat(CGAMESPRITE_FEAT_EXPERTISE)) {
                effectID = ICEWIND_CGAMEEFFECT_FEATEXPERTISE;
                pSprite->SetFeatRank(CGAMESPRITE_FEAT_EXPERTISE, pEntry->m_count);
            }

            // "Off" is the zero-point entry: it also drops the stashed
            // ability, so the next open of the picker has nothing to rebuild
            // from.
            if (pEntry->m_count == 0) {
                m_currentAbilityResRef = CResRef();
            }

            // Faithful: neither resref matching leaves effectID at the 0 the
            // binary zeroes edi to at 0x592982, and it still posts the effect.
            ITEM_EFFECT effect;
            CGameEffect::ClearItemEffect(&effect, effectID);
            effect.durationType = 1;

            CGameEffect* pEffect = CGameEffect::DecodeEffect(&effect,
                pSprite->GetPos(), pSprite->GetId(), CPoint(-1, -1));
            CMessage* pMsg = new CMessageAddEffect(pEffect,
                pSprite->GetId(), pSprite->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pMsg, FALSE);
        }

        // The shared tail at 0x592AAE, which every path above that did not
        // page reaches -- an empty cell and an exhausted list included.  The
        // points are spent, so it drops the picker list rather than pushing
        // another state.
        ClearPickerList();
        PopState(0, 0);
        SetSelectedButton(100);
        UpdateButtons();
        break;
    }
    case 0x6C:
        // The formation picker at 0x590294, reached by right-clicking a quick
        // formation slot on the group bar.  It rebinds the stashed quick slot
        // (m_nCustomizeSlot, set by OnRButtonPressed state 0x6E) to the chosen
        // formation and makes it current.  It then walks the state stack back
        // rather than naming a state: the tail at 0x59031E is PopState(0, 0)
        // inlined.
        //
        // NOTE: the binary range-checks m_nCustomizeSlot against 5 before each
        // of the two reads and calls CUtil::UtilAssert on failure (lines 1499
        // and 1500 of the original file).  Our UtilAssert is declared noreturn,
        // which the original's is not -- it returns and the read proceeds -- so
        // reproducing the calls here would change the control flow the compiler
        // sees.  Left out deliberately.
        if (buttonID < 12) {
            pGame->m_gameSave.m_quickFormations[m_nCustomizeSlot] = static_cast<SHORT>(buttonID);
            pGame->m_gameSave.m_curFormation = pGame->m_gameSave.m_quickFormations[m_nCustomizeSlot];
            PopState(0, 0);
        }
        break;
    case 0x6D:
        // The formation selector at 0x59022F: no quick slot is rebound, only
        // the current formation, and the same inlined PopState(0, 0) walks
        // back.  Nothing reachable opens this state -- the only SetState(0x6D)
        // in the image is the group bar's button-type 6 arm below, and no
        // reachable group bar shows a type 6.
        if (buttonID < 12) {
            pGame->m_gameSave.m_curFormation = static_cast<SHORT>(buttonID);
            PopState(0, 0);
        }
        break;
    case 0x6E:
        // The group action bar at 0x59005F.  Its own five-arm table, which
        // lives at 0x593A98, dispatches on m_buttonTypes[buttonID] - 6 over the
        // range 0..0xE; types 9..0xE and everything outside it share the
        // do-nothing body at 0x5928A9, which is a bare UpdateButtons.
        switch (nButtonType) {
        case 6:
            // NOTE: unmeasurable.  This is the only site in the image that
            // opens state 0x6D, and it passes 0 as the second argument, not 1 --
            // so the state is pushed without being stacked.  No reachable group
            // bar shows a type 6, so the arm is recovered from the disassembly
            // at 0x5901B5 and cannot be driven.
            SetState(0x6D, 0);
            m_nSelectedButton = m_buttonTypes[buttonID];
            UpdateButtons();
            break;
        case 7:
            if (pGame->m_nState == 3) {
                pGame->m_nState = 0;
                m_nSelectedButton = 100;
            } else {
                pGame->m_nState = 3;
                m_nSelectedButton = 7;
            }
            UpdateButtons();
            break;
        case 8:
            if (pGame->m_nState == 2 && pGame->m_iconIndex == 0x0C) {
                pGame->m_nState = 0;
                m_nSelectedButton = 100;
            } else {
                pGame->m_nState = 2;
                pGame->m_iconIndex = 0x0C;
                pGame->m_iconResRef = "";
                m_nSelectedButton = 8;
            }
            UpdateButtons();
            break;
        case 0x0F:
            pGame->m_nState = 0;
            m_nSelectedButton = 100;
            pGame->GetGroup()->ClearActions();
            UpdateButtons();
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14: {
            // The five quick-formation slots.  Unlike 0x6C this one does not
            // rebind anything, and unlike 0x6D it names the state it returns
            // to rather than popping.
            SHORT nFormationSlot = static_cast<SHORT>(nButtonType - 0x10);
            pGame->m_gameSave.m_curFormation = pGame->m_gameSave.m_quickFormations[nFormationSlot];
            SetState(0x6E, 0);
            UpdateButtons();
            break;
        }
        default:
            UpdateButtons();
            break;
        }
        break;
    case 0x73:
        // The skills bar, arm at 0x590556: a BYTE index table at 0x593AD8
        // over types 4..0x77 feeding six slots at 0x593AC0, of which five are
        // real -- 4, 0x0B, 0x0C, 0x0D and 0x77, the same five the 0x74 arm
        // covers, because 0x74 is this bar's customize twin.
        //
        // The body that stood here before was copied from the ACTION BAR's
        // handlers for those same five types -- it cited their addresses,
        // namely 0x593181 and 0x593295 -- and that is not this arm.  Two
        // of the five do turn out to be the same source inlined differently,
        // but type 0x0C is not, and the difference is a real one: see its
        // own case below.
        //
        // All five, and the default, end on the same three statements.  The
        // exit is never SetState(0x72, 0): it is the stack walk, and it is
        // PopState(0, 0) here rather than the (0, 1) the customize arms use.
        switch (nButtonType) {
        case 0x04:
            // Search.  Same source as the action bar's type 4 at 0x59309E:
            // that copy calls GetModalState and jumps into the shared tail,
            // this one inlines both, and the statements are identical.
            if (pSprite->GetModalState() == 2) {
                pSprite->SetModalState(0, 0);
                SetSelectedButton(100);
            } else {
                pSprite->FeedBack(CGameSprite::FEEDBACK_SEARCHSTART, 0, 0, 0, -1, 0, 0);
                pSprite->SetModalState(2, 0);
                SetSelectedButton(5);
            }
            pGame->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x0B:
            // NOTE: partial.  Stealth, and the only body of the five still
            // unrecovered -- 215 of the arm's 573 instructions.  The modal
            // toggle below is right as far as it goes and is what the arm
            // opens with, but BOTH of its branches then issue a message that
            // is missing here:
            //
            //   modal != 3 (turning stealth ON), at 0x59058C: three
            //   CAIObjectType temporaries built with the eleven-argument ctor
            //   at 0x40AE80, an action id read from the WORD at 0x8477A6, and
            //   a CAIAction assembled field by field and handed to a
            //   CMessageAddAction -- 0xE2 bytes, vtable 0x847B40, m_action at
            //   +0x0C, caller and target both the leader's id.
            //
            //   modal == 3 (turning it off), at 0x590781: writes the dword
            //   at 0x85BD1C into the sprite's +0x727A, then clears item
            //   effect 0x88 and sends that one.
            //
            // Left as it stands rather than stubbed, because the toggle it
            // does perform is faithful and removing it would take working
            // stealth away; but it is NOT the whole arm, and the gap has a
            // measured price rather than a guessed one.  Driving
            // actionbar-lclick-skills.txt onto this very button puts the bar
            // one field of 168 away from the original: m_nSelectedButton reads
            // 100 there and 5 here.  We set 5 and stop; the original sets 5,
            // sends the action, and the action resolving is what clears the
            // selection again.  Everything else on the bar matches.
            if (pSprite->GetModalState() == 3) {
                pSprite->SetModalState(0, 0);
                SetSelectedButton(100);
            } else {
                pSprite->SetModalState(3, 0);
                SetSelectedButton(5);
            }
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x0C:
            // The thieving / disarm cursor -- and this one is NOT the action
            // bar's type 0x0C.  That arm, at 0x5930E2, leaves m_iconResRef
            // alone; this one clears it, which is what the GROUP bar's type 8
            // does.  Same three fields otherwise, and the same order of
            // stores.  It is the second time in this file that a pair of
            // same-typed arms differ by exactly that field.
            if (pGame->m_nState == 2
                && (pGame->m_iconIndex == 0x24 || pGame->m_iconIndex == 0x28)) {
                pGame->m_nState = 0;
                SetSelectedButton(100);
            } else {
                pGame->m_nState = 2;
                pGame->m_iconIndex = 0x24;
                pGame->m_iconResRef = _T("");
                SetSelectedButton(0x0C);
            }
            // Unconditional, and after the branch: arming the cursor cancels
            // whatever modal the leader was in.
            pSprite->SetModalState(0, 0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x0D: {
            // Animal Empathy.  The skill is read straight out of the sprite's
            // derived stats rather than through the getter, and a rank of zero
            // or less skips the cast without skipping the repaint below.
            if (static_cast<signed char>(
                    pSprite->m_derivedStats.m_nSkills[CGAMESPRITE_SKILL_ANIMAL_EMPATHY]) > 0) {
                CButtonData buttonData;
                pSprite->BuildAbilityButtonData(CGameSprite::SPIN108, 0, 0, buttonData);
                UseSpellAction(&buttonData, 1);
            }
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        }
        case 0x77: {
            // Wilderness Lore.  Same source as the action bar's type 0x77,
            // arm 0x593181 -- that copy calls the CPoint ctor and the
            // effect's three setters out of line, this one inlines all four,
            // and the statements match.
            ITEM_EFFECT effect;
            CGameEffect::ClearItemEffect(&effect, ICEWIND_CGAMEEFFECT_RANGERTRACKING);
            effect.targetType = 1;
            CGameEffect* pEffect = CGameEffect::DecodeEffect(&effect,
                CPoint(-1, -1),
                -1,
                CPoint(-1, -1));
            pEffect->SetSource(pSprite->GetPos());
            pEffect->SetSourceId(pSprite->GetId());
            pEffect->SetEnabled(FALSE);

            CMessage* message = new CMessageAddEffect(pEffect,
                pSprite->GetId(), pSprite->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);

            SetSelectedButton(100);
            pGame->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        }
        default:
            // Arm 0x590D91.  No repaint, unlike the 0x75 default and like the
            // 0x74 and 0x77 ones.
            ClearPickerList();
            PopState(0, 0);
            break;
        }
        break;
    case 0x74:
        // The skills submenu of the customize menu, arm at 0x590E26.  Where
        // its 0x75 sibling indexes a jump table directly, this one goes
        // through a BYTE index table at 0x593B64 covering types 4..0x77 and
        // feeding five slots at 0x593B4C -- and of the 111 types that table
        // spans, exactly five take: 4, 0x0B, 0x0C, 0x0D and 0x77.  That much
        // the paraphrase had right.
        //
        // What it did not have is that those five are five SEPARATE arms in
        // the binary, identical to the instruction -- sixty each, differing
        // only in their own branch targets.  MSVC does not merge equal switch
        // arms, so five copies is what a source spelling the body out five
        // times compiles to and one case group would have produced one arm.
        // Written out five times for that reason.  The 0x0D copy is where the
        // shared tail physically lives; the other four jump into the middle
        // of it, at 0x5912D7.
        //
        // Both exits differ from the paraphrase's SetState(0x72, 0).  A type
        // that takes drops the game state, repaints, and unwinds the whole
        // stack with PopState(0, 1).  A type that does not skips the repaint
        // entirely -- the default at 0x5912FA opens on the picker-list
        // teardown, with no UpdateButtons before it -- and steps back one
        // level with PopState(0, 0).  The 0x75 arm's default does repaint,
        // so this asymmetry is real and not a shared tail misread.
        //
        // No bound on m_nCustomizeSlot, for the reason the 0x78 arm records:
        // the assert at 0x590E81 is SetCustomButtonValue's own body inlined.
        switch (nButtonType) {
        case 0x04:
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x0B:
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x0C:
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x0D:
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x77:
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        default:
            ClearPickerList();
            PopState(0, 0);
            break;
        }
        break;
    case 0x75:
        // The customize menu, arm at 0x592B01, with its own eight-entry table
        // at 0x593C6C covering types 0x23..0x2A.  Each entry rewrites the slot
        // that OnRButtonPressed stashed in m_nCustomizeSlot, in both the array
        // this class keeps and the sprite's own copy.
        //
        // There is NO bound on m_nCustomizeSlot here: the paraphrase's
        // `>= 0 && < 9` guard was invented.  The binary indexes
        // m_customButtonTypes with the raw INT and hands the truncated BYTE to
        // SetCustomButtonValue, whose own assert is the only check in the path.
        //
        // Nor does any exit name the action bar.  Seven of the nine are the
        // shared repaint at 0x5939CF -- UpdateButtons, ClearPickerList,
        // PopState(0, 0) -- which walks the state stack back to whatever
        // pushed the customize menu; the paraphrase's SetState(0x72, 0) named a
        // destination the binary never names.  Written out at each site because
        // the sharing is the compiler's tail-merge, not the source's.
        switch (nButtonType) {
        case 0x23:  // Attack
            m_customButtonTypes[m_nCustomizeSlot] = 5;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 5);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x24:  // Cast Spell
            m_customButtonTypes[m_nCustomizeSlot] = 3;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 3);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x25:
            m_customButtonTypes[m_nCustomizeSlot] = 0xE;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0xE);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x26:
            // The one arm that goes somewhere by name, and the one whose exit
            // at 0x5939E6 skips the repaint entirely -- SetState has just
            // rebuilt the bar, so repainting it again would be wasted work.
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x78, 1);
            break;
        case 0x27:  // Innate ability
            m_customButtonTypes[m_nCustomizeSlot] = 0xA;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0xA);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x28:  // Bard song
            m_customButtonTypes[m_nCustomizeSlot] = 2;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 2);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x29:  // No action
            m_customButtonTypes[m_nCustomizeSlot] = 0x64;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x64);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x2A:
            // Restore defaults: blank all nine on the sprite, let the sprite
            // rebuild its quick slots, then read the result back.  The
            // read-back is not redundant -- ResetQuickSlots is what decides
            // what the defaults are.
            for (BYTE i = 0; i < 9; i++) {
                pSprite->SetCustomButtonValue(i, 0);
            }
            pSprite->ResetQuickSlots();
            for (BYTE i = 0; i < 9; i++) {
                m_customButtonTypes[i] = pSprite->GetCustomButtonValue(i);
            }
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        default:
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        }
        break;
    case 0x76:
        // The spell-class picker at 0x5918F3, and it is NOT the same arm as
        // the one the action bar's 0x32..0x38 bank runs even though the two
        // pick the same eight classes: this one has its own eight-entry table
        // at 0x593BF4 over types 0x32..0x39, it drops the game state by writing
        // m_nState rather than calling SetState, and its default does not
        // repaint -- it clears the selection, tears the picker list down and
        // walks the state stack back.
        //
        // Measured: a plain left click on a Cast Spell button (type 0x03) in
        // the action bar puts the original here, and the click that follows
        // opens the spellbook in state 0x67.  This is the arm that carries
        // that step.
        switch (nButtonType) {
        case 0x32:
            m_nCurrentSelectedSpellClass = 2;
            m_nCurrentSelectedSpellLevel = 0;
            pGame->m_nState = 0;
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x33:
            m_nCurrentSelectedSpellClass = 3;
            m_nCurrentSelectedSpellLevel = 0;
            pGame->m_nState = 0;
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x34:
            m_nCurrentSelectedSpellClass = 4;
            m_nCurrentSelectedSpellLevel = 0;
            pGame->m_nState = 0;
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x35:
            m_nCurrentSelectedSpellClass = 7;
            m_nCurrentSelectedSpellLevel = 0;
            pGame->m_nState = 0;
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x36:
            m_nCurrentSelectedSpellClass = 8;
            m_nCurrentSelectedSpellLevel = 0;
            pGame->m_nState = 0;
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x37:
            m_nCurrentSelectedSpellClass = 10;
            m_nCurrentSelectedSpellLevel = 0;
            pGame->m_nState = 0;
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x38:
            m_nCurrentSelectedSpellClass = 11;
            m_nCurrentSelectedSpellLevel = 0;
            pGame->m_nState = 0;
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x39:
            // The domain pool, arm at 0x59195D.  As in the action bar's bank,
            // the one entry that does not zero the level: it stores the
            // caster's own specialization, read straight from m_baseStats.
            m_nCurrentSelectedSpellClass = 3;
            m_nCurrentSelectedSpellLevel = pSprite->m_baseStats.m_specialization;
            pGame->m_nState = 0;
            UpdateButtons();
            SetState(0x67, 1);
            break;
        default:
            m_nCurrentSelectedSpellClass = 0;
            m_nCurrentSelectedSpellLevel = 0;
            ClearPickerList();
            PopState(0, 0);
            break;
        }
        break;
    case 0x77:
        // The class picker of the customize menu, arm at 0x591A44: eight
        // slots at 0x593C14 over types 0x32..0x39, one per caster source, each
        // binding the customize slot to that source.
        //
        // Eight separate arms again, sixty instructions each, and this time
        // they are NOT interchangeable copies the way the 0x74 arm's five
        // are: each carries its own type as an IMMEDIATE, in both the write
        // to m_customButtonTypes and the one to the sprite.  The 0x74 arm
        // uses the register holding nButtonType instead.  Two arms of the
        // same shape, two different things written -- so the literals are
        // spelled out here rather than folded into nButtonType, which would
        // compile to the other arm's code.
        //
        // Every arm ends on PopState(0, 1), unwinding the customize sequence
        // in one go; only the first carries the tail, and the other seven
        // jump into it at 0x591B45.  The default at 0x5921DC skips the
        // repaint and steps back one level, exactly as the 0x74 default does.
        switch (nButtonType) {
        case 0x32:
            m_customButtonTypes[m_nCustomizeSlot] = 0x32;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x32);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x33:
            m_customButtonTypes[m_nCustomizeSlot] = 0x33;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x33);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x34:
            m_customButtonTypes[m_nCustomizeSlot] = 0x34;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x34);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x35:
            m_customButtonTypes[m_nCustomizeSlot] = 0x35;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x35);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x36:
            m_customButtonTypes[m_nCustomizeSlot] = 0x36;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x36);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x37:
            m_customButtonTypes[m_nCustomizeSlot] = 0x37;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x37);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x38:
            m_customButtonTypes[m_nCustomizeSlot] = 0x38;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x38);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        case 0x39:
            m_customButtonTypes[m_nCustomizeSlot] = 0x39;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), 0x39);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
            break;
        default:
            ClearPickerList();
            PopState(0, 0);
            break;
        }
        break;
    case 0x78:
        // The quick-item picker, arm at 0x592271, reached from the customize
        // menu's type 0x26.  Only the three quick-item placeholders take.
        //
        // The two exits differ, and that difference is the whole arm.  A type
        // that took walks the stack ALL the way back -- PopState(0, 1),
        // inlined at 0x59232E as back() / clear() / SetState -- so the bar it
        // lands on is the one the customize sequence started from.  A type
        // that did not steps back a single level, PopState(0, 0) inlined
        // at 0x5923DF.  The paraphrase sent both to SetState(0x72, 0).
        //
        // No bound on m_nCustomizeSlot: the assert at 0x5922C3 is
        // SetCustomButtonValue's own body inlined here, same __LINE__ 2036 and
        // the same two strings, so the call below already carries it.
        if (nButtonType >= 0x50 && nButtonType <= 0x52) {
            m_customButtonTypes[m_nCustomizeSlot] = nButtonType;
            pSprite->SetCustomButtonValue(static_cast<BYTE>(m_nCustomizeSlot), nButtonType);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 1);
        } else {
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
        }
        break;
    case 0x79:
        // The quick-weapon picker at 0x590365, entered by right-clicking a
        // weapon slot on the action bar.  A click on a 0x3C..0x43 button hands
        // CGameSprite::SetWeaponSet the set index -- the pair (button - 0x3C)
        // halved -- then mirrors the sprite's resulting set into
        // m_nQuickWeaponSlot.  The binary range-checks that set against 4 and
        // asserts, which is left out for the reason state 0x6C records.  The
        // exit is the stack walk at 0x590409, not a named state.
        if (nButtonType >= 0x3C && nButtonType <= 0x43) {
            pSprite->SetWeaponSet(static_cast<BYTE>((nButtonType - 0x3C) / 2));
            m_nQuickWeaponSlot = pSprite->m_nWeaponSet;
        }
        UpdateButtons();
        ClearPickerList();
        PopState(0, 0);
        break;
    default:
        // States 0x6F and 0x72 share the state table's default target, which
        // is 0x592C6F, so this is the single-PC action bar and every state the
        // table does not name.  The type table at 0x593C8C covers types
        // 2..0x77; the sixty values inside that range which select the table's
        // default, and everything outside it, fall straight through to the
        // release with nothing done.  Types 6, 9 and 0x0F are among them --
        // 0x0F in particular used to have a body here that the binary does not
        // have, which is what the jump-table audit reports as DEFAULT on 15.
        switch (nButtonType) {
        case 0x02:
            // Bard song.  Already singing (modal state 1) stops it; otherwise
            // the song picker opens.  Arm at 0x59336B.
            if (pSprite->GetModalState() == 1) {
                pSprite->SetModalState(0, 0);
                SetSelectedButton(100);
                UpdateButtons();
            } else {
                g_pBaldurChitin->GetObjectGame()->SetState(0);
                UpdateButtons();
                SetState(0x7A, 1);
            }
            break;
        case 0x03:
            // Four of the seven DispatchActionBarClick types have a two
            // instruction body of their own in the binary -- they sit at
            // addresses 0x5932A5, 0x592ED9, 0x593358 and 0x593345 -- because
            // each passes its own literal rather than the runtime value.
            // Only the quick-item group
            // at 0x5935A3 passes nButtonType.  That is exactly what the
            // jump-table audit reports as SPLIT on 3, 5, 10, 14, 80, 81 and 82,
            // and merging the seven into one case is what hid it.
            DispatchActionBarClick(3, pSprite);
            break;
        case 0x04:
            // Search modal toggle, arm at 0x59309E.
            if (pSprite->GetModalState() == 2) {
                pSprite->SetModalState(0, 0);
                SetSelectedButton(100);
            } else {
                // Entering Search announces "Searching" in the combat feedback
                // before the modal state flips on, at 0x5930B3.
                pSprite->FeedBack(CGameSprite::FEEDBACK_SEARCHSTART, 0, 0, 0, -1, 0, 0);
                pSprite->SetModalState(2, 0);
                SetSelectedButton(5);
            }
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x05:
            DispatchActionBarClick(5, pSprite);
            break;
        case 0x07:
            // Arm at 0x592C9C.  Note the unconditional SetModalState(0, 0):
            // arming the attack cursor cancels Search or Stealth.
            if (g_pBaldurChitin->GetObjectGame()->GetState() == 3) {
                g_pBaldurChitin->GetObjectGame()->SetState(0);
                SetSelectedButton(100);
            } else {
                g_pBaldurChitin->GetObjectGame()->SetState(3);
                SetSelectedButton(7);
            }
            pSprite->SetModalState(0, 0);
            UpdateButtons();
            break;
        case 0x08:
            // Arm at 0x592D02.  The group bar's own type 8 arm looks identical
            // but also clears m_iconResRef; this one does not.
            if (g_pBaldurChitin->GetObjectGame()->GetState() == 2
                && g_pBaldurChitin->GetObjectGame()->GetIconIndex() == 0x0C) {
                g_pBaldurChitin->GetObjectGame()->SetState(0);
                SetSelectedButton(100);
            } else {
                g_pBaldurChitin->GetObjectGame()->SetState(2);
                g_pBaldurChitin->GetObjectGame()->SetIconIndex(0x0C);
                SetSelectedButton(8);
            }
            pSprite->SetModalState(0, 0);
            UpdateButtons();
            break;
        case 0x0A:
            DispatchActionBarClick(0x0A, pSprite);
            break;
        case 0x0B:
            // Stealth modal toggle, arm at 0x592EEC.  Toggling the modal alone
            // is not enough: entering stealth also queues an immediate Hide()
            // action so the sprite attempts to hide on the spot instead of
            // waiting for the next three-cycle modal upkeep, and leaving
            // stealth applies a FORCEVISIBLE effect so it reappears at once.
            if (pSprite->GetModalState() == 3) {
                pSprite->SetModalState(0, 0);
                pSprite->SetStealthGreyOut(90);
                SetSelectedButton(100);

                ITEM_EFFECT effect;
                CGameEffect::ClearItemEffect(&effect, CGAMEEFFECT_FORCEVISIBLE);
                effect.durationType = 1;
                CGameEffect* pEffect = CGameEffect::DecodeEffect(&effect,
                    pSprite->GetPos(),
                    pSprite->m_id,
                    CPoint(-1, -1));
                CMessage* pMessage = new CMessageAddEffect(pEffect,
                    pSprite->m_id, pSprite->m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
            } else {
                pSprite->SetModalState(3, 0);
                SetSelectedButton(5);

                CAIAction action(18 /* Hide, ACTION.IDS */, CPoint(-1, -1), 0, -1);
                CMessage* pMessage = new CMessageAddAction(action,
                    pSprite->m_id, pSprite->m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
            }
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x0C:
            // Thieving mode, arm at 0x5930E2: SetState(2) with icon index 0x24,
            // or toggle off when already in that mode under either icon.
            if (g_pBaldurChitin->GetObjectGame()->GetState() == 2
                && (g_pBaldurChitin->GetObjectGame()->GetIconIndex() == 0x24
                    || g_pBaldurChitin->GetObjectGame()->GetIconIndex() == 0x28)) {
                g_pBaldurChitin->GetObjectGame()->SetState(0);
                SetSelectedButton(100);
            } else {
                g_pBaldurChitin->GetObjectGame()->SetState(2);
                g_pBaldurChitin->GetObjectGame()->SetIconIndex(0x24);
                SetSelectedButton(0x0C);
            }
            // Entering thieving/disarm mode cancels any active modal on the
            // leader -- unconditional, at 0x593173.
            pSprite->SetModalState(0, 0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x0D:
            // Animal Empathy, arm at 0x59325A: require the skill, then build
            // the SPIN108 (Charm Animal) button data and dispatch it.
            if (static_cast<signed char>(pSprite->GetDerivedStats()->m_nSkills[CGAMESPRITE_SKILL_ANIMAL_EMPATHY]) > 0) {
                CButtonData bd;
                pSprite->BuildAbilityButtonData(CGameSprite::SPIN108, 0, 0, bd);
                UseSpellAction(&bd, 1);
            }
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x0E:
            DispatchActionBarClick(0x0E, pSprite);
            break;
        case 0x32:
            m_nCurrentSelectedSpellClass = 2;
            m_nCurrentSelectedSpellLevel = 0;
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x33:
            // Cleric, regular spells: level 0 signals the picker dispatcher to
            // use the by-class list rather than the domain pool.
            m_nCurrentSelectedSpellClass = 3;
            m_nCurrentSelectedSpellLevel = 0;
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x34:
            m_nCurrentSelectedSpellClass = 4;
            m_nCurrentSelectedSpellLevel = 0;
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x35:
            m_nCurrentSelectedSpellClass = 7;
            m_nCurrentSelectedSpellLevel = 0;
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x36:
            m_nCurrentSelectedSpellClass = 8;
            m_nCurrentSelectedSpellLevel = 0;
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x37:
            m_nCurrentSelectedSpellClass = 10;
            m_nCurrentSelectedSpellLevel = 0;
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x38:
            m_nCurrentSelectedSpellClass = 11;
            m_nCurrentSelectedSpellLevel = 0;
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x39:
            // The cleric DOMAIN button, arm at 0x5932FD.  It is the one member
            // of this bank that does NOT zero the level: it stores the caster's
            // own specialization, read straight from m_baseStats through the
            // getter at 0x5940D0, and that non-zero value is what tells the
            // picker to walk the domain pool.  Writing a 1 here, as this case
            // used to, happened to look the same on a cleric whose
            // specialization is 1.
            m_nCurrentSelectedSpellClass = 3;
            m_nCurrentSelectedSpellLevel = pSprite->m_baseStats.m_specialization;
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            SetState(0x67, 1);
            break;
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43: {
            // Quick weapon click, arm at 0x592D92.  The virtual the binary
            // calls through the active engine's slot 0x44 is GetShiftKey, so
            // SHIFT-clicking a weapon button opens the full picker in state
            // 0x65 with the slot stashed; a plain click selects the weapon set
            // in place.
            if (g_pBaldurChitin->GetActiveEngine()->GetShiftKey() == 1) {
                g_pBaldurChitin->GetObjectGame()->SetState(0);
                SetSelectedButton(100);
                m_nCustomizeSlot = m_buttonTypes[buttonID] - 0x3C;
                UpdateButtons();
                SetState(0x65, 1);
                break;
            }

            // An off-hand button collapses onto its main hand when the main is
            // empty or holds a two-hander, a bow, a sling or a crossbow.  The
            // equipment index the binary computes is m_items[type - 0x11].
            INT nEffective = m_buttonTypes[buttonID];
            if (nEffective == 0x3D || nEffective == 0x3F
                || nEffective == 0x41 || nEffective == 0x43) {
                CItem* pMain = pSprite->GetEquipment()->m_items[nEffective - 0x11];
                if (pMain == NULL
                    || pMain->GetItemType() == 0x2F
                    || pMain->GetItemType() == 0x35
                    || pMain->GetItemType() == 0x31
                    || pMain->GetItemType() == 0x29) {
                    nEffective--;
                }
            }

            if (m_nSelectedButton == nEffective) {
                g_pBaldurChitin->GetObjectGame()->SetState(0);
                SetSelectedButton(100);
                UpdateButtons();
                break;
            }

            g_pBaldurChitin->GetObjectGame()->SetState(0);
            pSprite->SetModalState(0, 0);
            SetSelectedButton(nEffective);
            ReadyQuickSlotByMode(static_cast<SHORT>(nEffective - 0x3C), 1);
            if (g_pBaldurChitin->GetObjectGame()->GetState() == 0) {
                SetSelectedButton(100);
            }
            UpdateButtons();
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
        case 0x4E:
            // NOTE: unrecovered.  The binary's arm at 0x5933C3 first refuses a
            // click on the already-selected button, then demands the slot's
            // CSpell, builds a specialization mask through CRuleTables and
            // gates the whole dispatch on CGameSprite::CanCast, feeding back
            // the "beyond casting ability" string when it fails.  Only the
            // tail below is reproduced.
            if (m_nSelectedButton == nButtonType) {
                g_pBaldurChitin->GetObjectGame()->SetState(0);
                SetSelectedButton(100);
                UpdateButtons();
                break;
            }

            g_pBaldurChitin->GetObjectGame()->SetState(0);
            SetSelectedButton(nButtonType);
            pSprite->SetModalState(0, 0);
            UpdateButtons();
            ReadyQuickSlotByMode(static_cast<SHORT>(nButtonType - 0x46), 2);
            if (g_pBaldurChitin->GetObjectGame()->GetState() == 0) {
                SetSelectedButton(100);
            }
            break;
        case 0x50:
        case 0x51:
        case 0x52:
            // The one DispatchActionBarClick body that passes the runtime
            // value, at 0x5935A3, which is why these three share it.
            DispatchActionBarClick(nButtonType, pSprite);
            break;
        case 0x5A:
        case 0x5B:
        case 0x5C:
        case 0x5D:
        case 0x5E:
        case 0x5F:
        case 0x60:
        case 0x61:
        case 0x62:
            // NOTE: unrecovered.  The binary's arm at 0x5935B5 matches the
            // slot's resref against the five modal-feat abilities and, for
            // Power Attack and Expertise, opens the feat-point picker in state
            // 0x7B instead of firing; the other three flip a feat rank and post
            // the matching effect.  Only the tail below is reproduced.
            if (m_nSelectedButton == nButtonType) {
                g_pBaldurChitin->GetObjectGame()->SetState(0);
                SetSelectedButton(100);
                UpdateButtons();
                break;
            }

            g_pBaldurChitin->GetObjectGame()->SetState(0);
            SetSelectedButton(nButtonType);
            pSprite->SetModalState(0, 0);
            ReadyQuickSlotByMode(static_cast<SHORT>(nButtonType - 0x5A), 4);
            if (g_pBaldurChitin->GetObjectGame()->GetState() == 0) {
                SetSelectedButton(100);
            }
            break;
        case 0x6E:
        case 0x6F:
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
            // NOTE: unrecovered.  The binary's arm at 0x59393C reads the quick
            // song slot through the shared helper at 0x587F80 -- the same one
            // UpdateButtons' four quick-slot banks take, still unnamed -- and
            // hands the result to UseSongAction.  The modal branch below is
            // faithful; the dispatch is not.
            if (pSprite->GetModalState() == 1) {
                pSprite->SetModalState(0, 0);
                SetSelectedButton(100);
            }
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        case 0x77: {
            // Wilderness Lore, arm at 0x593181: the leader is handed a
            // disabled RANGERTRACKING effect addressed to itself.
            ITEM_EFFECT effect;
            CGameEffect::ClearItemEffect(&effect, ICEWIND_CGAMEEFFECT_RANGERTRACKING);
            effect.targetType = 1;
            CGameEffect* pEffect = CGameEffect::DecodeEffect(&effect,
                CPoint(-1, -1),
                -1,
                CPoint(-1, -1));
            pEffect->SetSource(pSprite->GetPos());
            pEffect->SetSourceId(pSprite->GetId());
            pEffect->SetEnabled(FALSE);

            CMessage* message = new CMessageAddEffect(pEffect,
                pSprite->GetId(), pSprite->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);

            SetSelectedButton(100);
            g_pBaldurChitin->GetObjectGame()->SetState(0);
            UpdateButtons();
            ClearPickerList();
            PopState(0, 0);
            break;
        }
        default:
            // The table's default at 0x5939E6 is the shared exit itself: sixty
            // of the 118 type values land here and the binary does nothing at
            // all with them.  There is deliberately no SetState back to the
            // action bar -- the submenu states never reach this switch.
            break;
        }
        break;
    }

    // The single release, at 0x593A13.  Every arm above reaches it; the only
    // paths that skip it are the three early returns before the lock existed.
    // Once the last paraphrase left, no arm needed to jump past a repaint any
    // more, so the label the earlier drafts carried here is gone.
    pGame->GetObjectArray()->ReleaseDeny(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);
}

// A right click on an action-bar slot opens the picker that rebinds it.  The
// binary's signature returns BOOL -- FALSE when buttonID is out of range and
// TRUE on every other path -- but nothing reads it: the image's only caller,
// CUIControlButtonAction::OnRButtonClick at 0x77A320, tail-calls this out of a
// void vtable slot.  Kept void, like OnLButtonPressed.
//
// 0x594720
void CInfButtonArray::OnRButtonPressed(int buttonID)
{
    if (buttonID < 0 || buttonID >= 12) {
        return;
    }

    // NO grey-out gate, and that is not an omission.  The binary tests
    // m_bGreyOut at 0x59474C and, when it is set, runs a chain of type range
    // checks meant to refuse the click -- but the chain is DEAD.  Each range
    // opens by jumping FORWARD over its own upper bound (`cmp eax,0x46` /
    // `jge` at 0x59475C, and the same shape at 0x594766, 0x594770 and
    // at 0x59477A), so reaching one of the three `jg 0x594784` exits would
    // need both `type > 0x4E` and `type < 0x46` at once.  Those three jumps
    // are the ONLY inbound edges to 0x594784 -- the `return TRUE` -- and its
    // fallthrough is unreachable too, since `type > 0x76` implies
    // `type >= 0x6E`, which the `jge 0x594792` above it has already sent to
    // the common path.  So every button proceeds, greyed or not.
    //
    // Measured rather than deduced: right-clicking portrait 4's slot 7 in the
    // Prologue autosave -- type 0x5E with m_bGreyOut set and the control still
    // active -- opens the customize menu 0x75 on the original, while the early
    // return this replaces left our build sitting in 0x72.
    //
    // (The bounds test above is ours: the binary rejects buttonID > 12, not
    // >= 12, so an id of exactly 12 reads one past m_buttonTypes[12] and
    // m_buttonArray[12].  Both land inside the object, so the original does
    // not fault, but nothing calls it with 12 and reproducing the read would
    // be writing deliberate out-of-bounds C++ for no observable gain.)

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    LONG nLeader = pGame->GetGroup()->GetGroupLeader();

    // The binary ignores the return code and keeps no early exit: the share it
    // takes here is released once, at the tail (0x594FE1), on every path below.
    CGameSprite* pSprite = NULL;
    pGame->GetObjectArray()->GetShare(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        reinterpret_cast<CGameObject**>(&pSprite),
        INFINITE);

    switch (m_nState) {
    case 0x6E: {
        // The group bar.  Right-clicking one of the five quick-formation
        // slots stashes it so the formation picker (0x6C) can rebind it.
        INT nButtonType = m_buttonTypes[buttonID];

        if (nButtonType >= 0x10 && nButtonType <= 0x14) {
            m_nCustomizeSlot = nButtonType - 0x10;
            SetState(0x6C, 1);
        }
        break;
    }
    case 0x72: {
        // The main action bar.  The index table at 0x595040 sorts every
        // button type into three groups: the customizable slots, the eight
        // quick-weapon slots, and everything else, which does nothing.
        INT nButtonType = m_buttonTypes[buttonID];

        switch (nButtonType) {
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x0E:
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
        case 0x64:
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
            // Stash the slot so state 0x75 knows which
            // m_customButtonTypes entry to write back.
            m_nCustomizeSlot = buttonID - 3;
            SetState(0x75, 1);
            break;
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
            // A quick-weapon slot.  Both refusals report and leave the
            // state alone: a cursed or otherwise unusable weapon, and a
            // magical weapon already occupying the melee slot.
            if (pSprite->CheckWeaponUsability(FALSE) != 1) {
                pSprite->FeedBack(CGameSprite::FEEDBACK_ITEMCURSED, 0, 0, 0, -1, 0, 0);
                break;
            }

            if (pSprite->m_equipment.m_items[42] != NULL) {
                pSprite->FeedBack(CGameSprite::FEEDBACK_MAGICALWEAPONINUSE, 0, 0, 0, -1, 0, 0);
                break;
            }

            m_nCustomizeSlot = m_buttonTypes[buttonID] - 0x3C;
            SetState(0x79, 1);
            break;
        }
        break;
    }
    case 0x75: {
        // The customize menu.  Six types, dispatched through the table
        // at 0x5950B8; the sixth (0x25, Use Item) is wired to the default
        // and does nothing at all.
        INT nButtonType = m_buttonTypes[buttonID];

        switch (nButtonType) {
        case 0x23:
            // Skills.
            SetState(0x74, 1);
            break;
        case 0x24: {
            // Cast Spell.  A class counts as a caster when any of its levels
            // still holds a spell, and the domain pool counts as one more.
            // Two or more sources open the class picker (0x77); exactly one
            // goes straight to that class's spellbook (0x66), but only if the
            // class has castings left.
            //
            // Note this counts by list contents, where the left-click twin
            // DispatchActionBarClick counts by m_nSharedCurrent.  The two
            // really do differ -- see the vector test at 0x5949E9.
            BYTE nClass = 0;
            UINT nCount = 0;

            for (UINT nClassIndex = 0; nClassIndex < CSPELLLIST_NUM_CLASSES; nClassIndex++) {
                if (nCount > 1) {
                    break;
                }

                UINT nLevel = 0;
                while (nLevel < pSprite->m_spells.m_spellsByClass[nClassIndex].m_nHighestLevel) {
                    if (!pSprite->m_spells.m_spellsByClass[nClassIndex].GetSpellsAtLevel(nLevel)->m_List.empty()) {
                        nClass = g_pBaldurChitin->GetObjectGame()->GetSpellcasterClass(nClassIndex);
                        nCount++;
                        break;
                    }

                    nLevel++;
                }
            }

            for (UINT nLevel = 0; nLevel < pSprite->m_domainSpells.m_nHighestLevel; nLevel++) {
                if (!pSprite->m_domainSpells.m_lists[nLevel].m_List.empty()) {
                    if (nCount == 0) {
                        // A cleric whose only spells are domain spells still
                        // goes straight to the spellbook, with the
                        // specialization steering it to the domain list.
                        nClass = CAIOBJECTTYPE_C_CLERIC;
                        m_nCurrentSelectedSpellLevel = pSprite->m_baseStats.m_specialization;
                    }

                    nCount++;
                    break;
                }

                m_nCurrentSelectedSpellLevel = 0;
            }

            if (nCount > 1) {
                SetState(0x77, 1);
                break;
            }

            if (nCount == 1) {
                if (pSprite->GetSpells(nClass)->GetTotalCurrentCount() != 0) {
                    m_nCurrentSelectedSpellClass = nClass;
                    SetState(0x66, 1);
                }
            }
            break;
        }
        case 0x26:
            // Quick Item.
            SetState(0x78, 1);
            break;
        case 0x27:
            // Special Abilities.
            if (pSprite->m_innateSpells.m_nSharedCurrent != 0) {
                SetState(0x6B, 1);
            }
            break;
        case 0x28:
            // Battle Song.
            if (!pSprite->m_songs.m_List.empty()) {
                SetState(0x71, 1);
            }
            break;
        }
        break;
    }
    case 0x77: {
        // The class picker in front of the spellbook.  Each arm names the
        // class, then walks its levels for anything castable; nothing
        // castable leaves the picker where it is.  The two spontaneous
        // casters -- Bard and Sorcerer -- read m_nSharedTotal where the
        // memorising classes read m_nSharedCurrent.
        INT nButtonType = m_buttonTypes[buttonID];
        BOOLEAN bHasSpells = FALSE;
        UINT nLevel;

        switch (nButtonType) {
        case 0x32:
            // The arm at 0x594BA5.
            m_nCurrentSelectedSpellClass = CAIOBJECTTYPE_C_BARD;
            m_nCurrentSelectedSpellLevel = 0;

            for (nLevel = 0; nLevel < pSprite->GetSpells(CAIOBJECTTYPE_C_BARD)->m_nHighestLevel; nLevel++) {
                if (bHasSpells) {
                    break;
                }

                bHasSpells = pSprite->GetSpellsAtLevel(CAIOBJECTTYPE_C_BARD, nLevel)->m_nSharedTotal != 0;
            }
            break;
        case 0x33:
            // The arm at 0x594C2E.
            m_nCurrentSelectedSpellClass = CAIOBJECTTYPE_C_CLERIC;
            m_nCurrentSelectedSpellLevel = 0;

            for (nLevel = 0; nLevel < pSprite->GetSpells(CAIOBJECTTYPE_C_CLERIC)->m_nHighestLevel; nLevel++) {
                if (bHasSpells) {
                    break;
                }

                bHasSpells = pSprite->GetSpellsAtLevel(CAIOBJECTTYPE_C_CLERIC, nLevel)->m_nSharedCurrent != 0;
            }
            break;
        case 0x34:
            // The arm at 0x594D32.
            m_nCurrentSelectedSpellClass = CAIOBJECTTYPE_C_DRUID;
            m_nCurrentSelectedSpellLevel = 0;

            for (nLevel = 0; nLevel < pSprite->GetSpells(CAIOBJECTTYPE_C_DRUID)->m_nHighestLevel; nLevel++) {
                if (bHasSpells) {
                    break;
                }

                bHasSpells = pSprite->GetSpellsAtLevel(CAIOBJECTTYPE_C_DRUID, nLevel)->m_nSharedCurrent != 0;
            }
            break;
        case 0x35:
            // The arm at 0x594DBB.
            m_nCurrentSelectedSpellClass = CAIOBJECTTYPE_C_PALADIN;
            m_nCurrentSelectedSpellLevel = 0;

            for (nLevel = 0; nLevel < pSprite->GetSpells(CAIOBJECTTYPE_C_PALADIN)->m_nHighestLevel; nLevel++) {
                if (bHasSpells) {
                    break;
                }

                bHasSpells = pSprite->GetSpellsAtLevel(CAIOBJECTTYPE_C_PALADIN, nLevel)->m_nSharedCurrent != 0;
            }
            break;
        case 0x36:
            // The arm at 0x594E44.
            m_nCurrentSelectedSpellClass = CAIOBJECTTYPE_C_RANGER;
            m_nCurrentSelectedSpellLevel = 0;

            for (nLevel = 0; nLevel < pSprite->GetSpells(CAIOBJECTTYPE_C_RANGER)->m_nHighestLevel; nLevel++) {
                if (bHasSpells) {
                    break;
                }

                bHasSpells = pSprite->GetSpellsAtLevel(CAIOBJECTTYPE_C_RANGER, nLevel)->m_nSharedCurrent != 0;
            }
            break;
        case 0x37:
            // The arm at 0x594ECD.
            m_nCurrentSelectedSpellClass = CAIOBJECTTYPE_C_SORCERER;
            m_nCurrentSelectedSpellLevel = 0;

            for (nLevel = 0; nLevel < pSprite->GetSpells(CAIOBJECTTYPE_C_SORCERER)->m_nHighestLevel; nLevel++) {
                if (bHasSpells) {
                    break;
                }

                bHasSpells = pSprite->GetSpellsAtLevel(CAIOBJECTTYPE_C_SORCERER, nLevel)->m_nSharedTotal != 0;
            }
            break;
        case 0x38:
            // The arm at 0x594F53.
            m_nCurrentSelectedSpellClass = CAIOBJECTTYPE_C_WIZARD;
            m_nCurrentSelectedSpellLevel = 0;

            for (nLevel = 0; nLevel < pSprite->GetSpells(CAIOBJECTTYPE_C_WIZARD)->m_nHighestLevel; nLevel++) {
                if (bHasSpells) {
                    break;
                }

                bHasSpells = pSprite->GetSpellsAtLevel(CAIOBJECTTYPE_C_WIZARD, nLevel)->m_nSharedCurrent != 0;
            }
            break;
        case 0x39:
            // The cleric's domain pool, at 0x594CB7.  Same shape, but the
            // level it opens on comes from the specialization.
            m_nCurrentSelectedSpellClass = CAIOBJECTTYPE_C_CLERIC;
            m_nCurrentSelectedSpellLevel = pSprite->m_baseStats.m_specialization;

            for (nLevel = 0; nLevel < pSprite->m_domainSpells.m_nHighestLevel; nLevel++) {
                if (bHasSpells) {
                    break;
                }

                bHasSpells = pSprite->m_domainSpells.m_lists[nLevel].m_nSharedCurrent != 0;
            }
            break;
        }

        if (bHasSpells) {
            SetState(0x66, 1);
        }
        break;
    }
    }

    pGame->GetObjectArray()->ReleaseShare(nLeader,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);
}

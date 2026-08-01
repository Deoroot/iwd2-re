#ifndef CINFBUTTONARRAY_H_
#define CINFBUTTONARRAY_H_

#include "CVidCell.h"

class CButtonData;
class CGameButtonList;
class CGameSprite;

// CInfButtonArray is the 12-slot action bar on the bottom-left of the
// in-game HUD.  Its state machine (m_nState) drives which 12 button types
// the array currently shows and how clicks dispatch.  Key states:
//
//   0x72  single-PC action bar (default)
//   0x6E  group action bar
//   0x6C/0x6D formation pickers
//   0x66/0x67  customize / cast spellbook
//   0x68/0x69  item picker
//   0x6A/0x6B  innate picker
//   0x70/0x71/0x7A  song pickers
//   0x73  skills submenu  (Stealth/Search/Thieving/Wilderness Lore/Animal Empathy)
//   0x74  customize-skills submenu
//   0x75  customize menu  (Attack/Cast/QuickItem/Innate/Song/Erase/Reset)
//   0x76  cast-spell class picker
//   0x77  customize-class picker
//   0x78  quick-item picker (from customize)
//   0x79  quick-weapon picker (right-click weapon slot)
//
// Sub-menu / picker entries always pop back to 0x72 on empty / unknown
// click (the Ghidra original uses a state stack via m_nStateStackDepth +
// FUN_00589ff0; we hardcode SetState(0x72, 0) since customize entry only
// comes from 0x72).

// Picker-list kinds accepted by CInfButtonArray::BuildPickerList.  SetState
// (0x589110) picks one per picker state; the value doubles as the buttonType
// handed to CGameSprite::GetItemUsages for the two item-backed kinds.
#define CINFBUTTONARRAY_PICKER_QUICK_WEAPON 1
#define CINFBUTTONARRAY_PICKER_SPELL 2
#define CINFBUTTONARRAY_PICKER_QUICK_ITEM 3
#define CINFBUTTONARRAY_PICKER_INNATE 4
#define CINFBUTTONARRAY_PICKER_INTERNAL 5
#define CINFBUTTONARRAY_PICKER_SONG 6

#pragma pack(push, 2)

class CInfButtonSettings {
public:
    CInfButtonSettings();
    ~CInfButtonSettings();

    /* 0000 */ int field_0;
    /* 0004 */ int m_bActive;
    /* 0008 */ int m_bHasOverlay;
    /* 000C */ int m_nIconNormalFrame;
    /* 0010 */ int m_nIconSelectedFrame;
    /* 0014 */ CVidCell m_iconCell;
    /* 00EE */ CVidCell m_countCell;
    /* 01C8 */ int m_nIconSequence;
    /* 01CC */ int m_bSelected;
    /* 01D0 */ int m_bActiveWeaponSet;
    /* 01D4 */ int field_1D4;
    /* 01D8 */ int m_nCount;
    /* 01DC */ BOOL m_bGreyOut;
};

class CInfButtonArray {
public:
    static const BYTE STATE_NONE;

    CInfButtonArray();

    static void GetSelectedQuickWeaponData(CButtonData& cButtonData);
    static BYTE GetSelectedModalMode();
    static void ReadyQuickSlotByMode(SHORT nButton, INT nMode);
    static void CustomizeQuickSlot(const CButtonData* pButtonData, BYTE nButton, INT nMode);
    static BOOLEAN UseItemAction(const CButtonData* pButtonData, BOOL bUseNow);
    static BOOLEAN UseSpellAction(const CButtonData* pButtonData, BOOL bUseNow);
    static BOOLEAN UseInnateAction(const CButtonData* pButtonData, BOOL bUseNow);
    BYTE GetButtonId(INT buttonType);
    BOOL UseSongAction(const CButtonData* pButtonData, BOOL bUseNow);
    BOOL ResetState();
    void UpdateState();
    BOOL SetState(INT nState, int a2);
    void UpdateButtons();
    BOOL RenderButton(CPoint pt, const CRect& rClip, BOOL bPressed, INT nButton);
    BOOL RenderButtonOverlay(CPoint pt, const CRect& rClip, BOOL bPressed, INT nButton);
    void DispatchActionBarClick(INT nButtonType, CGameSprite* pSprite);
    void SetCustomButtonTypes(const INT* pButtonList);
    void SetQuickWeaponSlot(BYTE nSlot);
    void OnLButtonPressed(int buttonID);
    void OnRButtonPressed(int buttonID);

    void SetSelectedButton(INT nSelectedButton);

    /* 0000 */ CInfButtonSettings m_buttonArray[12];
    /* 1680 */ BYTE field_1680[0x30];
    /* 16B0 */ INT m_buttonTypes[12];
    /* 16E0 */ INT field_16E0;
    /* 16E4 */ INT field_16E4;
    /* 16E8 */ CVidCell field_16E8;
    /* 17C2 */ CVidCell field_17C2;
    /* 189C */ CVidCell field_189C;
    /* 1976 */ INT m_nCustomizeSlot;
    /* 197A */ INT m_nPickerPage;
    /* 197E */ INT m_nSelectedButton;
    /* 1982 */ int m_nState;
    /* 1986 */ BYTE field_1986[0x2C];
    /* 19B2 */ int m_nStateStackDepth;
    /* 19B6 */ INT m_customButtonTypes[9];
    /* 19DA */ BYTE m_nCurrentSelectedSpellClass;
    /* 19DC */ INT m_nCurrentSelectedSpellLevel;
    /* 19E0 */ CResRef m_currentAbilityResRef;
    /* 19E8 */ BYTE m_nQuickWeaponSlot;

    void ClearPickerList();
    static CGameButtonList* BuildPickerList(INT nSlot, INT nListType, const BYTE& nClass,
        DWORD nSpecialization, BOOL a5);
};

#pragma pack(pop)

#endif /* CINFBUTTONARRAY_H_ */

#ifndef CINFBUTTONARRAY_H_
#define CINFBUTTONARRAY_H_

#include "CVidCell.h"

class CButtonData;

class CInfButtonSettings {
public:
    CInfButtonSettings();
    ~CInfButtonSettings();

    /* 0000 */ int field_0;
    /* 0004 */ int m_bActive;
    /* 0008 */ int m_bHasOverlay;
    /* 000C */ int m_nIconNormalFrame;
    /* 0010 */ int m_nIconSelectedFrame;
    /* 0014 */ CVidCell field_14;
    /* 00EE */ CVidCell field_EE;
    /* 01C8 */ int field_1C8;
    /* 01CC */ int m_bSelected;
    /* 01D0 */ int m_bActiveWeaponSet;
    /* 01D8 */ int m_nCount;
    /* 01DC */ BOOL m_bGreyOut;
};

class CInfButtonArray {
public:
    static const BYTE STATE_NONE;

    CInfButtonArray();

    static void GetSelectedQuickWeaponData(CButtonData& cButtonData);
    static BYTE GetSelectedModalMode();
    BYTE GetButtonId(INT buttonType);
    BOOL ResetState();
    void UpdateState();
    BOOL SetState(INT nState, int a2);
    void UpdateButtons();
    BOOL RenderButton(CPoint pt, const CRect& rClip, BOOL bPressed, INT nButton);
    void SetCustomButtonTypes(const INT* pButtonList);
    void SetQuickWeaponSlot(BYTE nSlot);
    void OnLButtonPressed(int buttonID);
    void OnRButtonPressed(int buttonID);

    void SetSelectedButton(INT nSelectedButton);

    /* 0000 */ CInfButtonSettings m_buttonArray[12];
    /* 16B0 */ INT m_buttonTypes[12];
    /* 16E8 */ CVidCell field_16E8;
    /* 17C2 */ CVidCell field_17C2;
    /* 189C */ CVidCell field_189C;
    /* 1976 */ CResRef field_1976;
    /* 197E */ INT m_nSelectedButton;
    /* 1982 */ int m_nState;
    /* 1986 */ BYTE field_1986[0x2C];
    /* 19B2 */ int field_19B2;
    /* 19B6 */ INT m_customButtonTypes[9];
    /* 19DA */ BYTE m_nCurrentSelectedSpellClass;
    /* 19DC */ INT m_nCurrentSelectedSpellLevel;
    /* 19E0 */ CResRef m_currentAbilityResRef;
    /* 19E8 */ BYTE m_nQuickWeaponSlot;
};

#endif /* CINFBUTTONARRAY_H_ */

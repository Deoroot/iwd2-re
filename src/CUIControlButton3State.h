#ifndef CUICONTROLBUTTON3STATE_H_
#define CUICONTROLBUTTON3STATE_H_

#include "CUIControlButton.h"

#pragma pack(push, 2)
class CUIControlButton3State : public CUIControlButton {
public:
    CUIControlButton3State(CUIPanel* panel, UI_CONTROL_BUTTON* controlInfo, BYTE nMouseButtons, unsigned char a5);
    ~CUIControlButton3State();
    void SetSelected(INT nSelected);
    void OnLButtonClick(CPoint pt) override;
    BOOL Render(BOOL bForce) override;

    /* 0666 */ SHORT m_nSelectedFrame;
    /* 0668 */ SHORT m_nNotSelectedFrame;
    /* 066A */ BOOL m_bSelected;
};
#pragma pack(pop)

static_assert(offsetof(CUIControlButton3State, m_bSelected) == 0x66A,
    "0x4D5A0B: mov dword ptr [esi+0x66a], 0");
static_assert(sizeof(CUIControlButton3State) == 0x66E,
    "its subclasses put their first member at 0x66E");

#endif /* CUICONTROLBUTTON3STATE_H_ */

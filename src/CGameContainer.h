#ifndef CGAMECONTAINER_H_
#define CGAMECONTAINER_H_

#include "CGameAIBase.h"
#include "CResRef.h"
#include "CVidCell.h"
#include "FileFormat.h"

#define STATICBUFFERSIZE_CGAMECONTAINER 4096

class CItem;

#pragma pack(push)
#pragma pack(2)
class CGameContainer : public CGameAIBase {
public:
    static const CResRef RESREF_AR6051;
    static BYTE STATICBUFFER[STATICBUFFERSIZE_CGAMECONTAINER];

    CGameContainer(CGameArea* pArea, CAreaFileContainer* pContainerObject, CAreaPoint* pPoints, WORD maxPts, CCreatureFileItem* pItems, DWORD maxItems);
    CGameContainer(CGameArea* pArea, const CRect& rBound);
    /* 0000 */ ~CGameContainer() override;
    /* 000C */ void AIUpdate() override;
    /* 0028 */ BOOLEAN CanSaveGame(STRREF& strError) override;
    /* 0030 */ void DebugDump(const CString& message, BOOLEAN bEchoToScreen) override;
    /* 0034 */ BOOL IsOver(const CPoint& pt) override;
    /* 0040 */ void OnActionButton(const CPoint& pt) override;
    /* 0048 */ void RemoveFromArea() override;
    /* 004C */ void Render(CGameArea* pArea, CVidMode* pVidMode, INT nSurface) override;
    /* 0050 */ BOOLEAN DoAIUpdate(BOOLEAN active, LONG counter) override;
    /* 0058 */ void SetCursor(LONG nToolTip) override;
    /* 0078 */ void AddEffect(CGameEffect* pEffect, BYTE list, BOOL noSave, BOOL immediateApply) override;

    /* 00B8 */ virtual void RenderClippedPoly(CGameArea* pArea, CVidMode* pVidMode, INT nSurface, COLORREF color);

    void CompressContainer();
    CItem* GetItem(SHORT nSlotNum);
    void SetItem(SHORT nSlotNum, CItem* pItem);
    void UpdateHeartContainerMapping(SHORT nSlotNum, CItem* pItem);
    BOOLEAN PlaceItemInBlankSlot(CItem* pItem, BOOLEAN bCompressContainer, SHORT nRecommendedSlotNum);
    void Marshal(CAreaFileContainer** pContainerObject);
    void MarshalMessage(BYTE** pData, DWORD* dwSize);
    void RefreshRenderPile();
    void OpenContainer(const CAIObjectType& user);
    void SetFlags(DWORD dwFlags);
    void SetTrapActivated(WORD active);
    void SetTrapDetected(WORD state);
    void SetDrawPoly(SHORT time);
    SHORT FindItemSlot(const CResRef& res);
    SHORT CountItem(const CResRef& res);
    SHORT CountItemsExcluding(CString sExclude);

    void SetScriptRes(CString scriptRes);
    SHORT GetNumItems();

    /* 0598 */ CRect m_rBounding;
    /* 05A8 */ CPoint* m_pPolygon;
    /* 05AC */ WORD m_nPolygon;
    /* 05AE */ CTypedPtrList<CPtrList, CItem*> m_lstItems;
    /* 05CA */ WORD m_containerType;
    /* 05CC */ CPoint m_ptWalkToUse;
    /* 05D4 */ CVidCell m_pileVidCell[3];
    /* 0862 */ BYTE m_nPileVidCell;
    /* 0863 */ BOOLEAN m_bDeleteMe;
    /* 0864 */ RESREF m_scriptRes;
    // The container's own name, as CGameTrigger and CGameDoor keep theirs: it
    // hides CGameAIBase::m_scriptName (0x554), which a container never writes.
    // The file ctor copies CAreaFileContainer's name here (0x47C21C strncpy),
    // the rect ctor zeroes it (0x47CA0B), Marshal (0x480B3D) and
    // MarshalMessage (0x480EAC) read it back.
    /* 086C */ SCRIPTNAME m_scriptName;
    /* 088C */ WORD m_lockDifficulty;
    /* 088E */ DWORD m_dwFlags;
    /* 0892 */ WORD m_trapDetectionDifficulty;
    /* 0894 */ WORD m_trapRemovalDifficulty;
    /* 0896 */ WORD m_trapActivated;
    /* 0898 */ WORD m_trapDetected;
    /* 089A */ CPoint m_posTrapOrigin;
    /* 08A2 */ WORD m_triggerRange;
    /* 08A4 */ SCRIPTNAME m_ownedBy;
    /* 08C4 */ CResRef m_keyType;
    /* 08CC */ DWORD m_breakDifficulty;
    /* 08D0 */ SHORT m_drawPoly;
    /* 08D2 */ int field_8D2;
    /* 08D6 */ CArray<CRect*> m_boundingGrid;
    /* 08EA */ CAreaPoint* m_pPolygonPoints;
};
#pragma pack(pop)

// Measured against the binary: the ctors and Marshal address every one of these.
static_assert(offsetof(CGameContainer, m_lstItems) == 0x5AE,
    "0x47BFCA lea edi,[esi+0x5ae] builds the item list");
static_assert(offsetof(CGameContainer, m_ptWalkToUse) == 0x5CC,
    "0x47C5B2 / Marshal 0x480B0D read [ebx+0x5cc]");
static_assert(offsetof(CGameContainer, m_nPileVidCell) == 0x862,
    "the rect ctor clears the BYTE at 0x47C9B0");
static_assert(offsetof(CGameContainer, m_scriptName) == 0x86C,
    "0x47C21C strncpy's the CAreaFileContainer name to this+0x86c");
static_assert(offsetof(CGameContainer, m_lockDifficulty) == 0x88C,
    "Marshal 0x480B4C reads WORD [ebx+0x88c]");
static_assert(offsetof(CGameContainer, m_triggerRange) == 0x8A2,
    "both ctors and Marshal address WORD +0x8a2");
static_assert(offsetof(CGameContainer, m_drawPoly) == 0x8D0,
    "both ctors clear WORD +0x8d0");
static_assert(offsetof(CGameContainer, m_boundingGrid) == 0x8D6,
    "0x47C785 lea eax,[esi+0x8d6] (SplitRectIntoGrid)");
static_assert(offsetof(CGameContainer, m_pPolygonPoints) == 0x8EA,
    "0x47C7A7 mov [esi+0x8ea],ebp");
static_assert(sizeof(CGameContainer) == 0x8EE,
    "0x43017C push 0x8ee before the ctor call at 0x4301C0");

#endif /* CGAMECONTAINER_H_ */

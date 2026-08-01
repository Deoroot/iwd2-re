#ifndef CSTORE_H_
#define CSTORE_H_

#include "mfc.h"

#include "CResRef.h"
#include "CResStore.h"
#include "FileFormat.h"

class CItem;

class CStore {
public:
    CStore();
    CStore(const CResRef& resRef);
    ~CStore();
    void SetResRef(const CResRef& resRef);
    void Marshal(const CString& sDirName);
    void RemoveEmptyItems(); // #guess (name only): 0x54CF80
    DWORD GetItemSellValue(CItem& cItem, unsigned char a2);
    DWORD GetItemBuyValue(CItem& cItem, BYTE nCHR, SHORT nReputation, unsigned char a4);
    INT GetNumItems();
    DWORD GetItemNumInStock(INT nIndex);
    BOOL GetItem(INT nIndex, CItem& cItem);
    BOOL GetItemExt(INT nIndex, CItem& cItem); // #guess (name only): 0x54CFE0
    BOOL GetItemStats(INT nIndex, CItem& cItem); // #guess (name only): 0x54D130
    CResRef GetItemId(INT nIndex);
    INT GetItemIndex(const CResRef& itemId);
    INT RemoveItemExt(CResRef resRef, DWORD dwFlags, INT nIndex, INT nAmount, BOOLEAN* pbRemoved);
    INT RemoveItemExtStore(CResRef resRef, DWORD dwFlags, INT nIndex, INT nAmount, BOOLEAN* pbRemoved); // #guess (name only): 0x54C230
    INT RemoveItemExtBag(CResRef resRef, DWORD dwFlags, INT nIndex, INT nAmount, BOOLEAN* pbRemoved); // #guess (name only): 0x54C460
    INT AddItemExt(CItem& cItem, DWORD storeFlags);
    INT AddItemExtStore(CItem& cItem); // #guess (name only): 0x54C770
    INT AddItemExtBag(CItem& cItem); // #guess (name only): 0x54C5F0
    BOOL IsValidIdentifyItem(CItem* pItem);
    BOOL IsValidSellType(CItem* pItem);
    void CompressItems();
    BOOL GetSpell(INT nIndex, CResRef& spellResRef, DWORD& dwCost);
    BOOL GetDrink(INT nIndex, STRREF& strName, DWORD& dwCost, DWORD& dwRumorChance);

    static void InvalidateStore(const CResRef& resRef);

    // Seen in `CScreenStore::OpenBag`.
    DWORD GetType() { return m_header.m_nStoreType; }

    /* 0000 */ CResRef m_resRef;
    /* 0008 */ CStoreFileHeader m_header;
    /* 00F0 */ CTypedPtrList<CPtrList, CStoreFileItem*> m_lInventory;
    /* 010C */ DWORD* m_pBuyTypes;
    /* 0110 */ DWORD m_nBuyTypes;
    /* 0114 */ CStoreFileDrinks* m_pDrinks;
    /* 0118 */ DWORD m_nDrinks;
    /* 011C */ CStoreFileSpell* m_pSpells;
    /* 0120 */ DWORD m_nSpells;
    /* 0124 */ BYTE m_pVersion[8];
    /* 0128 */ BOOL m_bLocalCopy;
};

class CStoreFile : public CResHelper<CResStore, 1014> {
public:
    void* GetData();
    DWORD GetDataSize();
};

#endif /* CSTORE_H_ */

#ifndef CITEM_H_
#define CITEM_H_

#include "CResItem.h"
#include "CSound.h"
#include "FileFormat.h"

class CGameEffect;
class CGameObject;
class CGameSprite;
class CUIControlTextDisplay;
class CWeaponIdentification;

// pack(2), like every other class in this repo that carries binary offset
// comments: without it the trailing SHORT would 4-align and sizeof would
// come out 0xF0 instead of the 0xEE the binary needs.
#pragma pack(push, 2)
class CItem : public CResHelper<CResItem, 1005> {
public:
    static const CString VALUE;

    CItem();
    CItem(const CItem& item);
    CItem(const CCreatureFileItem& item);
    CItem(CResRef id, WORD useCount1, WORD useCount2, WORD useCount3, int wear, DWORD flags);
    // VIRTUAL, and that is where CItem's first four bytes go.  The binary
    // installs a vtable at 0x84CED0 at the end of every CItem constructor
    // (0x4E78C6), and that vtable holds exactly one slot: the scalar
    // deleting destructor at 0x4E78E0.  The vptr is why the
    // CResHelper<CResItem,1005> base subobject starts at CItem+4 and
    // m_nAbilities at CItem+0x14, and it is NOT a CResHelper that is four
    // bytes bigger than it looks -- sizeof(CResHelper) is 0x10 here as it
    // is everywhere else.
    virtual ~CItem();

    CCreatureFileItem GetItemFile();
    BOOL Demand();
    BOOL Release();
    BOOL ReleaseAll();
    void SetResRef(const CResRef& cNewResRef, BOOL bSetAutoRequest);
    INT GetAbilityCount();
    WORD GetUsageCount(INT nAbility);
    WORD GetMaxUsageCount(INT nAbility);
    void SetUsageCount(INT nAbility, WORD nUseCount);
    void Equip(CGameSprite* pSprite, LONG slotNum, BOOL animationOnly);
    void Unequip(CGameSprite* pSprite, LONG slotNum, BOOL recalculateEffects, BOOL animationOnly);
    WORD GetAnimationType();
    ITEM_ABILITY* GetAbility(INT nAbility);
    CGameEffect* GetAbilityEffect(LONG abilityNum, LONG effectNum, CGameObject* pObject);
    INT GetMaxEffectSpellLevel();
    WORD GetItemType();
    DWORD GetCriticalHitMultiplier();
    DWORD GetWeight();
    CResRef GetUsedUpItemId();
    STRREF GetGenericName();
    STRREF GetIdentifiedName();
    DWORD GetFlagsFile();
    DWORD GetNotUsableBy();
    DWORD GetNotUsableBy2();
    CResRef GetGroundIcon();
    CResRef GetItemIcon();
    WORD GetMaxStackable();
    DWORD GetBaseValue();
    WORD GetLoreValue();
    INT GetEquippedACBonus();
    STRREF GetDescription();
    CResRef GetDescriptionPicture();
    void LoadWeaponIdentification(CWeaponIdentification& weaponId);
    BYTE GetMinLevelRequired();
    BYTE GetMinSTRRequired();
    BYTE GetMinINTRequired();
    BYTE GetMinDEXRequired();
    BYTE GetMinWISRequired();
    BYTE GetMinCONRequired();
    BYTE GetMinCHRRequired();
    void FormatItemDescription(CUIControlTextDisplay* pText, COLORREF rgbColor);
    void FormatItemStats(CUIControlTextDisplay* pText, COLORREF rgbColor); // #guess: 0x4EA750

    CItem& operator=(const CItem& other);
    bool operator==(const CItem& other);

    /* 0014 */ INT m_nAbilities;
    /* 0018 */ WORD m_useCount1;
    /* 001A */ WORD m_useCount2;
    /* 001C */ WORD m_useCount3;
    /* 001E */ WORD m_wear;
    /* 0020 */ DWORD m_flags;
    /* 0024 */ CSound m_useSound[2];
    /* 00EC */ SHORT m_numSounds;
};
#pragma pack(pop)

// These are measurements, not wishes: the compiler evaluates them, so they are
// how the four-byte question above was settled rather than argued.  With the
// virtual destructor in place every member of CItem now sits exactly where the
// binary puts it.
static_assert(offsetof(CItem, m_nAbilities) == 0x14,
    "the CResHelper base occupies CItem+4..+0x13, after the vptr");
static_assert(offsetof(CItem, m_useSound) == 0x24,
    "CItem::CItem builds the sound array at this+0x24 (0x4E7892)");
static_assert(sizeof(CSound) == 0x64,
    "the vector constructor iterator at 0x4E789A is given 0x64 and 2");
static_assert(sizeof(CResHelper<CResItem, 1005>) == 0x10,
    "0x10, as C2DArray and CVidCell already show -- CItem's extra four bytes "
    "are its vptr, not a bigger CResHelper");
static_assert(sizeof(CItem) == 0xEE,
    "CMessageItem puts m_item at 0x0C and the SHORT after it at 0xFA");

// sizeof(CItem) is 0xEE, as in the binary: the two bytes s43 could not remove
// were trailing padding, and pack(2) removes them.  CMessageContainerAddItem
// pins the figure (m_item at 0x0C, m_slotNum at 0xFA -- static_asserts in
// CMessage.cpp).  s43 recorded that this very pragma killed the build before
// world activation, exit 0xCFFFFFFF with no crash log.  That exit is what ANY
// launch gives when it follows a kill too closely (see the smoke-test notes);
// s54 launched the pack(2) build with a clean gap and it loads, runs the
// action-bar routes and the inventory party-transfer route to PASS.
//
// Still open, and NOT CItem's: the three controls that embed a CItem by value
// sit on bases 0xA bytes too big -- our CUIControlButton is 0x670 where the
// binary's is 0x666 (CUIControlButtonInventoryHistoryIcon::m_pItem at 0x666),
// CUIControlButton3State 0x678 against 0x66E -- so their m_item comment
// offsets (0x66A, 0x66E) are not met yet.

#endif /* CITEM_H_ */

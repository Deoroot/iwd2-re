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

// NOT closed, and deliberately not forced: sizeof(CItem) is 0xF0 here and 0xEE
// in the binary.  Every member matches; the two bytes are trailing padding,
// and CMessageItem pins the real figure (m_item at 0x0C, the SHORT after it at
// 0xFA).  Only `#pragma pack(2)` around this class brings it to 0xEE -- and
// with that pragma our build dies before the world engine activates, exit
// 0xCFFFFFFF with no crash log, while the same build without it loads and runs
// the action-bar scenarios clean.  Something in the tree depends on CItem's
// current alignment; finding it is its own arc, and a class this widely
// embedded is not worth breaking to win two bytes of padding.

#endif /* CITEM_H_ */

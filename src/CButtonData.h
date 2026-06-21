#ifndef CBUTTONDATA_H_
#define CBUTTONDATA_H_

#include "BalDataTypes.h"
#include "CResRef.h"

// Binary-mirror layout (IE packs to 2): CButtonData embeds CAbilityId @0x1A, but
// CAbilityId is 4-aligned (it has int members) so MSVC pushes it to 0x1C without
// pack(2), inflating CButtonData to 0x40 vs the binary's 0x3C. The +4 propagates
// through CGameSprite's m_quick* button arrays (m_quickWeapons[8], m_quickSpells[9],
// m_quickItems[3], m_quickInnates[9], m_quickSongs[9]).
#pragma pack(push)
#pragma pack(2)
class CAbilityId {
public:
    CAbilityId();
    CAbilityId& operator=(const CAbilityId& other);

    /* 0000 */ SHORT m_itemType;
    /* 0002 */ SHORT m_itemNum;
    /* 0004 */ SHORT m_abilityNum;
    /* 0006 */ CResRef m_res;
    /* 000E */ short m_targetType;
    /* 0010 */ int m_strDescription;
    /* 0014 */ int m_strTooltipName;
    /* 0018 */ int m_strTooltipDesc;
    /* 001C */ BYTE m_nClass;
    /* 001D */ unsigned char m_bCanUse;
    /* 001E */ short m_nTooltip;
};

class CButtonData {
public:
    CButtonData();
    CButtonData& operator=(const CButtonData& other);

    /* 0000 */ CResRef m_icon;
    /* 0008 */ STRREF m_name;
    /* 000C */ CResRef m_launcherIcon;
    /* 0014 */ STRREF m_launcherName;
    /* 0018 */ SHORT m_count;
    /* 001A */ CAbilityId m_abilityId;
    /* 003A */ BOOLEAN m_bDisabled;
    /* 003B */ BOOLEAN m_bDisplayCount;
};

#pragma pack(pop)

#endif /* CBUTTONDATA_H_ */

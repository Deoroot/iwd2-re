#ifndef CGAMESTATSSPRITE_H_
#define CGAMESTATSSPRITE_H_

#include "mfc.h"

#include "BalDataTypes.h"
#include "CResRef.h"
#include "FileFormat.h"

class CGameSprite;

// Binary-mirror classes: IE packs to 2. Without it CGameStatsRes (CObject
// vtable + CResRef + SHORT) rounds 0x0E -> 0x10, over-sizing the m_pSpellStats
// /m_pWeaponStats arrays (sizeof 0xA4 vs binary 0x94).
#pragma pack(push)
#pragma pack(2)

class CGameStatsRes : public CObject {
public:
    CGameStatsRes();
    ~CGameStatsRes() override;

    /* 0004 */ CResRef m_cResRef;
    /* 000C */ SHORT m_nTimesUsed;
};

class CGameStatsSprite {
public:
    CGameStatsSprite();
    ~CGameStatsSprite();
    void RecordKill(CGameSprite* pSprite);
    void GetStrongestKill(STRREF& strName);
    void RecordJoinParty();
    void RecordLeaveParty();
    void GetTimeWithParty(ULONG& nCurrentTimeWithParty);
    void RecordSpellUse(const CResRef& cResSpell);
    void RecordWeaponUse(const CResRef& cResWeapon);
    void GetFavouriteSpell(CResRef& cResSpell);
    void GetFavouriteWeapon(CResRef& cResWeapon);

    void SetSpellStats(BYTE index, BYTE* name, SHORT count);
    void SetWeaponStats(BYTE index, BYTE* name, SHORT count);

    /* 0000 */ STRREF m_strStrongestKillName;
    /* 0004 */ DWORD m_nStrongestKillXPValue;
    /* 0008 */ ULONG m_nPreviousTimeWithParty;
    /* 000C */ ULONG m_nJoinPartyTime;
    /* 0010 */ BOOL m_bWithParty;
    /* 0014 */ DWORD m_nChapterKillsXPValue;
    /* 0018 */ DWORD m_nChapterKillsNumber;
    /* 001C */ DWORD m_nGameKillsXPValue;
    /* 0020 */ DWORD m_nGameKillsNumber;
    /* 0024 */ CGameStatsRes m_pSpellStats[CGAMESAVECHARACTER_NUM_STATS_SPELLS];
    /* 005C */ CGameStatsRes m_pWeaponStats[CGAMESAVECHARACTER_NUM_STATS_WEAPONS];
};

#pragma pack(pop)

#endif /* CGAMESTATSSPRITE_H_ */

#ifndef ICEWINDCPROJECTILETARGETMAP_H_
#define ICEWINDCPROJECTILETARGETMAP_H_

#include <map>

#include "mfc.h"

class CGameObject;

// Per-victim bookkeeping for IcewindCProjectileTargetMap: consecutive service
// passes the object has stayed in range, and strikes it has already taken.
// Lives as the 8-byte mapped value of m_targets (map node value at +0x10).
struct IcewindCProjectileTargetEntry {
    /* 00 */ LONG m_nTicksInRange;
    /* 04 */ LONG m_nStrikes;
};

// Periodic proximity-strike scheduler embedded by value in the five wandering
// projectiles (Whirlwind and siblings -- ctors 0x57D390, 0x57F390, 0x57F640,
// 0x5806C0, 0x580C00; the Whirlwind embeds it at +0x2BA). No BG2 PDB name --
// Icewind* is the repo convention for IWD2-only classes.
//
// Service flow (slot 0, 0x55A610): when m_serviceCountdown expires, gather the
// objects within m_nRange of m_pOwner via CGameArea::GetAllInRange/
// GetAllInRangeBack (virtual slot 1), merge them into m_targets (0x55A890:
// out-of-range entries reset m_nTicksInRange, new entries are inserted, and a
// target is due when m_nTicksInRange % m_strikeInterval == 0 -- so on first
// contact -- while m_nStrikes < m_maxStrikesPerTarget), then strike the due
// list (0x55AB20: m_bSkipSource spares m_pOwner->m_sourceId; each strike bumps
// m_nStrikes until m_maxStrikesTotal latches m_bDone and the tracker goes
// inert). A strike (0x55AB80) posts a copy of every effect on the owner's
// m_effectList to the victim through the message handler, or only
// CGameEffect::FeedBackImmuneToResource(victim, owner->m_casterResRef) when
// the owner reports the victim immune (0x536FC0).
//
// vtable 0x84EEA8 (2 slots; a sibling class at vtable 0x84EEB0 shares slot 0
// and overrides only the gather). Binary sizeof 0x34 with a VC6 std::map at
// +0x08 -- our std::map layout differs, accepted by-name drift.
class IcewindCProjectileTargetMap {
public:
    IcewindCProjectileTargetMap();

    /* slot 0 */ virtual void Service();
    /* slot 1 */ virtual std::map<LONG, CPoint> GatherTargets();

    /* 0004 */ CGameObject* m_pOwner;
    /* 0008 */ std::map<LONG, IcewindCProjectileTargetEntry> m_targets;
    /* 0018 */ LONG m_servicePeriod;
    /* 001C */ LONG m_serviceCountdown;
    /* 0020 */ LONG m_strikeInterval;
    /* 0024 */ LONG m_maxStrikesPerTarget;
    /* 0028 */ LONG m_maxStrikesTotal;
    /* 002C */ LONG m_nStrikes;
    /* 0030 */ BOOLEAN m_bDone;
    /* 0031 */ BOOLEAN m_bSkipSource;
    /* 0032 */ WORD m_nRange;
};

#endif /* ICEWINDCPROJECTILETARGETMAP_H_ */

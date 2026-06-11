#include "IcewindCProjectileTargetMap.h"

// 0x55ACC0
// The binary opens with the inlined VC6 std::map construction (shared _Nil
// sentinel at 0x8E28C0, its refcount at 0x8E28BC, _Lockit guard calls); our
// m_targets default construction stands for all of it. m_pOwner and m_nRange
// are left uninitialized, faithfully -- every embedding projectile ctor fills
// them right after this runs (the Whirlwind stores itself, range 70, period 3,
// interval 33, total cap 8, skip-source).
IcewindCProjectileTargetMap::IcewindCProjectileTargetMap()
{
    m_servicePeriod = 10;
    m_serviceCountdown = 0;
    m_strikeInterval = 10;
    m_maxStrikesPerTarget = 100000;
    m_maxStrikesTotal = 100000;
    m_nStrikes = 0;
    m_bDone = FALSE;
    m_bSkipSource = FALSE;
}

// 0x55A610
// UNIMPLEMENTED: the service tick. Binary: return at once when m_bDone; while
// m_serviceCountdown > 0 just decrement it; otherwise GatherTargets() (virtual
// call), merge into m_targets and collect the due-victim list (0x55A890),
// strike the list (0x55AB20 -> 0x55AB80 per victim), then reload
// m_serviceCountdown = m_servicePeriod - 1. Inert stub: no strikes happen
// until this is recovered.
void IcewindCProjectileTargetMap::Service()
{
}

// 0x55AD90
// UNIMPLEMENTED: the gather pass. Binary: from m_pOwner's area and position,
// CGameArea::GetAllInRange + GetAllInRangeBack within m_nRange, keyed by
// object id (the 8-byte mapped value is CPoint-sized and unread by the
// service path). Inert stub: empty result.
std::map<LONG, CPoint> IcewindCProjectileTargetMap::GatherTargets()
{
    return std::map<LONG, CPoint>();
}

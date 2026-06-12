#ifndef CGAMETEMPORAL_H_
#define CGAMETEMPORAL_H_

#include "CGameAnimation.h"
#include "CGameObject.h"
#include "CSound.h"

class CGameArea;
class CVidMode;

// Transient sprite-less animation object (BG2 PDB: CGameTemporal; original
// home .\Include\ObjAnimation.h per its assert strings). Spawned at an exact
// subpixel point, it plays a CGameAnimation until the sequence ends (or for a
// fixed tick duration), drifting by a subpixel delta each tick and reacting
// to walls per its collision mode. Self-registers in the game object array
// and adds itself to the area on construction; self-deletes on removal.
// The travelling weapon missiles spawn their flame-trail puffs with it
// (CProjectileSPFLMARR::AIUpdate, animation set 0x301). vtable 0x85BF7C,
// binary sizeof 0x104.
class CGameTemporal : public CGameObject {
public:
    static const BYTE COLLISION_REBOUND;       // 0x85BD6C (= 0)
    static const BYTE COLLISION_DESTROY;       // 0x85BD6D (= 1)
    static const BYTE COLLISION_PASSTHROUGH;   // 0x85BD6E (= 2)

    CGameTemporal(USHORT animationID, BYTE* colorRangeValues, const CString& soundName,
        CGameArea* pArea, const CPoint& posExact, LONG posZ, const CPoint& posDelta,
        SHORT duration, BYTE durationFade, BYTE collision);   // 0x70FD50
    ~CGameTemporal() override;   // 0x710000

    void AIUpdate() override;   // 0x710090
    void Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface) override;   // 0x710330
    void RemoveFromArea() override;   // 0x710550

private:
    /* 006E */ BYTE m_visibleTerrainTable[16];
    /* 007E */ CGameAnimation m_animation;
    /* 0088 */ int m_animationRunning;
    /* 008C */ CPoint m_posExact;
    /* 0094 */ CPoint m_posDelta;
    /* 009C */ SHORT m_duration;
    /* 009E */ BYTE m_durationFade;
    /* 009F */ BYTE m_collision;
    /* 00A0 */ CSound m_sound;
};

#endif

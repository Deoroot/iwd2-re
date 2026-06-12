#ifndef CGAMEFIREBALL3D_H_
#define CGAMEFIREBALL3D_H_

#include "CGameObject.h"
#include "CSound.h"
#include "CVidCell.h"
#include "CVidPalette.h"

class CGameArea;
class CVidMode;

// The expanding explosion burst (BG2 PDB: CGameFireball3d; vtable 0x85BFE8,
// binary sizeof 0x210). Built by the exploding missiles' Explode overrides at
// the strike point: the constructor picks the explosion look per type
// (0 = the SPFIREPI fire pillar with the EFF_M21 boom, 1-5 = SPBOOM variants
// with their own sounds and ring-temporal animation ids), precomputes the
// four-quadrant particle table along the explosion ellipse arc
// (CVidMode::GetEllipseArcPixelList) and spawns the center CGameTemporal
// (except type 0). Render only asserts -- the visuals are the temporals the
// particle tick spawns. The particle tick itself (AIUpdate 0x711D90) is not
// yet recovered.
class CGameFireball3d : public CGameObject {
public:
    static const BYTE TYPE_FIREBALL;   // 0x85BD70 (= 0; types 1-5 live at 0x85BD71-75)

    CGameFireball3d(BYTE nType, BYTE* colorRangeValues, CGameArea* pArea, const CPoint& pos,
        SHORT nRadius, BYTE nSpeed, BYTE nCollision, USHORT nHoldDuration);   // 0x7105B0
    ~CGameFireball3d() override;   // 0x711C90 / 0x711CB0

    void AIUpdate() override;   // 0x711D90 (slot 3)
    void Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface) override;   // 0x7122E0
    void RemoveFromArea() override;   // 0x712310

private:
    // One expanding particle: a subpixel position pair plus its per-tick step
    // toward the ellipse-arc slot (1/1024 fixed point), in four mirrored
    // quadrant blocks of (m_arcLengthX + m_arcLengthY) entries.
    struct SParticle/*#guess*/ {
        /* 00 */ int x;
        /* 04 */ int y;
        /* 08 */ int stepY;
        /* 0C */ int stepX;
    };

    /* 006E */ BYTE m_visibleTerrainTable[16];
    /* 007E */ USHORT m_animationID;         // ring temporal animation set
    /* 0080 */ USHORT m_animationIDStatic;   // center temporal animation set
    /* 0082 */ BYTE m_colorRangeValues[7];
    /* 008A */ CVidCell m_spriteSplashVidCell;
    // The binary palette is 0x24 bytes and the scalars below follow at +0x188;
    // only the member order is preserved here.
    /* 0164 */ CVidPalette m_spriteSplashPalette;
    /* 0188 */ USHORT m_holdDuration;
    /* 018A */ BYTE m_duration;              // ring count: (radius - 1) / speed + 1
    /* 018B */ BYTE m_collision;
    /* 018C */ CPoint m_nEllipse;            // ellipse axes in grid squares
    /* 0194 */ BYTE* m_flagEllipse;          // the two end-marked arc pixel lists
    /* 0198 */ int m_arcLengthX/*#guess*/;
    /* 019C */ int m_arcLengthY/*#guess*/;
    /* 01A0 */ SParticle* m_pParticles/*#guess*/;
    /* 01A4 */ BYTE* m_pParticleStatus/*#guess*/;
    /* 01A8 */ CSound m_sndExplosion;
    /* 020C */ CString m_sSoundTemporal;
};

#endif

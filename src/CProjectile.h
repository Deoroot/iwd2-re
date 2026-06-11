#ifndef CPROJECTILE_H_
#define CPROJECTILE_H_

#include "CGameEffectList.h"
#include "CGameObject.h"
#include "CVidCell.h"
#include "CVidPalette.h"
#include "CVidBitmap.h"
#include "CSound.h"
#include "IcewindCVisualEffect.h"

class CGameAIBase;
class CGameArea;

class CProjectile : public CGameObject {
public:
    static CProjectile* DecodeProjectile(USHORT projectileType, CGameAIBase* pCaster, BYTE castDelay);

    CProjectile();

    /* 0068 */ BOOLEAN IsProjectile() override;
    /* 006C */ virtual void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType);
    /* 0070 */ virtual void OnArrival();
    /* 0074 */ virtual void RemoveSelf();
    /* 0078 */ virtual void DeliverEffects();
    /* 007C */ virtual void CallBack();

    void AddEffect(CGameEffect* pEffect);
    void ClearEffects();
    LONG DetermineHeight(CGameSprite* pSprite);
    SHORT GetDirection(CPoint target);
    void PlaySound(CResRef resRef, BOOL loop, BOOL fireAndForget);

    /* 006E */ WORD m_projectileType;
    /* 0070 */ WORD field_70;
    /* 0072 */ LONG m_sourceId;
    /* 0076 */ LONG m_targetId;
    /* 007A */ LONG m_callBackProjectile;
    /* 007E */ CGameEffectList m_effectList;
    /* 00EA */ CGameArea* m_pArea;
    /* 00EE */ CSound m_sound;
    /* 0152 */ CResRef m_fireSoundRef;
    /* 015A */ BOOL m_loopFireSound;
    /* 015E */ CResRef m_arrivalSoundRef;
    /* 0166 */ BOOL m_loopArrivalSound;
    /* 016A */ BOOLEAN m_bHasHeight;
    /* 016C */ SHORT m_nDeltaZ;
    /* 016E */ SHORT m_nDeltaZLast;
    /* 0170 */ LONG m_nOrigDistance;
    /* 017C */ BYTE field_17C;
    /* 017E */ CString field_17E;
    /* 0182 */ LONG m_nTargetId;
    /* 0186 */ LONG m_casterClass;
    /* 018A */ CResRef m_casterResRef;
};

class CProjectileBAM : public CProjectile {
public:
    CProjectileBAM(const CResRef& visualResRef, const CResRef& arrivalSoundRef, BYTE sequenceDelay, BYTE initialDelay, const IcewindCVisualEffect& visualEffect);

    void AIUpdate() override;
    void Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface) override;
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;

private:
    /* 0192 */ CVidCell m_vidCell;
    /* 026C */ IcewindCVisualEffect m_visualEffect;
    /* 032C */ BYTE m_sequenceDelay;
    /* 032D */ BYTE m_initialDelay;
};

class CProjectileSummonVFX : public CProjectile {
public:
    CProjectileSummonVFX(const CResRef& visualResRef, const IcewindCVisualEffect& visualEffect);

    void AIUpdate() override;
    void Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface) override;
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;

    void SetArrivalSound(const CResRef& arrivalSoundRef);
    void SetOffsetAboveTarget(BOOL offsetAboveTarget);

    static CProjectile* DecodeSpellHitProjectile(int typeIndex, CGameAIBase* pCaster, BOOL bPositive);

private:
    /* 0192 */ CVidCell m_vidCell;
    /* 026C */ IcewindCVisualEffect m_visualEffect;
    /* 032C */ BOOL m_offsetAboveTarget;
};

// Intermediate base for the travelling weapon/spell projectiles (arrows, bolts,
// darts, axes, spears, magic missiles, fireballs). Adds a heap CVidCell
// animation cell plus a range palette and bitmap on top of CProjectile. 17 leaf
// classes derive from it (~63 DecodeProjectile cases). vtable 0x84D9C4.
//
// The core flight, render, and collision virtuals (slots 3/19/27/32/33) are
// recovered; a few leaf-specific overrides (CProjectileArrow slots 34/37) remain
// stubbed.
class CProjectileTravelling : public CProjectile {
    // The Magic Missile launcher (0x530C90) writes its sub-missiles' drift
    // fields directly (m_driftX/Y, m_driftDecay, m_hasDrift, m_velocity).
    friend class CProjectileSPMAGMIS;

public:
    CProjectileTravelling(const CResRef& resRef);
    ~CProjectileTravelling() override;
    void AIUpdate() override;                  // vtable slot 3 (0x52B900)
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;  // slot 27 (0x52C050)
    void Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface) override;  // slot 19 (0x52B190)
    virtual void AimAtPoint(int x, int y);     // vtable slot 33 (0x52BD20)
    virtual DWORD GetRenderFlags();            // vtable slot 32 (0x5297D0)

protected:
    void GetCellBounds(CRect& rBounds, CPoint& ptRef);   // 0x52B6B0 -- cell draw rect + ref point
    void UpdateDirectionSequence(CVidCell* pCell);       // 0x52C8C0 -- pick anim sequence from facing

    CVidCell* m_pVidCell;       // +0x192 main animation cell
    CVidPalette m_palette;
    CVidBitmap m_bitmap;
    CVidCell* m_pShadowCell;    // +0x196 secondary "shadow" cell (drawn when m_hasShadowCell)
    SHORT m_velocity;           // +0x70 -- Frida-confirmed (arrival radius = velocity+1)
    int m_posAccumX;            // +0x9C -- subpixel position accumulator (1/1024 fixed point)
    int m_posAccumY;            // +0xA0 -- subpixel position (4/3 y-scaled, 1/1024)
    int m_stepX;                // +0xA4 -- per-tick velocity step x
    int m_stepY;                // +0xA8 -- per-tick velocity step y
    int m_driftX;               // +0xAC -- per-tick lateral drift carry X (bled off by m_driftDecay)
    int m_driftY;               // +0xB0 -- per-tick lateral drift carry Y
    int m_jitterMinX;           // +0xB4 -- random step-spread band low (x)
    int m_jitterMinY;           // +0xB8 -- random step-spread band low (y)
    int m_jitterMaxX;           // +0xBC -- random step-spread band high (x)
    int m_jitterMaxY;           // +0xC0 -- random step-spread band high (y)
    int m_hasDrift;             // +0xC4 -- lateral-offset flag (1 if the launcher gave a perpendicular spread step)
    USHORT m_driftDecay;        // +0xE0 -- drift carry decay modulus per tick (launch velocity for odd-man)
    DWORD m_renderFlags;        // +0xE6 -- base blit flags (GetRenderFlags, slot 32)
    int m_targetX;              // +0xC8 -- Frida-confirmed (target point)
    int m_targetY;              // +0xCC -- Frida-confirmed
    int m_flightDistSq;         // +0x170 -- flight distance^2 computed at Fire; 0 => arrived
    int m_tinted;               // +0x1BE -- Frida-confirmed (apply area tint colour)
    int m_useHeightOffset;      // +0x1C2 -- Frida-confirmed (add area height offset)
    int m_mirror;               // +0x1C6 -- Frida-confirmed (flip; adds blit flag 0x200)
    int m_mirrorMinX;           // +0x1CA -- mirror ref-point clamp X (GetCellBounds)
    int m_mirrorMinY;           // +0x1CE -- mirror ref-point clamp Y (GetCellBounds)
    SHORT m_leafRenderParam;    // +0x1D2 -- leaf-set render param (MMissiT/SPMAGMIS = 0x80)
    int m_hasShadowCell;        // +0x1D4 (param[0x75]) -- Frida-confirmed
    SHORT m_dirCount;           // +0x1D8 -- directional sequence count (16/8/1; >1 => pick by facing)
    SHORT m_direction;          // +0x1DA -- Frida-confirmed (facing; drives mirror thresholds)
    SHORT m_facing;             // +0x1DC -- movement facing (0..15, CGameSprite::GetDirection)
    int m_visible;              // +0x1DE -- Frida-confirmed (render gate)
    BYTE m_paletteSwap;         // +0x29C (param[0xa7]) -- Frida-confirmed
    BYTE m_distLifetime;        // +0x29D -- gate: when set, compute lifetime from sqrt(dist)/velocity
    SHORT m_lifetime;           // +0x29E -- Frida-confirmed (decrements 1/tick from 0x7FFF)
};

// Leaf 0x5324A0 -- the canonical travelling arrow (DecodeProjectile types
// 0x2/0x5/0x6; vtable 0x84E58C). The arrow-specific virtual overrides (its
// destructor 0x5325E0, slot34 0x532860, and the slot37 impact/effect delivery
// 0x5329A0) are deferred -- the leaf inherits CProjectileTravelling's flight and
// render virtuals, so it flies and draws but does not yet deliver its on-hit
// effects.
class CProjectileArrow : public CProjectileTravelling {
public:
    CProjectileArrow();   // 0x5324A0
};

// Leaf 0x57E030 -- the Magic Missile homing sub-missile (DecodeProjectile type
// 0xDA; vtable 0x8510A4). Spawned in quantity by the Magic Missile launcher
// (CProjectileMagicMissileMulti), not cast directly. A directional (mirrored)
// travelling missile at double the base velocity.
class CProjectileMMissiT : public CProjectileTravelling {
public:
    CProjectileMMissiT(SHORT nPaletteFlag);   // 0x57E030
};

// Intermediate base for the Magic Missile launcher (ctor 0x5309C0). Pre-spawns a
// list of sub-missiles in its constructor; the launcher's Fire drains the list
// into the area. The original multiply-inherits the sub-missile list at +0x2A2
// (a CPtrList, secondary vtable 0x84E0B8); we model it as a member -- it is
// transient launch staging, so the layout difference is immaterial.
class CProjectileMagicMissileMulti : public CProjectileTravelling {
public:
    CProjectileMagicMissileMulti(const CResRef& resRef, SHORT nCount, USHORT nSubType, BYTE nPaletteFlag);

protected:
    SHORT m_missileCount;                                  // +0x2A0
    CTypedPtrList<CPtrList, CProjectile*> m_subMissiles;   // +0x2A2 (binary: a 2nd base)
    USHORT m_subType;                                      // +0x2BE
};

// Leaf 0x531120 -- the Magic Missile launcher (DecodeProjectile types 0x44-0x48,
// one per caster-level band = 1..5 missiles). Spawns N MMissiT sub-missiles in
// its base, then fires them. vtable 0x84E0C4.
class CProjectileSPMAGMIS : public CProjectileMagicMissileMulti {
public:
    CProjectileSPMAGMIS(SHORT nCount, SHORT nPaletteFlag);   // 0x531120
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;  // 0x530C90

private:
    // Per sub-missile prep inlined twice inside Fire (0x530C90).
    void PrimeAndFireSubMissile(CProjectileTravelling* pMissile, CGameArea* pArea,
        LONG source, LONG target, CPoint targetPos, SHORT nType);
};

// Intermediate base of the IWD2-only spell projectiles that fly a heap BAM
// cell with an attached IcewindCVisualEffect. DecodeProjectile builds it
// directly for the travelling spell bolts (types 0x18/0xFB IcelanT/0x10C
// DisintT/0x10F OFSpheT/0x11D MSporeT/0x12A ALanceT/0x13C/0x158 MFMissT), and
// its ctor (0x578110, 21 callers) is chained by CProjectileSummonVFX
// (0x57E490), the 68-case spell-hit class (0x56EDD0), the wandering family
// (Whirlwind 0x57F640, 0x57D390, 0x57F390, 0x5806C0, 0x580C00, WoMoonX
// 0x5802B0) and 0x57AEB0/0x57C510/0x57E370/0x581060/0x581CA0. vtable
// 0x850CAC; binary sizeof 0x2AE.
//
// In the binary it derives CProjectile directly and duplicates
// CProjectileTravelling's layout verbatim (same offsets: heap cell +0x192,
// shadow cell, palette +0x19A, bitmap +0x1E2, motion fields, lifetime trio
// +0x29C); we derive CProjectileTravelling instead so those members and the
// recovered flight virtuals are shared (same modelling licence as
// CProjectileMagicMissileMulti). Differences from the travelling ctor, per
// the 0x578110 asm: height handling on by default (m_bHasHeight,
// m_useHeightOffset), the four +0xD0 colors seeded, the +0x2A0/+0x2A1 flags,
// and the visual-effect member. The already-recovered chainers
// (CProjectileSummonVFX) keep their independent minimal models for now.
class IcewindCProjectileTravellingVFX : public CProjectileTravelling {
public:
    IcewindCProjectileTravellingVFX(const CResRef& resRef);   // 0x578110

protected:
    /* 00D0 */ DWORD m_trailColors[4];
    /* 02A0 */ BYTE field_2A0;
    /* 02A1 */ BYTE field_2A1;
    /* 02A2 */ IcewindCVisualEffect m_visualEffect;
};

#endif /* CPROJECTILE_H_ */

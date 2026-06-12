#ifndef CPROJECTILE_H_
#define CPROJECTILE_H_

#include "CGameEffectList.h"
#include "CGameObject.h"
#include "CVidCell.h"
#include "CVidPalette.h"
#include "CVidBitmap.h"
#include "CSound.h"
#include "IcewindCProjectileTargetMap.h"
#include "IcewindCVisualEffect.h"

class CGameAIBase;
class CGameArea;
class CGameSprite;
class CRes;
class CResBitmap;

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
    BOOL IsTargetImmune(CGameSprite* pSprite);
    void PlaySound(CResRef resRef, BOOL loop, BOOL fireAndForget);

    /* 006E */ WORD m_projectileType;
    /* 0070 */ WORD field_70;
    /* 0072 */ LONG m_sourceId;
    /* 0076 */ LONG m_targetId;
    /* 007A */ LONG m_callBackProjectile;
    // The projectile's effect list is the plain MFC typed list (0x1C), NOT a
    // CGameEffectList: the sprite list's cursor tail (+0x1C..+0x2B) would
    // overlap m_sparkleColor and the travelling flight accumulators at
    // +0x9C.. (the ctor 0x530790 zeroes a WORD at +0x9A, and the factory's
    // sparkle cases stamp the colour there).
    /* 007E */ CTypedPtrList<CPtrList, CGameEffect*> m_effectList;
    // Sparkle-stream colour (1=black 2=blue 3=chromatic 4=gold 5=green
    // 6=purple 7=red 9=ice 10=stone 11=magenta 12=orange), row index into the
    // travel palette bitmap; written by the DecodeProjectile sparkle cases.
    /* 009A */ SHORT m_sparkleColor;
    // BG2 PDB: CProjectile::m_terrainTable. Render's passability gate feeds
    // it to CSearchBitmap::GetMobileCost; the wandering leaves pass it to
    // GetLOSCost to bounce off walls. Seeded by the CProjectileTravelling and
    // IcewindCProjectileTravellingVFX ctors (.data 0x8A8154), not here.
    /* 00D0 */ BYTE m_terrainTable[16];
    /* 00E2 */ BOOL m_bSparkleTrail;
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
    // The level of the spell that launched this projectile -- IsTargetImmune
    // (0x536FC0) indexes the target's spell-level immunity table (the one
    // Globe of Invulnerability sets) with it. Only the AI-side
    // FireSpell/FireSpellPoint paths look up and store the real memorized
    // level; the player executors (Spell/SpellPointSequence/UseItemPoint)
    // store 0.
    /* 0186 */ LONG m_nSpellLevel;
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
    // DecodeProjectile's sparkle cases (0x2F-0x37/0x41/0xB8/0xB9) write
    // m_velocity and m_visible on the freshly built leaf.
    friend class CProjectile;

public:
    CProjectileTravelling(const CResRef& resRef);
    ~CProjectileTravelling() override;
    void AIUpdate() override;                  // vtable slot 3 (0x52B900)
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;  // slot 27 (0x52C050)
    void Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface) override;  // slot 19 (0x52B190)
    virtual void AimAtPoint(int x, int y);     // vtable slot 33 (0x52BD20)
    virtual DWORD GetRenderFlags();            // vtable slot 32 (0x5297D0)

    void SetVidCell(CResRef resRef);            // 0x5295D0 -- replace the animation cell
    void SetTravelPalette(CString bitmapName);  // 0x529660 -- request the sparkle colour-table bitmap (#guess name)

protected:
    void GetCellBounds(CRect& rBounds, CPoint& ptRef, CVidCell* pCell);   // 0x52B6B0 -- cell draw rect + ref point (pCell NULL = the main cell)
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
    BOOL m_travelPaletteRequested/*#guess*/;  // +0x282 -- a CRes::Request is outstanding
    CResBitmap* m_pTravelPaletteRes/*#guess*/; // +0x286 -- the sparkle colour-table bitmap
    CResRef m_travelPaletteRef/*#guess*/;     // +0x28A -- its resref ("STTRAVL1")
    int field_298;              // +0x298 -- zeroed by SetTravelPalette
    BYTE m_paletteSwap;         // +0x29C (param[0xa7]) -- Frida-confirmed; set by SetTravelPalette, Render swaps the cell palette from the bitmap
    BYTE m_distLifetime;        // +0x29D -- gate: when set, compute lifetime from sqrt(dist)/velocity
    SHORT m_lifetime;           // +0x29E -- Frida-confirmed (decrements 1/tick from 0x7FFF)
};

// Leaf 0x5324A0 -- the canonical travelling arrow (DecodeProjectile types
// 0x2/0x5/0x6; vtable 0x84E58C). Its only own virtual is the destructor
// (0x5325E0, base dtor inlined, deferred); everything else is inherited from
// CProjectileTravelling. (The family vtables are 34 slots: 0x532860/0x5329A0,
// once mistaken for arrow slots 34/37, are slots 0/3 of the next vtable,
// CProjectileSPFLMARR's 0x84E614.)
class CProjectileArrow : public CProjectileTravelling {
public:
    CProjectileArrow();   // 0x5324A0
};

// Leaf 0x532720 -- the generic travelling weapon missile with a flame trail
// (vtable 0x84E614, binary sizeof 0x2A8). DecodeProjectile builds it for the
// flaming arrow (type 0x4, flying its default SPFLMARR cell), the thrown axe
// (0x9, AXE cell), crossbow bolt (0xE, BOLT), throwing dagger (0x1D, DAGGER),
// dart (0x22, DART) and the palette-tinted variants (0x66/0x67/0xBC, ranges
// 0x45/0x44/0x47). Each AI tick it drops three CGameTemporal trail puffs
// (animation set 0x301, colour ranges m_trailColorRanges) behind itself at
// 4/3, 1 and 2/3 of a velocity step. Its destructor (0x532860, scalar
// 0x532880, base dtor inlined) is the compiler-generated empty leaf part.
class CProjectileSPFLMARR : public CProjectileTravelling {
    // DecodeProjectile's tinted cases re-range the missile palette and stamp
    // the trail colour ranges on the freshly built leaf.
    friend class CProjectile;

public:
    CProjectileSPFLMARR();   // 0x532720

    void AIUpdate() override;   // 0x5329A0 (vtable slot 3)

private:
    /* 02A0 */ BYTE m_trailColorRanges[7];
    /* 02A7 */ BYTE m_trailTick;
};

// Intermediate 0x52CCE0 -- the exploding weapon missile (vtable 0x84DBDC,
// binary sizeof 0x4B2). On top of the travelling flight it carries a target
// CAIObjectType, strike ranges and charges, a linger state and two extra
// animation cells, and its strike pass (AreaEffect -- the name carries over
// from the BG2 PDB's CProjectileArea) fires a child projectile (type
// m_childProjectileType + 1) at every creature in strike range, cloning this
// missile's effect list onto each child.
//
// Its own flight virtuals are deferred: the destructor 0x52CE10/0x52CE30,
// AIUpdate 0x52DD60 (slot 3), Render 0x52CF80 (slot 19), Fire 0x52D9F0
// (slot 27) and OnArrival 0x52D7F0 (slot 28, arms the linger state). This
// branch's vtables are 35 slots -- Explode is the only added virtual.
class CProjectileExploding : public CProjectileTravelling {
public:
    CProjectileExploding(const CResRef& resRef);   // 0x52CCE0

    // 0x78E730 (vtable slot 34; the binary impl is COMDAT-folded with the
    // no-op CProjectile::CallBack). AreaEffect fires it once any child went
    // out; the leaves override it with their explosion VFX (0x52F1C0 --
    // deferred).
    virtual void Explode();

protected:
    int AreaEffect(BYTE bCheckRange);   // 0x52D430

    /* 02A0 */ SHORT m_strikeRange;
    /* 02A2 */ SHORT m_preCheckRange;
    /* 02A4 */ SHORT m_childProjectileType;   // the strike fires type + 1
    /* 02A6 */ SHORT m_strikesLeft;
    // 0 while flying; OnArrival's linger state otherwise (AIUpdate then runs
    // the periodic strike pass instead of the flight).
    /* 02A8 */ int m_nState;
    /* 02AC */ SHORT m_lingerPeriod;
    /* 02AE */ SHORT m_lingerCountdown;
    // Nonzero: AreaEffect first verifies somebody is inside m_preCheckRange
    // and strikes nobody otherwise.
    /* 02B0 */ int m_bPreScan/*#guess*/;
    /* 02B4 */ CAIObjectType m_targetType;
    /* 02F0 */ BYTE field_2F0;
    /* 02F1 */ BYTE field_2F1;
    /* 02F2 */ int m_bCheckNonSprites;
    /* 02F6 */ int field_2F6;
    /* 02FA */ CVidCell m_explodeCell1/*#guess*/;
    /* 03D4 */ int field_3D4;
    /* 03D8 */ CVidCell m_explodeCell2/*#guess*/;
};

// Leaf 0x52E9F0 -- the exploding flame missile (DecodeProjectile type 0x3;
// vtable 0x84DCF4, binary sizeof 0x4BA): the SPFLMARR cell at 3x the base
// velocity with the flame-trail puffs (CGameTemporal, animation set 0x300).
// Its own deferred virtuals: the destructor 0x52EB10/0x52EB30 (inline-base
// empty leaf part) and Explode 0x52F1C0 (slot 34).
class CProjectileExplodingFlame : public CProjectileExploding {
public:
    CProjectileExplodingFlame();   // 0x52E9F0

    void AIUpdate() override;   // 0x52EC80 (vtable slot 3)

private:
    /* 04B2 */ BYTE m_trailColorRanges[7];
    /* 04B9 */ BYTE m_trailTick;
};

// Leaf 0x52E230 -- the exploding thrown-weapon missile (DecodeProjectile
// types 0x8 AXE / 0xD BOLT / 0x12 MAGICSTN / 0x1C DAGGER / 0x21 DART /
// 0x39 SPEAR and the palette-tinted 0x68/0xCC; vtable 0x84DC68, binary
// sizeof 0x4BC): a mirrored 16-direction missile at 2x the base velocity
// flying the SPFIREBL cell until the case swaps in the weapon BAM. Drops the
// same CGameTemporal flame trail as CProjectileExplodingFlame (animation set
// 0x300) but aims only at the recorded target point (no live re-aim), and
// carries the explosion colour range the tinted cases restamp. Its own
// deferred virtuals: the destructor 0x52E360 and Explode 0x52E940 (slot 34).
class CProjectileExplodingWeapon : public CProjectileExploding {
    // DecodeProjectile's tinted cases re-range the palette and stamp the
    // trail and explosion colour ranges on the freshly built leaf.
    friend class CProjectile;

public:
    CProjectileExplodingWeapon();   // 0x52E230

    void AIUpdate() override;   // 0x52E4D0 (vtable slot 3)

private:
    /* 04B2 */ BYTE m_trailColorRanges[7];
    /* 04B9 */ BYTE m_trailTick;
    /* 04BA */ BYTE m_explodeColorRange/*#guess*/;
};

// Leaf 0x5300E0 -- the strike bolt (DecodeProjectile type 0x4F; vtable
// 0x84DF1C): the invisible effect carrier the exploding missiles' strike
// pass (CProjectileExploding::AreaEffect) fires at each target. The SPFIREBL
// cell never draws (m_visible = 0) and the flight lifetime derives from the
// launch distance (m_distLifetime). Its only own virtual is the destructor
// (0x5301C0, inline-base empty leaf part, deferred).
class CProjectileStrike : public CProjectileTravelling {
public:
    CProjectileStrike();   // 0x5300E0
};

// Leaf 0x57E030 -- the Magic Missile homing sub-missile (DecodeProjectile type
// 0xDA; vtable 0x8510A4). Spawned in quantity by the Magic Missile launcher
// (CProjectileMagicMissileMulti), not cast directly. A directional (mirrored)
// travelling missile at double the base velocity.
class CProjectileMMissiT : public CProjectileTravelling {
public:
    CProjectileMMissiT(SHORT nPaletteFlag);   // 0x57E030
};

// Leaf 0x52CA10 -- the single travelling spell missile ("SPMAGMIS" BAM,
// vtable 0x84DB54): the plain Magic Missile bolt (DecodeProjectile type 0x25)
// and, with the cell swapped to "TRAVEL" and a colour stamped, the coloured
// sparkle streams (types 0x2F-0x37/0xB8/0xB9) plus the invisible gaze carrier
// (0x41). A mirrored missile at double the base velocity, like CProjectileMMissiT,
// with the directional fields explicitly cleared. Its only own virtual is the
// destructor (0x52CBC0 -- empty leaf part, base dtor inlined, deferred); it
// flies, renders and delivers effects through the CProjectileTravelling/
// CProjectile base path. (0x52CE10/0x52DD60, once mistaken for sparkle slots
// 34/37, are slots 0/3 of the next vtable, CProjectileExploding's 0x84DBDC.)
class CProjectileSparkle : public CProjectileTravelling {
public:
    CProjectileSparkle(SHORT nPaletteType);   // 0x52CA10
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
    // The strike tracker's gather pass reads m_terrainTable off its owner.
    friend class IcewindCProjectileTargetMap;
    // DecodeProjectile's CHROMORB case (0x18) arms copy-from-back on the
    // freshly built projectile's visual effect.
    friend class CProjectile;

public:
    IcewindCProjectileTravellingVFX(const CResRef& resRef);   // 0x578110

    void AIUpdate() override;                 // 0x578AB0 (vtable slot 3)
    void Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface) override;   // 0x578480 (slot 19)
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;   // 0x5791D0 (slot 27)
    void AimAtPoint(int x, int y) override;   // 0x578ED0 (slot 33)

protected:
    void GetCellBounds(CRect& rBounds, CPoint& ptRef, CVidCell* pCell);   // 0x578970 -- union of the cell (z-lifted) and shadow-cell bounds
    void UpdateDirectionSequence(CVidCell* pCell);   // 0x579860 -- facing -> sequence, folding mirrored facings per the flags below

    // The BAM has no north/east-half sequences: fold those facings onto the
    // south/west ones and blit with CInfinity::MIRROR_FX.
    /* 02A0 */ BYTE m_bMirrorNorth/*#guess*/;
    /* 02A1 */ BYTE m_bMirrorEast/*#guess*/;
    /* 02A2 */ IcewindCVisualEffect m_visualEffect;
};

// Leaf 0x57F640 -- the wandering tornado (WhirlwX BAM; DecodeProjectile
// type 0x131, m_projectileType 0x130). Used by Whirlwind (SPPR613) and Wing
// Buffet (SPIN159); Wall of Moonlight (WoMoonX, ctor 0x5802B0, factory type
// 0x130) is the sibling family. vtable 0x851444 (34 slots; Render and
// GetRenderFlags are inherited -- slot 19 = 0x578480, the family
// IcewindCProjectileTravellingVFX::Render). Wanders the area
// from its wander seed (MP-replicated through CMessageFireProjectile +0x20)
// and strikes everything it touches through the embedded
// IcewindCProjectileTargetMap (period 3, re-strike interval 33, max 8 total
// strikes, spares the caster, gather radius 70).
class CProjectileWhirlwind : public IcewindCProjectileTravellingVFX {
public:
    CProjectileWhirlwind();        // 0x57F640
    ~CProjectileWhirlwind() override;   // 0x57F760 (vtable slot 0)

    void AIUpdate() override;      // 0x57F8D0 (slot 3)
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;   // 0x57FF80 (slot 27)
    void OnArrival() override;     // 0x580270 (slot 28)

    POINT* PickWanderPoint(POINT* pResult, BOOL bReverseFacing);   // 0x5800E0

    /* 02AE */ LONG m_nLifetime;
    /* 02B2 */ LONG m_nLegBudget;
    /* 02B6 */ LONG field_2B6;
    /* 02BA */ IcewindCProjectileTargetMap m_targetMap;
    /* 02F0 */ CSound m_loopSound;
    /* 0354 */ BYTE m_bFinishing;
    /* 0356 */ LONG m_wanderSeed;
};

#endif /* CPROJECTILE_H_ */

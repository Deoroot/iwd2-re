#ifndef CPROJECTILE_H_
#define CPROJECTILE_H_

#include <vector>

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
    // CProjectileCone::Pulse configures the cell/velocity/direction of the
    // per-pulse spray visuals it spawns.
    friend class CProjectileCone;

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

// Leaf 0x531790 -- the thrown dart (DecodeProjectile types 0x20/0x23/0x24;
// vtable 0x84E25C; standard dart items carry missile type 0x24). A tinted
// 16-direction missile flying the DART cell at 3x the base velocity; its only
// own virtual is the destructor (0x531860, deferred), everything else is
// inherited from CProjectileTravelling.
class CProjectileDart : public CProjectileTravelling {
public:
    CProjectileDart();   // 0x531790
};

// Intermediate 0x52CCE0 -- the exploding weapon missile (vtable 0x84DBDC,
// binary sizeof 0x4B2). On top of the travelling flight it carries a target
// CAIObjectType, strike ranges and charges, a linger state and two extra
// animation cells, and its strike pass (AreaEffect -- the name carries over
// from the BG2 PDB's CProjectileArea) fires a child projectile (type
// m_childProjectileType + 1) at every creature in strike range, cloning this
// missile's effect list onto each child.
//
// Its only own deferred virtual is Render 0x52CF80 (slot 19, draws the two
// explosion cells during the linger); the destructor 0x52CE10/0x52CE30 is the
// compiler-generated member/base chain. This branch's vtables are 35 slots --
// Explode is the only added virtual.
class CProjectileExploding : public CProjectileTravelling {
    // DecodeProjectile's Skull Trap / Glyph cases (0x60/0x64) stamp the strike
    // and pre-check ranges on the freshly built leaf.
    friend class CProjectile;

public:
    CProjectileExploding(const CResRef& resRef);   // 0x52CCE0

    void AIUpdate() override;    // 0x52DD60 (slot 3)
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;   // 0x52D9F0 (slot 27)
    void OnArrival() override;   // 0x52D7F0 (slot 28, arms the linger state)

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
    // One-shot: once one strike remains, AIUpdate fires the delayed
    // CGameFireball3d burst of m_burstType (0-5 pick the colour ranges,
    // 0xFF = none) -- the spell cases of DecodeProjectile arm these.
    /* 02F0 */ BYTE m_bBurstPending/*#guess*/;
    /* 02F1 */ BYTE m_burstType/*#guess*/;
    /* 02F2 */ int m_bCheckNonSprites;
    /* 02F6 */ int m_bExplodeCell1Active/*#guess*/;
    /* 02FA */ CVidCell m_explodeCell1/*#guess*/;
    /* 03D4 */ int m_bExplodeCell2Active/*#guess*/;
    /* 03D8 */ CVidCell m_explodeCell2/*#guess*/;
};

// Leaf 0x52E9F0 -- the exploding flame missile (DecodeProjectile type 0x3;
// vtable 0x84DCF4, binary sizeof 0x4BA): the SPFLMARR cell at 3x the base
// velocity with the flame-trail puffs (CGameTemporal, animation set 0x300).
// Its only own deferred virtual is the destructor 0x52EB10/0x52EB30
// (inline-base empty leaf part).
class CProjectileExplodingFlame : public CProjectileExploding {
public:
    CProjectileExplodingFlame();   // 0x52E9F0

    void AIUpdate() override;   // 0x52EC80 (vtable slot 3)
    void Explode() override;    // 0x52F1C0 (vtable slot 34)

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
// carries the explosion colour range the tinted cases restamp. Its only own
// deferred virtual is the destructor 0x52E360.
class CProjectileExplodingWeapon : public CProjectileExploding {
    // DecodeProjectile's tinted cases re-range the palette and stamp the
    // trail and explosion colour ranges on the freshly built leaf.
    friend class CProjectile;

public:
    CProjectileExplodingWeapon();   // 0x52E230

    void AIUpdate() override;   // 0x52E4D0 (vtable slot 3)
    void Explode() override;    // 0x52E940 (vtable slot 34)

private:
    /* 04B2 */ BYTE m_trailColorRanges[7];
    /* 04B9 */ BYTE m_trailTick;
    /* 04BA */ BYTE m_explodeColorRange/*#guess*/;
};

// Leaf 0x52F260 -- Skull Trap (DecodeProjectile type 0x60; vtable 0x84DD80,
// binary sizeof 0x4B8). An exploding spell projectile: it flies its missile
// cell and, on arrival, lingers and strikes via CProjectileExploding. Beyond
// the base it builds a second (shadow) animation cell from the explosion BAM
// and arms both embedded explosion cells (m_explodeCell1/m_explodeCell2) with
// the missile and explosion resrefs, and flies at double the launch velocity.
// Its AIUpdate (0x52F9E0, slot 3) and Explode (slot 34) overrides are deferred;
// until they are recovered it ticks with the base exploding behaviour. The
// compiler-generated destructor matches the binary's 0x52F5E0 (it adds only the
// +0x4B4 CString to the base members).
class CProjectileSkullTrap : public CProjectileExploding {
    // DecodeProjectile (types 0x60/0x64) stamps the explosion sound.
    friend class CProjectile;

public:
    CProjectileSkullTrap(const CResRef& cMissileRef, const CResRef& cExplodeRef, SHORT nType);   // 0x52F260

    void AIUpdate() override;   // 0x52F9E0 (vtable slot 3)
    void Explode() override;    // 0x52F760 (vtable slot 34)

private:
    /* 04B2 */ SHORT m_nType;
    /* 04B4 */ CString m_explodeSound;   // explosion sound resref; default-empty (ctor seeds the nil sentinel)
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
    // CProjectileCone::Pulse copies its visual effect onto the spray visuals.
    friend class CProjectileCone;

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

// Intermediate base 0x56EDD0 -- the large spell-hit / area-of-effect projectile
// (vtable 0x84F1C4; : IcewindCProjectileTravellingVFX). Unnamed in the binary
// (no BG2/RTTI carry-over); the AOE spell-effect family builds through it --
// Fireball (0x571E80, vtable 0x84F580) and ~110 other derived ctors stamp their
// own state on top. The ctor seeds the invisible "SPMAGMIS" carrier, builds the
// target filter (CAIObjectType::ANYONE), two spare animation cells, three
// visual-emission slots (each two refcounted resource names plus an
// IcewindCVisualEffect) and two sounds, then broadcasts the projectile type byte
// across the per-slot flag fields.
//
// Two embedded list-bearing sub-objects -- m_miniA (+0x4C4, ctor 0x570D50) and
// m_miniB (+0x64E, ctor 0x4C4A90; shared with the CPersistantEffect copies) --
// are not yet recovered: their list initialisation is faithfully omitted and
// only the scalar fields the ctor stamps directly are reproduced. The opaque
// bytes around them are explicit padding so the named fields keep their offsets.
class IcewindCProjectileSpellHit /*#guess*/ : public IcewindCProjectileTravellingVFX {
public:
    IcewindCProjectileSpellHit(SHORT nType);   // 0x56EDD0

protected:
    // The IE reference-counted string CProjectileCone inlines for its cone-BAM
    // name (share-count byte at block[-1], character data at block + 1); here it
    // recurs six times, each preceded by a flag byte the ctor stamps with the
    // projectile type. Cleared to empty (NULL pointer) by the ctor.
    struct ResName /*#guess*/ {
        /* 00 */ BYTE  m_flags;     // ctor: = (BYTE)nType
        /* 01 */ BYTE  _pad[3];
        /* 04 */ char* m_pName;     // refcounted block + 1 (NULL when empty)
        /* 08 */ LONG  m_nameLen;
        /* 0C */ LONG  m_nameCap;
    };
    // One visual-emission slot: two named resources and a visual-effect block.
    struct VisualSlot /*#guess*/ {
        /* 00 */ ResName              m_resA;
        /* 10 */ ResName              m_resB;
        /* 20 */ IcewindCVisualEffect m_fx;
    };

    /* 02AE */ SHORT         m_type;         // = nType (DecodeProjectile factory type)
    /* 02B0 */ WORD          field_2B0;
    /* 02B2 */ WORD          m_objectTag;    // = 0x4E
    /* 02B4 */ WORD          field_2B4;
    /* 02B6 */ LONG          field_2B6;
    /* 02BA */ WORD          field_2BA;
    /* 02BC */ WORD          field_2BC;
    /* 02BE */ CAIObjectType m_targetType;   // .Set(CAIObjectType::ANYONE)
    /* 02FA */ LONG          field_2FA;
    /* 02FE */ BYTE          field_2FE;
    /* 02FF */ BYTE          field_2FF;
    /* 0300 */ LONG          field_300;
    /* 0304 */ LONG          field_304;
    /* 0308 */ CVidCell      m_cell1;
    /* 03E2 */ LONG          field_3E2;
    /* 03E6 */ CVidCell      m_cell2;
    /* 04C0 */ LONG          field_4C0;      // = 0x2D
    // m_miniA -- list-bearing sub-object (ctor 0x570D50 unrecovered); only the
    // bytes the parent ctor stamps are named, the rest is opaque padding.
    /* 04C4 */ BYTE          m_miniA_field0; // = (BYTE)nType
    /* 04C5 */ BYTE          m_miniA_field1; // = (BYTE)nType
    /* 04C6 */ BYTE          _padA0[6];
    /* 04CC */ BYTE          m_miniA_field8; // = 0
    /* 04CD */ BYTE          _padA1[7];
    /* 04D4 */ LONG          field_4D4;      // = 10000
    /* 04D8 */ LONG          field_4D8;      // = 0
    /* 04DC */ LONG          field_4DC;      // = 10
    /* 04E0 */ BYTE          field_4E0;      // = 0
    /* 04E1 */ BYTE          _pad4E1;
    /* 04E2 */ VisualSlot    m_visual1;
    /* 050E */ VisualSlot    m_visual2;
    /* 053A */ BYTE          field_53A;      // = 0
    /* 053B */ BYTE          _pad53B;
    /* 053C */ LONG          field_53C;      // = 0x7FFFFFFF
    /* 0540 */ VisualSlot    m_visual3;
    /* 056C */ LONG          field_56C;      // = 0
    /* 0570 */ LONG          field_570;      // = 0
    /* 0574 */ BYTE          field_574;      // = 0
    /* 0575 */ BYTE          field_575;      // = 0
    /* 0576 */ BYTE          field_576;      // = 0
    /* 0577 */ BYTE          _pad577;
    /* 0578 */ LONG          field_578;      // = 0xFA
    /* 057C */ LONG          field_57C;      // = 6
    /* 0580 */ LONG          field_580;      // = 0x1E
    /* 0584 */ BYTE          field_584;      // = 0
    /* 0585 */ BYTE          _pad585;
    /* 0586 */ CSound        m_sound1;
    /* 05EA */ CSound        m_sound2;
    // m_miniB -- second list-bearing sub-object (ctor 0x4C4A90 unrecovered).
    /* 064E */ BYTE          m_miniB_field0; // = (BYTE)nType
    /* 064F */ BYTE          m_miniB_field1; // = (BYTE)nType
    /* 0650 */ BYTE          _padB0[6];
    /* 0656 */ BYTE          m_miniB_field8; // = 0
    /* 0657 */ BYTE          _padB1;
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

// Leaf 0x5806C0 -- the straight-line beam that strikes everything along its
// path (vtable 0x8513BC; ctor takes the travelling BAM). DecodeProjectile
// builds it for Lightning Bolt (type 0x28, "LightnT"; SPWI002/308/997 + Eye of
// the Mage), Smashing Wave (0x12E, "SWaveX"; SPPR522), Lance of Disruption
// (0x139, "LoDisrT"; SPWI319) and the acid breath weapon (0x17A, "HDABreT";
// SPIN222) -- the per-case fields differ but the class is one.
//
// BG2's CProjectileLightningBolt is a CProjectileBAM with a DeliverEffects
// override; IWD2 reworked it into this Icewind travelling-VFX leaf that drives
// an embedded IcewindCProjectileTargetMap (service period 2, re-strike interval
// 33, gather range set per case) the same way CProjectileWhirlwind does. Fire
// re-aims the shot to m_beamRange past the picked target along the caster->
// target line, so the bolt overshoots and rakes the whole line.
class CProjectileLightningBolt : public IcewindCProjectileTravellingVFX {
public:
    CProjectileLightningBolt(const CResRef& resRef);   // 0x5806C0
    ~CProjectileLightningBolt() override;              // 0x580770 (vtable slot 0)

    void AIUpdate() override;      // 0x5808A0 (slot 3)
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;   // 0x580B30 (slot 27)

    // Distance the shot is pushed past the picked target (ScaleToCircle radius
    // in Fire); the rake length, not a lifetime. 400 or 600 per spell.
    /* 02AE */ LONG m_beamRange;
    /* 02B2 */ IcewindCProjectileTargetMap m_targetMap;
};

// Leaf 0x579B40 -- the cone/spray area effect (vtable 0x850DDC). A single
// generic class for the whole cone family: DecodeProjectile builds it for
// Burning Hands, Cone of Cold, Color Spray, Prismatic Spray, the Shout/Great
// Shout sonic cones, Frost Fingers, the Will-o-Wisp spray and the dragon/breath
// cones (14 ability-header cases, each passing an empty carrier-cell name and
// the cone's own BAM as the second argument).
//
// BG2 split this into CProjectileConeOfCold and CProjectileColorSpray (both
// CProjectileBAM leaves, sizeof 0x2A0, with static CLOCK*/ANTICLOCK* edge
// tables and a DoLayers per-layer emit); IWD2 unified them into this single
// IcewindCProjectileTravellingVFX leaf. It carries an invisible MMissiT cell
// and paints the cone by pulsing a new virtual (slot 34, the BG2 DoLayers)
// every m_pulsePeriod ticks until m_duration expires, building the cone
// edge-point list in Fire from the caster position.
//
// The binary constructs the base subobject through a cone-specific, cell-less
// emission of the IcewindCProjectileTravellingVFX ctor (0x577E40); per the
// family modelling licence that is represented here as the ordinary base ctor
// plus the carrier-cell swap in the body (the throwaway base cell the binary
// elides).
class CProjectileCone : public IcewindCProjectileTravellingVFX {
public:
    CProjectileCone(const CResRef& cellBam, const CResRef& coneBam);   // 0x579B40
    ~CProjectileCone() override;              // 0x579DE0 (slot 0; deleting thunk 0x579DC0)

    void AIUpdate() override;                 // 0x579E50 (slot 3)
    void Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType) override;   // 0x579EF0 (slot 27)
    void OnArrival() override;                // 0x57A530 (slot 28)
    void DeliverEffects() override;           // 0x57A670 (slot 30)

    // New virtual (vtable slot 34, the BG2 CProjectileConeOfCold::DoLayers):
    // emits one cone layer/pulse. AIUpdate calls it every m_pulsePeriod ticks.
    virtual void Pulse();                     // 0x57A970

    // -- cone geometry, built in Fire from the caster position --
    /* 02AE */ CPoint m_edge0;                // four cone corner points (Fire trig)
    /* 02B6 */ CPoint m_edge1;
    /* 02BE */ CPoint m_edge2;
    /* 02C6 */ CPoint m_edge3;
    /* 02CE */ LONG m_coneLength;             // ctor 200
    /* 02D2 */ LONG m_outerRadius/*#guess*/;  // ctor 0x15E (350); Fire trig radius
    /* 02D6 */ LONG m_segCount;               // Fire: edge segment count
    /* 02DA */ LONG m_coneRadius/*#guess*/;   // ctor 0x19 (25)
    /* 02DE */ BYTE field_2DE;                // ctor = coneBam[0]
    /* 02DF */ BYTE _pad2DF[3];
    /* 02E2 */ char* m_pName;                 // refcounted cone-BAM name buffer
    /* 02E6 */ LONG m_nameLen;
    /* 02EA */ LONG m_nameCap;
    /* 02EE */ LONG field_2EE;                // ctor 1
    // The cone fan: Fire fills it with the per-segment arc points. A 16-byte
    // std::vector (this build's debug layout: proxy + first/last/end at +0x2F6/
    // FA/FE), matching the binary's std::vector<CPoint>.
    /* 02F2 */ std::vector<CPoint> m_edgePoints;
    /* 0302 */ LONG m_segmentStep/*#guess*/;  // ctor 0x14 (20); Fire divides m_outerRadius by it
    /* 0306 */ LONG field_306;                // ctor 0x23 (35)
    /* 030A */ LONG field_30A;                // ctor 0x2D (45)
    /* 030E */ LONG m_duration;               // ctor 0xF (15); AIUpdate finish threshold
    /* 0312 */ LONG m_pulsePeriod;            // ctor 5; AIUpdate pulse interval
    /* 0316 */ BYTE field_316;                // ctor 0
    /* 0317 */ BYTE field_317;                // ctor 1
    /* 0318 */ CPoint m_casterPos;            // Fire
    /* 0320 */ CPoint m_center;               // Fire
    /* 0328 */ LONG m_nHeight;                // Fire
    /* 032C */ SHORT m_nType;                 // Fire
    /* 032E */ LONG m_tickCount;              // ctor 0; AIUpdate counter
    /* 0332 */ BYTE m_bFinishing;             // ctor 0
};

// Leaf (vtable 0x850DDC's sibling 0x850D54) -- the per-pulse cone "spray"
// visual that CProjectileCone::Pulse fires along each fan point. A trivial
// IcewindCProjectileTravellingVFX with no own members (it flies the cone BAM
// and inherits the family flight/render); only its destructor is its own.
class CProjectileConePulseVisual : public IcewindCProjectileTravellingVFX {
public:
    CProjectileConePulseVisual(const CResRef& resRef);   // inlined into CProjectileCone::Pulse
    ~CProjectileConePulseVisual() override;              // 0x579B10 (vtable slot 0)
};

#endif /* CPROJECTILE_H_ */

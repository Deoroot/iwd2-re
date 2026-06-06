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
    /* 0072 */ LONG m_sourceId;
    /* 0076 */ LONG m_targetId;
    /* 007A */ LONG m_callBackProjectile;
    /* 007E */ CGameEffectList m_effectList;
    /* 00EA */ CGameArea* m_pArea;
    /* 00EE */ CSound m_sound;
    /* 0152 */ CResRef m_fireSoundRef;
    /* 015E */ CResRef m_arrivalSoundRef;
    /* 0166 */ BOOL m_loopArrivalSound;
    /* 016A */ BOOLEAN m_bHasHeight;
    /* 0182 */ LONG m_nTargetId;
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
// STEP 1 of the family recovery: layout + ctor/dtor only. The 12 virtual
// overrides (vtable 0x84D9C4 -- the flight/render/collision behaviour at
// 0x52B900/0x52B190/Fire 0x52C050 and the new slots 32-39) are NOT yet
// recovered; the class inherits CProjectile's minimal virtuals for now, so an
// instance constructs correctly but does not yet fly or draw. Only the field
// subset the constructor initializes is modelled so far (field_XXX names carry
// the binary offset); more is added as the virtuals and leaves are recovered.
class CProjectileTravelling : public CProjectile {
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
    int field_AC;               // +0xAC -- per-tick carry x (bled off by field_E0 modulus)
    int field_B0;               // +0xB0 -- per-tick carry y
    int field_B4;               // +0xB4 -- random step-spread band low (x)
    int field_B8;               // +0xB8 -- random step-spread band low (y)
    int field_BC;               // +0xBC -- random step-spread band high (x)
    int field_C0;               // +0xC0 -- random step-spread band high (y)
    USHORT field_E0;            // +0xE0 -- carry wrap modulus
    DWORD m_renderFlags;        // +0xE6 -- base blit flags (GetRenderFlags, slot 32)
    int m_targetX;              // +0xC8 -- Frida-confirmed (target point)
    int m_targetY;              // +0xCC -- Frida-confirmed
    int field_170;              // +0x170 -- nonzero during flight; 0 => arrived
    int m_tinted;               // +0x1BE -- Frida-confirmed (apply area tint colour)
    int m_useHeightOffset;      // +0x1C2 -- Frida-confirmed (add area height offset)
    int m_mirror;               // +0x1C6 -- Frida-confirmed (flip; adds blit flag 0x200)
    int field_1CA;              // +0x1CA -- mirror ref offset x
    int field_1CE;              // +0x1CE -- mirror ref offset y
    SHORT field_1D2;            // +0x1D2 -- leaf-set render param (e.g. MMissiT = 0x80)
    int m_hasShadowCell;        // +0x1D4 (param[0x75]) -- Frida-confirmed
    SHORT m_dirCount;           // +0x1D8 -- directional sequence count (16/8/1; >1 => pick by facing)
    SHORT m_direction;          // +0x1DA -- Frida-confirmed (facing; drives mirror thresholds)
    SHORT m_facing;             // +0x1DC -- movement facing (0..15, CGameSprite::GetDirection)
    int m_visible;              // +0x1DE -- Frida-confirmed (render gate)
    BYTE m_paletteSwap;         // +0x29C (param[0xa7]) -- Frida-confirmed
    BYTE field_29D;
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
};

#endif /* CPROJECTILE_H_ */

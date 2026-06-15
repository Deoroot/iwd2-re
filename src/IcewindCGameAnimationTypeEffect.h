#ifndef ICEWINDCGAMEANIMATIONTYPEEFFECT_H_
#define ICEWINDCGAMEANIMATIONTYPEEFFECT_H_

#include "CGameAnimationTypeEffect.h"

struct IcewindCSpellHitEmission;
struct IcewindCSpellHitEmissionRanged;

// IcewindCGameAnimationTypeEffect.cpp in IWD2.exe (vtable 0x84eeb8). The spell-hit
// detonation ember animation, a distinct binary class from the SPFLAMES/SPCHUNKS
// CGameAnimationTypeEffect (vtable 0x85ac48, ObjAnimation.cpp): both derive from
// CGameAnimationType with an identical 0x606-byte layout. Conflating the two -- our
// build gave the spell-hit anim CGameAnimationTypeEffect::Render (0x6A36A0) -- is
// what tinted Fireball's embers green. This Render (0x55d950) drives the
// clear-fill/copy-from-back gate, tinting and per-cell transparency from the
// descriptor's IcewindCVisualEffect (m_bCopyFromBack etc.) and blits the cells with
// m_visualEffect.m_dwFlags, so a Fireball detonation (CopyFromBack=1) blends its
// embers over the backdrop (orange) instead of clear-filling to the BAM's green key.
//
// Modelled as a CGameAnimationTypeEffect subclass to reuse that layout and vtable
// (the spell-hit vtable is CGameAnimationTypeEffect's with the dtor, CalculateFxRect,
// ChangeDirection and Render slots overridden); the binary instead derives both
// straight from CGameAnimationType (siblings). The dtor (0x55d2c0,
// compiler-generated), CalculateFxRect (0x55d690) and ChangeDirection (0x55d800) are
// also overridden in the binary but stay inherited here pending recovery -- they are
// behaviour-identical to the pre-split conflated class, so no regression.
class IcewindCGameAnimationTypeEffect : public CGameAnimationTypeEffect {
public:
    IcewindCGameAnimationTypeEffect(const IcewindCSpellHitEmission& descriptor, WORD facing);        // 0x55CD70
    IcewindCGameAnimationTypeEffect(const IcewindCSpellHitEmissionRanged& descriptor, WORD facing);  // 0x55D3A0
    /* 0090 */ void Render(CInfinity* pInfinity, CVidMode* pVidMode, INT nSurface, const CRect& rectFX, const CPoint& ptNewPos, const CPoint& ptReference, DWORD dwRenderFlags, COLORREF rgbTintColor, const CRect& rGCBounds, BOOL bDithered, BOOL bFadeOut, LONG posZ, BYTE transparency) override;  // 0x55d950
};

#endif /* ICEWINDCGAMEANIMATIONTYPEEFFECT_H_ */

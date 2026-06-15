#include "IcewindCGameAnimationTypeEffect.h"

#include "CBaldurChitin.h"
#include "CGameSprite.h"
#include "CInfGame.h"
#include "CInfinity.h"
#include "CProjectile.h"
#include "CUtil.h"

// 0x55CD70. Spell-hit detonation overload for the plainer IcewindCSpellHitEmission
// (the 0x55D3A0 twin handles the IcewindCSpellHitEmissionRanged descriptor). It is
// identical except this emission carries no m_animFlag36 (it stays 0) and its
// random-sequence opt-in (m_animMode) comes from the plain descriptor's 0x2C slot
// rather than the ranged descriptor's 0x35 slot. The shared base/member construction
// (CGameAnimationType + the CVidCell/CVidPalette/IcewindCVisualEffect subobjects)
// runs through CGameAnimationTypeEffect's protected default ctor.
IcewindCGameAnimationTypeEffect::IcewindCGameAnimationTypeEffect(const IcewindCSpellHitEmission& descriptor,
    WORD facing)
    : CGameAnimationTypeEffect()
{
    m_facing = static_cast<BYTE>(facing);
    m_animResName = NULL;
    m_animResLen = 0;
    m_animResCap = 0;
    field_5DE = 0;
    m_translucent = 0;
    field_5E1 = 0;
    m_currentBamSequence = 0;
    m_currentBamDirection = 0;
    m_extendDirectionTest = 0;
    m_animationID = 0;
    m_visualEffect = descriptor.m_visualEffect;
    m_animFlag36 = 0;
    m_animMode = descriptor.m_animMode;
    m_colorChunks = -1;
    m_bRender = TRUE;
    m_pSndDeath = "";

    if (descriptor.m_resref0Len == 0) {
        m_g1VidCell.SetResRef(CResRef("GreaseA"), FALSE, TRUE, TRUE);
    } else {
        m_g1VidCell.SetResRef(CResRef(descriptor.m_resref0), FALSE, TRUE, TRUE);

        if (m_animMode == 1) {
            BYTE cnt = static_cast<BYTE>(m_g1VidCell.GetNumberSequences(FALSE));
            if (cnt != 0) {
                m_currentBamSequence = static_cast<SHORT>(rand() % cnt);
            } else {
                m_currentBamSequence = 0;
            }
        } else {
            m_currentBamSequence = 0;
        }
        m_g1VidCell.SequenceSet(m_currentBamSequence);
        m_g1VidCell.FrameSet(0);

        // The descriptor's resref name is copied into a scratch buffer here via
        // the 0x44BC20 / 0x44BC00 allocator pair; pending recovery of those
        // helpers m_animResName is left NULL.
    }

    m_currentVidCell = &m_g1VidCell;
    m_currentVidCellShadow = NULL;
    m_currentBamDirection = facing;
    m_extendDirectionTest = CGameSprite::DIR_N;
}

// 0x55D3A0. Spell-hit detonation overload: builds one detonation-fan particle's
// animation from an IcewindCSpellHitVisual emission descriptor and the launch
// direction. Loads the detonation BAM into m_g1VidCell (the descriptor's
// m_resref0, or "GreaseA" when the descriptor carries no geometry), copies the
// descriptor's visual-effect parameters and, when the descriptor opts in
// (m_animMode), seeds a random start sequence.
IcewindCGameAnimationTypeEffect::IcewindCGameAnimationTypeEffect(const IcewindCSpellHitEmissionRanged& descriptor,
    WORD facing)
    : CGameAnimationTypeEffect()
{
    m_facing = static_cast<BYTE>(facing);
    m_animResName = NULL;
    m_animResLen = 0;
    m_animResCap = 0;
    field_5DE = 0;
    m_translucent = 0;
    field_5E1 = 0;
    m_currentBamSequence = 0;
    m_currentBamDirection = 0;
    m_extendDirectionTest = 0;
    m_animationID = 0;
    m_visualEffect = descriptor.m_visualEffect;
    m_animFlag36 = descriptor.m_animFlag36;
    m_animMode = descriptor.m_animMode;
    m_colorChunks = -1;
    m_bRender = TRUE;
    m_pSndDeath = "";

    if (descriptor.m_resref0Len == 0) {
        m_g1VidCell.SetResRef(CResRef("GreaseA"), FALSE, TRUE, TRUE);
    } else {
        m_g1VidCell.SetResRef(CResRef(descriptor.m_resref0), FALSE, TRUE, TRUE);

        if (m_animMode == 1) {
            BYTE cnt = static_cast<BYTE>(m_g1VidCell.GetNumberSequences(FALSE));
            if (cnt != 0) {
                m_currentBamSequence = static_cast<SHORT>(rand() % cnt);
            } else {
                m_currentBamSequence = 0;
            }
        } else {
            m_currentBamSequence = 0;
        }
        m_g1VidCell.SequenceSet(m_currentBamSequence);
        m_g1VidCell.FrameSet(0);

        // The descriptor's resref name is copied into a scratch buffer here via
        // the 0x44BC20 / 0x44BC00 allocator pair; pending recovery of those
        // helpers m_animResName is left NULL.
    }

    m_currentVidCell = &m_g1VidCell;
    m_currentVidCellShadow = NULL;
    m_currentBamDirection = facing;
    m_extendDirectionTest = CGameSprite::DIR_N;
}

// 0x55d950. The detonation-ember render. Unlike the CGameAnimationTypeEffect twin
// (0x6A36A0, which keys clear-fill on m_translucent/transparency and blits with the
// caller's render flags), every blend decision here comes from the descriptor's
// IcewindCVisualEffect: clear-fill only for an opaque, non-copy-from-back, shadowless
// cell; tint only when m_bTintEnabled; per-cell transparency from m_nTransValue; and
// the cells themselves are drawn with m_visualEffect.m_dwFlags. A Fireball detonation
// sets CopyFromBack=1, so its embers copy-from-back (blend over the backdrop, orange)
// instead of clear-filling to the BAM's green key colour.
void IcewindCGameAnimationTypeEffect::Render(CInfinity* pInfinity, CVidMode* pVidMode, INT nSurface, const CRect& rectFX, const CPoint& ptNewPos, const CPoint& ptReference, DWORD dwRenderFlags, COLORREF rgbTintColor, const CRect& rGCBounds, BOOL bDithered, BOOL bFadeOut, LONG posZ, BYTE transparency)
{
    // __FILE__: C:\Projects\Icewind2\src\Baldur\IcewindCGameAnimationTypeEffect.cpp
    // __LINE__: 262
    UTIL_ASSERT(pInfinity != NULL && pVidMode != NULL);

    // __FILE__: C:\Projects\Icewind2\src\Baldur\IcewindCGameAnimationTypeEffect.cpp
    // __LINE__: 263
    UTIL_ASSERT(m_currentVidCell != NULL);

    if (!m_bRender) {
        return;
    }

    CPoint ptPos(ptNewPos.x, ptNewPos.y + posZ);
    CRect rFXRect(rectFX);

    // The blend mode comes entirely from the visual-effect descriptor: an opaque,
    // non-copy-from-back, shadowless ember clear-fills the FX surface to its key
    // colour; anything else (transparent, field_2, copy-from-back, or shadowed)
    // copies the backdrop in first so the ember blends over it.
    if (m_currentVidCellShadow == NULL
        && !m_visualEffect.m_bTransparent
        && !m_visualEffect.field_2
        && !m_visualEffect.m_bCopyFromBack) {
        dwRenderFlags |= CInfinity::FXPREP_CLEARFILL;
    } else {
        dwRenderFlags |= CInfinity::FXPREP_COPYFROMBACK;
    }

    pInfinity->FXPrep(rFXRect, dwRenderFlags, nSurface, ptPos, ptReference);

    if (pInfinity->FXLock(rFXRect, dwRenderFlags)) {
        if (m_visualEffect.m_bTintEnabled == 1) {
            m_currentVidCell->SetTintColor(rgbTintColor);
        }

        if (m_currentVidCellShadow != NULL) {
            pInfinity->FXRender(m_currentVidCellShadow,
                ptReference.x,
                ptReference.y - posZ,
                m_visualEffect.m_dwFlags | 0x4,
                0);
        }

        if (m_visualEffect.m_bTransparent == 1) {
            pInfinity->FXRender(m_currentVidCell,
                ptReference.x,
                ptReference.y,
                m_visualEffect.m_dwFlags,
                m_visualEffect.m_nTransValue);
        } else {
            pInfinity->FXRender(m_currentVidCell,
                ptReference.x,
                ptReference.y,
                m_visualEffect.m_dwFlags,
                0);
        }

        // Both shadow/no-shadow paths in the binary push identical clipping-poly
        // arguments (ptPos.y unadjusted, posZ folded to 0, the raw GC bounds), so the
        // compiler's duplicated branch collapses to one call here.
        pInfinity->FXRenderClippingPolys(ptPos.x,
            ptPos.y,
            0,
            ptReference,
            rGCBounds,
            bDithered,
            m_visualEffect.m_dwFlags);

        if (bFadeOut) {
            pInfinity->FXUnlock(dwRenderFlags, &rFXRect, ptPos + ptReference);
        } else {
            pInfinity->FXUnlock(dwRenderFlags, NULL, CPoint(0, 0));
        }

        pInfinity->FXBltFrom(nSurface,
            rFXRect,
            ptPos.x,
            ptPos.y,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags | 0x1);
    }
}

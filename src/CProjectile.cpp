#include "CProjectile.h"

#include <math.h>
#include <stdlib.h>

#include "CBaldurChitin.h"
#include "CGameEffect.h"
#include "CGameArea.h"
#include "CGameObjectArray.h"
#include "CGameSprite.h"
#include "CInfinity.h"
#include "CInfGame.h"
#include "IcewindMisc.h"

static LONG GetProjectileSourceDiagonalOffset(const CRect& rEllipse)
{
    LONG x = rEllipse.right;
    LONG y = rEllipse.bottom;
    LONG denom = x * x + y * y;
    if (denom == 0) {
        return 0;
    }

    return static_cast<LONG>(sqrt(static_cast<double>((x * x * y * y) / denom)));
}

static BOOL GetProjectileSourcePosition(LONG source, CPoint& pt)
{
    CGameObject* pSource;

    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(source,
            CGameObjectArray::THREAD_ASYNCH,
            &pSource,
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return FALSE;
    }

    pt = pSource->GetPos();

    if (pSource->GetObjectType() == CGameObject::TYPE_SPRITE) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(pSource);
        const CRect& rEllipse = pSprite->GetAnimation()->GetEllipseRect();
        LONG diagonalOffset = GetProjectileSourceDiagonalOffset(rEllipse);

        switch (pSprite->GetDirection()) {
        case 0:
        case 1:
            pt.y += rEllipse.bottom / 2;
            break;
        case 2:
        case 3:
            pt.x -= 2 * diagonalOffset;
            pt.y += 2 * diagonalOffset;
            break;
        case 4:
        case 5:
            pt.x -= 2 * rEllipse.right;
            pt.y += 1;
            break;
        case 6:
        case 7:
            pt.x -= 2 * diagonalOffset;
            pt.y -= 2 * diagonalOffset;
            break;
        case 8:
        case 9:
            pt.y -= 2 * rEllipse.bottom;
            break;
        case 10:
        case 11:
            pt.x += 2 * diagonalOffset;
            pt.y -= 2 * diagonalOffset;
            break;
        case 12:
        case 13:
            pt.x += 2 * rEllipse.right;
            pt.y += 1;
            break;
        case 14:
        case 15:
            pt.x += 2 * diagonalOffset;
            pt.y += 2 * diagonalOffset;
            break;
        default:
            break;
        }
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(source,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return TRUE;
}

// 0x6A3130
BOOLEAN CProjectile::IsProjectile()
{
    return TRUE;
}

// 0x5551B0
void CProjectile::RemoveSelf()
{
    RemoveFromArea();

    BYTE rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(m_id,
        CGameObjectArray::THREAD_ASYNCH,
        NULL,
        INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        delete this;
    }
}

// 0x51EA90
void CProjectile::AddEffect(CGameEffect* pEffect)
{
    pEffect->m_projectileType = m_projectileType;
    m_effectList.AddTail(pEffect);
}

// 0x529F10
void CProjectile::ClearEffects()
{
    POSITION pos = m_effectList.GetHeadPosition();
    while (pos != NULL) {
        CGameEffect* node = m_effectList.GetNext(pos);
        delete node;
    }
    m_effectList.RemoveAll();
}

// 0x529F40
LONG CProjectile::DetermineHeight(CGameSprite* pSprite)
{
    if (!m_bHasHeight) {
        return 0;
    }

    if (pSprite->GetObjectType() != TYPE_SPRITE) {
        return 32;
    }

    return pSprite->GetAnimation()->GetCastHeight();
}

// 0x78E740
void CProjectile::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
}

// 0x529FB0
void CProjectile::OnArrival()
{
    CProjectile* pProjectile;
    BYTE rc;

    if (m_callBackProjectile != CGameObjectArray::INVALID_INDEX) {
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(m_callBackProjectile,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pProjectile),
                INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

        if (rc != CGameObjectArray::SUCCESS) {
            return;
        }

        pProjectile->CallBack();

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(m_callBackProjectile,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }

    // NOTE: Uninline.
    PlaySound(m_arrivalSoundRef, m_loopArrivalSound, TRUE);

    if (m_nTargetId != CGameObjectArray::INVALID_INDEX) {
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(m_nTargetId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pProjectile),
                INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

        if (rc == CGameObjectArray::SUCCESS) {
            pProjectile->RemoveSelf();

            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(m_nTargetId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
    }

    DeliverEffects();
    RemoveFromArea();

    rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(m_id,
        CGameObjectArray::THREAD_ASYNCH,
        NULL,
        INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        delete this;
    }
}

// 0x52A1A0
void CProjectile::DeliverEffects()
{
    // TODO: Incomplete.
}

// 0x52A480
SHORT CProjectile::GetDirection(CPoint target)
{
    CPoint ptStart;
    ptStart.x = m_pos.x;
    ptStart.y = 4 * m_pos.y / 3;

    CPoint ptTarget;
    ptTarget.x = target.x;
    ptTarget.y = 4 * target.y / 3;

    return CGameSprite::GetDirection(ptStart, ptTarget);
}

// 0x52A4E0
void CProjectile::PlaySound(CResRef resRef, BOOL loop, BOOL fireAndForget)
{
    m_sound.Stop();
    if (resRef != "") {
        m_sound.SetResRef(resRef, TRUE, TRUE);
        if (loop) {
            m_sound.SetLoopingFlag(TRUE);
        }
        if (fireAndForget) {
            m_sound.SetFireForget(TRUE);
        }
        m_sound.SetChannel(15, reinterpret_cast<DWORD>(m_pArea));
        m_sound.Play(m_pos.x, m_pos.y, 0, FALSE);
    }
}

// 0x78E730
void CProjectile::CallBack()
{
}

// 0x51EAF0
//
// Factory that maps a numeric projectile type to a concrete CProjectile
// subclass. The original dispatches ~327 hardcoded types (projectileType - 1
// indexes a 386-entry jump table) plus a generic path for types > 0x1000
// (handled by the school-overlay sub-factory at 0x560310). Only the eight
// spell-school casting-glow projectiles (CProjectileBAM) are recovered here;
// the remaining hardcoded classes are left unimplemented rather than guessed.
CProjectile* CProjectile::DecodeProjectile(USHORT projectileType, CGameAIBase* pCaster, BYTE castDelay)
{
    IcewindCVisualEffect visualEffect;

    if (projectileType > 0x1000) {
        CProjectile* pSpellHit = CProjectileSummonVFX::DecodeSpellHitProjectile(
            projectileType - 0x1001, pCaster, FALSE);
        if (pSpellHit != NULL) {
            pSpellHit->m_projectileType = projectileType - 1;
        }
        return pSpellHit;
    }

    CProjectile* pProjectile = NULL;
    switch (projectileType) {
    case 0x6F:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76: {
        // Spell-school casting-glow overlays.
        CResRef visualResRef;
        switch (projectileType) {
        case 0x6F: visualResRef = "NecroCG"; break;
        case 0x70: visualResRef = "AlterCG"; break;
        case 0x71: visualResRef = "EnchaCG"; break;
        case 0x72: visualResRef = "AbjurCG"; break;
        case 0x73: visualResRef = "IllusCG"; break;
        case 0x74: visualResRef = "ConjuCG"; break;
        case 0x75: visualResRef = "InvocCG"; break;
        case 0x76: visualResRef = "DivinCG"; break;
        }
        BYTE sequenceDelay = castDelay ? castDelay : 0x32;
        pProjectile = new CProjectileBAM(visualResRef, CResRef(""), sequenceDelay, 0, visualEffect);
        break;
    }

    case 0x121:
    case 0x122:
    case 0x123:
    case 0x124:
    case 0x125: {
        // Summon-group VFX overlays (mirror CreateSummonGroupProjectile):
        // copy-from-back tint, EFF_M13 arrival sound, offset above the target.
        CResRef visualResRef;
        switch (projectileType) {
        case 0x121: visualResRef = "MSumm1X"; break;
        case 0x122: visualResRef = "ASumm1X"; break;
        case 0x123: visualResRef = "CEElemX"; break;
        case 0x124: visualResRef = "CFElemX"; break;
        case 0x125: visualResRef = "CWElemX"; break;
        }
        visualEffect.SetCopyFromBack(TRUE);
        CProjectileSummonVFX* pSummon = new CProjectileSummonVFX(visualResRef, visualEffect);
        pSummon->SetArrivalSound(CResRef("EFF_M13"));
        pSummon->SetOffsetAboveTarget(TRUE);
        pProjectile = pSummon;
        break;
    }

    case 0x15B:
    case 0x161:
    case 0x162: {
        // Single spell-hit VFX overlays: copy-from-back tint only.
        CResRef visualResRef;
        switch (projectileType) {
        case 0x15B: visualResRef = "PortalH"; break;
        case 0x161: visualResRef = "IllusH"; break;
        case 0x162: visualResRef = "CCDamaH"; break;
        }
        visualEffect.SetCopyFromBack(TRUE);
        pProjectile = new CProjectileSummonVFX(visualResRef, visualEffect);
        break;
    }

    case 0x169:
        // Gate VFX overlay: no copy-from-back tint, no arrival sound.
        pProjectile = new CProjectileSummonVFX(CResRef("GateX"), visualEffect);
        break;

    default:
        // ~80 hardcoded projectile classes not yet recovered.
        return NULL;
    }

    // Common tail (0x528E1C): the factory stamps the 0-based projectile type on
    // every object it builds before returning it.
    if (pProjectile != NULL) {
        pProjectile->m_projectileType = projectileType - 1;
    }
    return pProjectile;
}

// -----------------------------------------------------------------------------
// INCOMPLETE: only CProjectileBAM subclass recovered; the global projectile
// dispatch switch (0x57B0C0) and remaining projectile types are not yet
// implemented. Fire/AIUpdate/Render mapped from Ghidra.

// 0x57CAC0
CProjectileBAM::CProjectileBAM(const CResRef& visualResRef, const CResRef& arrivalSoundRef, BYTE sequenceDelay, BYTE initialDelay, const IcewindCVisualEffect& visualEffect)
    : m_visualEffect(visualEffect)
{
    m_projectileType = 0;
    m_sourceId = CGameObjectArray::INVALID_INDEX;
    m_targetId = CGameObjectArray::INVALID_INDEX;
    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_pArea = NULL;
    m_arrivalSoundRef = arrivalSoundRef;
    m_loopArrivalSound = FALSE;
    m_bHasHeight = TRUE;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;

    m_vidCell.SetResRef(visualResRef, FALSE, TRUE, TRUE);
    m_vidCell.FrameSet(0);

    m_sequenceDelay = sequenceDelay;
    BYTE sequenceLength = m_vidCell.GetSequenceLength(2, FALSE);
    if (sequenceLength < m_sequenceDelay) {
        m_vidCell.SequenceSet(0);
        m_sequenceDelay -= sequenceLength;
    } else {
        m_vidCell.SequenceSet(2);
        m_sequenceDelay = 0;
    }

    m_initialDelay = initialDelay;
}

// 0x57CEE0 (virtual)
void CProjectileBAM::AIUpdate()
{
    if (m_initialDelay != 0) {
        m_initialDelay--;
        return;
    }

    if (m_sequenceDelay == 0) {
        if (m_vidCell.GetCurrentSequenceId() == 2) {
            if (m_vidCell.IsEndOfSequence(FALSE)) {
                OnArrival();
                return;
            }

            m_vidCell.FrameAdvance();
        } else {
            m_vidCell.SequenceSet(2);
            m_vidCell.FrameSet(0);
        }
    } else {
        m_sequenceDelay--;
        if (!m_vidCell.IsEndOfSequence(FALSE)) {
            m_vidCell.FrameAdvance();
        } else {
            m_vidCell.SequenceSet(1);
            m_vidCell.FrameSet(0);
        }
    }

    m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
}

// 0x57CFB0 (virtual)
void CProjectileBAM::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    CRect rFX;
    CSize frameSize;
    CRect rGCBounds;
    CPoint newPos;
    CPoint ptReference;
    CInfinity* pInfinity;

    (void)pVidMode;

    if (pArea == NULL || m_initialDelay != 0) {
        return;
    }

    pInfinity = pArea->GetInfinity();

    m_vidCell.GetCurrentCenterPoint(ptReference, FALSE);
    m_vidCell.GetCurrentFrameSize(frameSize, FALSE);

    rFX.SetRect(0, 0, frameSize.cx, frameSize.cy);

    newPos.x = m_pos.x;
    newPos.y = pArea->GetHeightOffset(m_pos, m_listType) + m_pos.y - m_posZ;

    DWORD dwPrepFlags;
    if (m_visualEffect.m_dwFlags != 0) {
        dwPrepFlags = CInfinity::FXPREP_COPYFROMBACK;
    } else {
        dwPrepFlags = CInfinity::FXPREP_CLEARFILL;
    }

    rGCBounds.left = newPos.x - ptReference.x;
    rGCBounds.top = newPos.y - ptReference.y;
    rGCBounds.right = rGCBounds.left + rFX.Width();
    rGCBounds.bottom = rGCBounds.top + rFX.Height();

    pInfinity->FXPrep(rFX,
        dwPrepFlags,
        nSurface,
        newPos,
        ptReference);

    if (pInfinity->FXLock(rFX, dwPrepFlags)) {
        pInfinity->FXRender(&m_vidCell,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags,
            m_visualEffect.m_nTransValue);

        pInfinity->FXRenderClippingPolys(newPos.x,
            newPos.y,
            0,
            ptReference,
            rGCBounds,
            FALSE,
            dwPrepFlags);

        pInfinity->FXUnlock(dwPrepFlags, NULL, CPoint(0, 0));
        pInfinity->FXBltFrom(nSurface,
            rFX,
            newPos.x,
            newPos.y,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags | 0x1);
    }
}

// 0x57D230 (virtual)
void CProjectileBAM::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
    CGameObject* pTarget;
    CPoint sourcePos;
    BYTE rc;

    (void)nType;

    m_sourceId = source;
    m_targetId = target;
    m_pArea = pArea;
    sourcePos = targetPos;

    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(target,
            CGameObjectArray::THREAD_ASYNCH,
            &pTarget,
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc == CGameObjectArray::SUCCESS) {
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(target,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }

    GetProjectileSourcePosition(source, sourcePos);
    m_sound.Play(sourcePos.x, sourcePos.y, 0, FALSE);

    rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        CGameObject::AddToArea(pArea, sourcePos, nHeight, LIST_FRONT);
        DeliverEffects();
    } else {
        delete this;
    }
}

// -----------------------------------------------------------------------------

// 0x57E490
CProjectileSummonVFX::CProjectileSummonVFX(const CResRef& visualResRef, const IcewindCVisualEffect& visualEffect)
    : m_visualEffect(visualEffect)
{
    m_projectileType = 0;
    m_sourceId = CGameObjectArray::INVALID_INDEX;
    m_targetId = CGameObjectArray::INVALID_INDEX;
    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_pArea = NULL;
    m_arrivalSoundRef = "";
    m_loopArrivalSound = FALSE;
    m_bHasHeight = FALSE;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
    m_offsetAboveTarget = FALSE;

    m_vidCell.SetResRef(visualResRef, FALSE, TRUE, TRUE);
    m_vidCell.SequenceSet(0);
    m_vidCell.FrameSet(0);
}

// 0x57E580 (virtual)
void CProjectileSummonVFX::AIUpdate()
{
    if (m_vidCell.IsEndOfSequence(FALSE)) {
        OnArrival();
        return;
    }

    m_vidCell.FrameAdvance();
    m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
}

// 0x578480 (virtual)
void CProjectileSummonVFX::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    CRect rFX;
    CSize frameSize;
    CRect rGCBounds;
    CPoint newPos;
    CPoint ptReference;
    CInfinity* pInfinity;

    (void)pVidMode;

    if (pArea == NULL) {
        return;
    }

    pInfinity = pArea->GetInfinity();

    m_vidCell.GetCurrentCenterPoint(ptReference, FALSE);
    m_vidCell.GetCurrentFrameSize(frameSize, FALSE);

    rFX.SetRect(0, 0, frameSize.cx, frameSize.cy);

    newPos.x = m_pos.x;
    newPos.y = pArea->GetHeightOffset(m_pos, m_listType) + m_pos.y - m_posZ;

    DWORD dwPrepFlags;
    if (m_visualEffect.m_dwFlags != 0) {
        dwPrepFlags = CInfinity::FXPREP_COPYFROMBACK;
    } else {
        dwPrepFlags = CInfinity::FXPREP_CLEARFILL;
    }

    rGCBounds.left = newPos.x - ptReference.x;
    rGCBounds.top = newPos.y - ptReference.y;
    rGCBounds.right = rGCBounds.left + rFX.Width();
    rGCBounds.bottom = rGCBounds.top + rFX.Height();

    pInfinity->FXPrep(rFX,
        dwPrepFlags,
        nSurface,
        newPos,
        ptReference);

    if (pInfinity->FXLock(rFX, dwPrepFlags)) {
        pInfinity->FXRender(&m_vidCell,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags,
            m_visualEffect.m_nTransValue);

        pInfinity->FXRenderClippingPolys(newPos.x,
            newPos.y,
            0,
            ptReference,
            rGCBounds,
            FALSE,
            dwPrepFlags);

        pInfinity->FXUnlock(dwPrepFlags, NULL, CPoint(0, 0));
        pInfinity->FXBltFrom(nSurface,
            rFX,
            newPos.x,
            newPos.y,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags | 0x1);
    }
}

// 0x57E710 (virtual)
void CProjectileSummonVFX::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
    BYTE rc;

    (void)nHeight;
    (void)nType;

    m_sourceId = source;
    m_targetId = target;
    m_pArea = pArea;
    m_pos.x = targetPos.x;
    m_pos.y = m_offsetAboveTarget ? targetPos.y - 100 : targetPos.y + 1;
    m_posZ = 0;

    rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        CGameObject::AddToArea(pArea, m_pos, 0, CGAMEOBJECT_LIST_FRONT);
        DeliverEffects();
    } else {
        delete this;
    }
}

void CProjectileSummonVFX::SetArrivalSound(const CResRef& arrivalSoundRef)
{
    m_arrivalSoundRef = arrivalSoundRef;
}

void CProjectileSummonVFX::SetOffsetAboveTarget(BOOL offsetAboveTarget)
{
    m_offsetAboveTarget = offsetAboveTarget;
}

// 0x560310
//
// Sub-factory for the casting/"spell hit" overlay projectiles -- the
// DecodeProjectile path for projectileType > 0x1000 (typeIndex =
// projectileType - 0x1001, bPositive selects the good/evil sound variant).
// 107 of the 112 cases build a CProjectileSummonVFX overlay (resref "<spell>H"
// or "<x>X") with per-case copy-from-back / tint-from-flags / caster color-glow
// flash / fire sound / offset-above-target.
//
// Not yet recovered (faithfully omitted, documented per case): the aura-attach
// config on a few overlays, and 4 exotic projectile classes (CallLightning x3
// at 0x5348C0, Sunray at 0x57E860). The color-glow is applied via the caster's
// AddEffect (vtbl+0x78) exactly as the original; AddEffect's own full immunity
// filter (0x733050) is a separate arc.
CProjectile* CProjectileSummonVFX::DecodeSpellHitProjectile(int typeIndex, CGameAIBase* pCaster, BOOL bPositive)
{
    CProjectileSummonVFX* p = NULL;
    switch (typeIndex) {
    case 0:  // base CProjectile (0x530790); base-projectile ctor not recovered
        return NULL;
    case 1: {
        p = new CProjectileSummonVFX(CResRef("AbjurH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0xD7, 0x8C, 0x46, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P01") : CResRef("EFF_M02");
        break;
    }
    case 2: {
        p = new CProjectileSummonVFX(CResRef("AlterH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x4B, 0xD2, 0xA0, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P07") : CResRef("EFF_M08");
        break;
    }
    case 3: {
        p = new CProjectileSummonVFX(CResRef("InvocH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x6B, 0x06, 0xC9, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P05") : CResRef("EFF_M06");
        break;
    }
    case 4: {
        p = new CProjectileSummonVFX(CResRef("NecroH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0xB4, 0xD2, 0x50, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P06") : CResRef("EFF_M07");
        break;
    }
    case 5: {
        p = new CProjectileSummonVFX(CResRef("ConjuH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x69, 0xD7, 0x46, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P02") : CResRef("EFF_M03");
        break;
    }
    case 6: {
        p = new CProjectileSummonVFX(CResRef("EnchaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x50, 0xD2, 0x50, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P04") : CResRef("EFF_M05");
        break;
    }
    case 7: {
        p = new CProjectileSummonVFX(CResRef("IllusH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x50, 0x5A, 0xD2, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = CResRef("EFF_M34");
        break;
    }
    case 8: {
        p = new CProjectileSummonVFX(CResRef("DivinH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x64, 0x46, 0xD2, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P03") : CResRef("EFF_M04");
        break;
    }
    case 9: {
        p = new CProjectileSummonVFX(CResRef("ArmorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M48");
        break;
    }
    case 10: {
        p = new CProjectileSummonVFX(CResRef("SArmorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M36");
        break;
    }
    case 11: {
        p = new CProjectileSummonVFX(CResRef("GArmorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M36");
        break;
    }
    case 12: {
        p = new CProjectileSummonVFX(CResRef("StrengH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M12");
        break;
    }
    case 13: {
        p = new CProjectileSummonVFX(CResRef("ConfusH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M16");
        break;
    }
    case 14: {
        p = new CProjectileSummonVFX(CResRef("SOFlamH"), IcewindCVisualEffect());
        break;
    }
    case 15: {
        p = new CProjectileSummonVFX(CResRef("DSpellH"), IcewindCVisualEffect());
        break;
    }
    case 16: {
        p = new CProjectileSummonVFX(CResRef("DisintH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M43");
        break;
    }
    case 17: {
        p = new CProjectileSummonVFX(CResRef("PWSileH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M44");
        break;
    }
    case 18: {
        p = new CProjectileSummonVFX(CResRef("None"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        break;
    }
    case 19: {
        p = new CProjectileSummonVFX(CResRef("FODeatH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M07");
        break;
    }
    case 20: {
        p = new CProjectileSummonVFX(CResRef("MSwordH"), IcewindCVisualEffect());
        break;
    }
    case 21: {
        p = new CProjectileSummonVFX(CResRef("MSumm1H"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 22: {
        p = new CProjectileSummonVFX(CResRef("MSumm2H"), IcewindCVisualEffect());
        break;
    }
    case 23: {
        p = new CProjectileSummonVFX(CResRef("MSumm3H"), IcewindCVisualEffect());
        break;
    }
    case 24: {
        p = new CProjectileSummonVFX(CResRef("MSumm4H"), IcewindCVisualEffect());
        break;
    }
    case 25: {
        p = new CProjectileSummonVFX(CResRef("MSumm5H"), IcewindCVisualEffect());
        break;
    }
    case 26: {
        p = new CProjectileSummonVFX(CResRef("MSumm6H"), IcewindCVisualEffect());
        break;
    }
    case 27: {
        p = new CProjectileSummonVFX(CResRef("MSumm7H"), IcewindCVisualEffect());
        break;
    }
    case 28: {
        p = new CProjectileSummonVFX(CResRef("CFElemH"), IcewindCVisualEffect());
        break;
    }
    case 29: {
        p = new CProjectileSummonVFX(CResRef("CEElemH"), IcewindCVisualEffect());
        break;
    }
    case 30: {
        p = new CProjectileSummonVFX(CResRef("CWElemH"), IcewindCVisualEffect());
        break;
    }
    case 31: {
        p = new CProjectileSummonVFX(CResRef("BlessH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P31");
        break;
    }
    case 32: {
        p = new CProjectileSummonVFX(CResRef("CurseH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P32");
        break;
    }
    case 33: {
        p = new CProjectileSummonVFX(CResRef("PrayerH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P31");
        break;
    }
    case 34: {
        p = new CProjectileSummonVFX(CResRef("RecitaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P44");
        break;
    }
    case 35: {
        p = new CProjectileSummonVFX(CResRef("CLWounH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P26");
        break;
    }
    case 36: {
        p = new CProjectileSummonVFX(CResRef("CMWounH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P26");
        break;
    }
    case 37: {
        p = new CProjectileSummonVFX(CResRef("CSWounH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P34");
        break;
    }
    case 38: {
        p = new CProjectileSummonVFX(CResRef("CCWounH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P46");
        break;
    }
    case 39: {
        p = new CProjectileSummonVFX(CResRef("HealH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P37");
        break;
    }
    case 40: {
        p = new CProjectileSummonVFX(CResRef("ASumm1H"), IcewindCVisualEffect());
        break;
    }
    case 41: {
        p = new CProjectileSummonVFX(CResRef("ASumm2H"), IcewindCVisualEffect());
        break;
    }
    case 42: {
        p = new CProjectileSummonVFX(CResRef("ASumm3H"), IcewindCVisualEffect());
        break;
    }
    case 43: {
        p = new CProjectileSummonVFX(CResRef("SPoisoH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P17");
        break;
    }
    case 44: {
        p = new CProjectileSummonVFX(CResRef("NPoisoH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P41");
        break;
    }
    case 45:  // CallLightning projectile (class not recovered)
        return NULL;
    case 46: {
        p = new CProjectileSummonVFX(CResRef("SChargH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P42");
        break;
    }
    case 47: {
        p = new CProjectileSummonVFX(CResRef("RParalH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P33");
        break;
    }
    case 48: {
        p = new CProjectileSummonVFX(CResRef("FActioH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P40");
        break;
    }
    case 49: {
        p = new CProjectileSummonVFX(CResRef("MMagicH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P13");
        break;
    }
    case 50: {
        p = new CProjectileSummonVFX(CResRef("SOOneH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P35");
        break;
    }
    case 51: {
        p = new CProjectileSummonVFX(CResRef("CStrenH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M41");
        break;
    }
    case 52:  // CallLightning projectile (class not recovered)
        return NULL;
    case 53: {
        p = new CProjectileSummonVFX(CResRef("RDeadH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P18");
        break;
    }
    case 54: {
        p = new CProjectileSummonVFX(CResRef("ResurrH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P18");
        break;
    }
    case 55: {
        p = new CProjectileSummonVFX(CResRef("CCommaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x80, 0x00, 0x80, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = CResRef("EFF_P47");
        break;
    }
    case 56: {
        p = new CProjectileSummonVFX(CResRef("RWOTFaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P36");
        break;
    }
    case 57:  // Sunray projectile (class not recovered)
        return NULL;
    case 58: {
        p = new CProjectileSummonVFX(CResRef("SStoneA"), IcewindCVisualEffect());
        p->m_fireSoundRef = CResRef("CRE_P03");
        break;
    }
    case 59: {
        p = new CProjectileSummonVFX(CResRef("DDoorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M09");
        break;
    }
    case 60: {
        p = new CProjectileSummonVFX(CResRef("DDoorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M09");
        break;
    }
    case 61: {
        p = new CProjectileSummonVFX(CResRef("CoColdH"), IcewindCVisualEffect());
        break;
    }
    case 62: {
        p = new CProjectileSummonVFX(CResRef("SSOrbH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P38");
        break;
    }
    case 63: {
        p = new CProjectileSummonVFX(CResRef("FireH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0xFF, 0x00, 0x00, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        break;
    }
    case 64: {
        p = new CProjectileSummonVFX(CResRef("ColdH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        break;
    }
    case 65: {
        p = new CProjectileSummonVFX(CResRef("ElectrH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        break;
    }
    case 66: {
        p = new CProjectileSummonVFX(CResRef("AcidH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        break;
    }
    case 67: {
        p = new CProjectileSummonVFX(CResRef("ParalH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x2D, 0x00, 0x5A, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        break;
    }
    case 68: {
        p = new CProjectileSummonVFX(CResRef("MRageH"), IcewindCVisualEffect());
        break;
    }
    case 69: {
        p = new CProjectileSummonVFX(CResRef("RWOTFaG"), IcewindCVisualEffect());
        break;
    }
    case 70: {
        p = new CProjectileSummonVFX(CResRef("BDeath"), IcewindCVisualEffect());
        break;
    }
    case 71: {
        p = new CProjectileSummonVFX(CResRef("PortalH"), IcewindCVisualEffect());
        break;
    }
    case 72:  // CallLightning projectile (class not recovered)
        return NULL;
    case 73: {
        p = new CProjectileSummonVFX(CResRef("BBarrH1"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->m_fireSoundRef = CResRef("ARE_P20");
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 74: {
        p = new CProjectileSummonVFX(CResRef("BBarrH2"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->SetOffsetAboveTarget(TRUE);
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 75: {
        p = new CProjectileSummonVFX(CResRef("CoBonH1"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->m_fireSoundRef = CResRef("ARE_P21");
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 76: {
        p = new CProjectileSummonVFX(CResRef("CoBonH2"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->SetOffsetAboveTarget(TRUE);
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 77: {
        p = new CProjectileSummonVFX(CResRef("CLDamaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 78: {
        p = new CProjectileSummonVFX(CResRef("CMDamaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 79: {
        p = new CProjectileSummonVFX(CResRef("CSDamaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 80: {
        p = new CProjectileSummonVFX(CResRef("CCDamaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 81: {
        p = new CProjectileSummonVFX(CResRef("CDiseaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P108");
        break;
    }
    case 82: {
        p = new CProjectileSummonVFX(CResRef("PoisonH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P108");
        break;
    }
    case 83: {
        p = new CProjectileSummonVFX(CResRef("SLivinH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P109");
        break;
    }
    case 84: {
        p = new CProjectileSummonVFX(CResRef("HarmH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 85: {
        p = new CProjectileSummonVFX(CResRef("DestruH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P113");
        break;
    }
    case 86: {
        p = new CProjectileSummonVFX(CResRef("ExaltaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P106");
        break;
    }
    case 87: {
        p = new CProjectileSummonVFX(CResRef("CloudbH"), IcewindCVisualEffect());
        break;
    }
    case 88: {
        p = new CProjectileSummonVFX(CResRef("MTouchH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P107");
        break;
    }
    case 89: {
        p = new CProjectileSummonVFX(CResRef("MTouchH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P107");
        break;
    }
    case 90: {
        p = new CProjectileSummonVFX(CResRef("CGraceH"), IcewindCVisualEffect());
        break;
    }
    case 91: {
        p = new CProjectileSummonVFX(CResRef("SEaterH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M104");
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 92: {
        p = new CProjectileSummonVFX(CResRef("SWaveH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P110");
        break;
    }
    case 93: {
        p = new CProjectileSummonVFX(CResRef("SuffocA"), IcewindCVisualEffect());
        break;
    }
    case 94: {
        p = new CProjectileSummonVFX(CResRef("ADHWilH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M105");
        break;
    }
    case 95: {
        p = new CProjectileSummonVFX(CResRef("MFMissX"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M103");
        break;
    }
    case 96: {
        p = new CProjectileSummonVFX(CResRef("VSpherX"), IcewindCVisualEffect());
        break;
    }
    case 97: {
        p = new CProjectileSummonVFX(CResRef("WVDeatH"), IcewindCVisualEffect());
        break;
    }
    case 98: {
        p = new CProjectileSummonVFX(CResRef("UWardX"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->SetOffsetAboveTarget(TRUE);
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 99: {
        p = new CProjectileSummonVFX(CResRef("WVHitH"), IcewindCVisualEffect());
        break;
    }
    case 100: {
        p = new CProjectileSummonVFX(CResRef("WDeath1"), IcewindCVisualEffect());
        break;
    }
    case 101: {
        p = new CProjectileSummonVFX(CResRef("WDeath2"), IcewindCVisualEffect());
        break;
    }
    case 102: {
        p = new CProjectileSummonVFX(CResRef("DDeath"), IcewindCVisualEffect());
        break;
    }
    case 103: {
        p = new CProjectileSummonVFX(CResRef("DDeath2"), IcewindCVisualEffect());
        break;
    }
    case 104: {
        p = new CProjectileSummonVFX(CResRef("MSumm1X"), IcewindCVisualEffect());
        break;
    }
    case 105: {
        p = new CProjectileSummonVFX(CResRef("ASumm1X"), IcewindCVisualEffect());
        break;
    }
    case 106: {
        p = new CProjectileSummonVFX(CResRef("CEElemX"), IcewindCVisualEffect());
        break;
    }
    case 107: {
        p = new CProjectileSummonVFX(CResRef("CFElemX"), IcewindCVisualEffect());
        break;
    }
    case 108: {
        p = new CProjectileSummonVFX(CResRef("CWElemX"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->SetOffsetAboveTarget(TRUE);
        break;
    }
    case 109: {
        p = new CProjectileSummonVFX(CResRef("GELoopX"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        break;
    }
    case 110: {
        p = new CProjectileSummonVFX(CResRef("DAttacH"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->m_fireSoundRef = CResRef("CRE_P01");
        break;
    }
    case 111: {
        p = new CProjectileSummonVFX(CResRef("WoMoonX"), IcewindCVisualEffect());
        break;
    }
    default:
        return NULL;
    }
    return p;
}

// 0x52AD60
//
// CProjectileTravelling -- shared base ctor for the travelling weapon/spell
// projectiles. Builds the heap CVidCell animation cell from the visual resref,
// a range palette and a bitmap, then seeds the travelling state. The leaf
// classes (CProjectileArrow etc.) prepare the resref and call this, then set
// their own vtable and per-projectile configuration.
//
// STEP 1: the CProjectile base sub-object is constructed by its (implicit)
// member ctor; this body initializes the travelling additions. The original
// also zeroes the motion-integrator work fields in the +0x9C..0xC0 "drift gap"
// (see the projectile-factory layout note) -- the subpixel position, per-tick
// step, carry and random-spread band that AimAtPoint reads each tick -- plus
// the +0xE6 render flags; these are modelled by-name on CProjectileTravelling
// and seeded here. The remaining unread gap defaults (+0xC4, +0xD0..0xDC) are
// omitted. field_E0 (the carry modulus) is left unwritten by the original; it
// is only consulted when a carry is non-zero, which the zeroed carries prevent,
// so it is zero-seeded here for definedness.
CProjectileTravelling::CProjectileTravelling(const CResRef& resRef)
    : m_palette(CVidPalette::TYPE_RANGE)
{
    field_1CA = 0;
    field_1CE = 0;
    field_29D = 0;
    m_lifetime = 0x7FFF;

    m_pVidCell = new CVidCell(resRef, FALSE);

    m_pShadowCell = NULL;
    m_hasShadowCell = 0;
    m_mirror = 0;
    m_visible = 1;
    m_direction = 0;
    m_paletteSwap = 0;
    m_tinted = 0;
    m_useHeightOffset = 0;
    m_velocity = 0x14;
    m_renderFlags = 0x20000;

    // Motion-integrator state (AimAtPoint): subpixel position, step, carry and
    // random-spread band, all zero so the path starts straight from the launch.
    m_posAccumX = 0;
    m_posAccumY = 0;
    m_stepX = 0;
    m_stepY = 0;
    field_AC = 0;
    field_B0 = 0;
    field_B4 = 0;
    field_B8 = 0;
    field_BC = 0;
    field_C0 = 0;
    field_E0 = 0;
    m_targetX = 0;
    m_targetY = 0;
    field_170 = 0;

    m_sourceId = 0;
    m_targetId = 0;
    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x52B010 (vtable slot 0, partial)
//
// Frees the heap CVidCell; the embedded palette and bitmap destruct
// automatically. The full original destructor (which also releases the cell's
// requested resources) is recovered with the rest of the virtual interface.
CProjectileTravelling::~CProjectileTravelling()
{
    delete m_pVidCell;
}

// 0x52B900 (vtable slot 3 -- AIUpdate)
//
// Per-tick flight. Field semantics confirmed by a Frida trace of a Magic
// Missile cast on the original IWD2.exe: target (+0xC8/+0xCC) stays constant,
// the position (CGameObject m_pos) closes on it at ~velocity (+0x70) per tick,
// the lifetime (+0x29E) counts down from 0x7FFF, and the trail field (+0xE2)
// stays 0.
//
// PARTIAL: the pause-gate (skip while the engine single-steps another object),
// the moving-target homing branch (shares the live target and interpolates
// height from its animation), and the trailing sub-projectile (unrecovered
// factory 0x51AE40; branch disabled for Magic Missile) are documented stubs.
// The advance/arrival/expiry/lifetime/sound core is recovered and trace-verified
// (the per-tick aim step itself, 0x52BD20, is recovered -- see AimAtPoint).
void CProjectileTravelling::AIUpdate()
{
    m_pVidCell->FrameAdvance();

    // Arrival: target and position share the same 16-unit x and 12-unit y cell.
    int posX = m_pos.x;
    if (((m_targetX + ((m_targetX >> 31) & 0xF)) >> 4) == ((posX + ((posX >> 31) & 0xF)) >> 4)
        && m_targetY / 12 == m_pos.y / 12) {
        OnArrival();
        return;
    }

    // Arrival within the per-tick travel radius (velocity + 1); y weighted 16/9.
    int dy = m_targetY - m_pos.y;
    int dist = (dy * dy * 16) / 9 + (m_targetX - posX) * (m_targetX - posX);
    int radius = m_velocity + 1;
    if (dist <= radius * radius) {
        OnArrival();
        return;
    }
    if (field_170 == 0) {
        OnArrival();
        return;
    }

    // Lifetime countdown.
    SHORT life = m_lifetime;
    m_lifetime = life - 1;
    if (life == 0) {
        RemoveSelf();
        return;
    }

    if (m_targetId == CGameObjectArray::INVALID_INDEX) {
        AimAtPoint(m_targetX, m_targetY);
    } else {
        // Homing: the original shares the live target, aims at its position and
        // interpolates the projectile height from its animation. STUB: aim at
        // the recorded target point; live-target tracking and height interp are
        // recovered with the rest of the homing path.
        AimAtPoint(m_targetX, m_targetY);
    }

    // Trailing sub-projectile (+0xE2 != 0) via the unrecovered factory 0x51AE40
    // -- omitted (trace shows it disabled for Magic Missile).

    m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
}

// 0x52C050 (vtable slot 27 -- Fire; the launch)
//
// Ghidra recovers no function at the vtable target; transcribed from capstone
// disassembly (0x85E bytes) with the field semantics Frida-confirmed. Records
// the source/target/area, resolves the launch origin and the target point, and
// computes the flight distance and lifetime.
//
// PARTIAL: the trajectory setup (distance^2 + lifetime + target point), the
// area insertion (AddToArea), the subpixel-position seed and the initial facing
// are recovered -- the seed values are Frida-confirmed exact against the
// original (m_posAccumX = launch.x << 10, m_posAccumY = (launch.y << 12) / 3),
// and AddToArea's arguments are Frida-confirmed (pNewArea == the pArea arg, pos
// == the launch origin, listType 0). The remaining launch actions are documented
// stubs:
//   * the launch height (posZ): the original gates on m_bHasHeight and, for a
//     creature source, reads the source's current animation height (source
//     object's CGameAnimation at +0x50F0, GetHeight virtual; 0x20 for a
//     non-creature source) -- the animation-height stub shared with AIUpdate;
//     passed as 0 (ground) here.
//   * the attached-object create (CGameObjectArray::Add 0x59A0F0 +
//     CMessageHandler::AddMessage 0x4F7500 + CMessage 0x554D20 -> m_nTargetId);
//     not exercised by Magic Missile (m_nTargetId stays INVALID).
//   * the fire-sound setup (CSound at +0xEE, fields +0xF2/+0xF6,
//     CDimm::GetResObject / CSound::SetChannel).
// AddToArea registers the projectile with the area's object lists, so once a leaf
// is wired into the factory the engine will drive its AIUpdate/Render.
void CProjectileTravelling::Fire(CGameArea* pArea, LONG source, LONG target,
                                 CPoint targetPos, LONG nHeight, SHORT nType)
{
    (void)nHeight;
    (void)nType;

    m_sourceId = source;
    m_targetId = target;
    m_pArea = pArea;

    CPoint ptTarget;
    if (m_targetId != CGameObjectArray::INVALID_INDEX) {
        // Homing: aim at the live target object's current position.
        CGameObject* pTargetObj;
        BYTE rc;
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(m_targetId,
                CGameObjectArray::THREAD_ASYNCH, &pTargetObj, INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
        if (rc != CGameObjectArray::SUCCESS) {
            return;
        }
        ptTarget = pTargetObj->GetPos();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(m_targetId,
            CGameObjectArray::THREAD_ASYNCH, INFINITE);
    } else {
        ptTarget = targetPos;
    }

    // Launch origin from the source (facing-adjusted).
    CPoint ptSource;
    if (!GetProjectileSourcePosition(m_sourceId, ptSource)) {
        return;
    }

    // Flight distance^2 (y weighted 16/9) -- the metric AIUpdate tests for
    // arrival; drives the lifetime.
    int dx = ptTarget.x - ptSource.x;
    int dy = ptTarget.y - ptSource.y;
    field_170 = dx * dx + (dy * dy * 16) / 9;

    // Distance-based lifetime (only when field_29D is set; Magic Missile leaves
    // it 0, keeping the 0x7FFF default).
    if (field_29D != 0) {
        m_lifetime = static_cast<SHORT>(
            static_cast<int>(sqrt(static_cast<double>(field_170))) / m_velocity + 1);
    }

    m_targetX = ptTarget.x;
    m_targetY = ptTarget.y;

    // Insert the projectile into the area so the engine drives its AIUpdate /
    // Render. CGameObject::AddToArea sets the base m_pos / m_posZ / area
    // membership and registers with the area's object lists. pArea and the
    // launch position (ptSource) are Frida-confirmed; posZ (the launch height)
    // is the documented animation-height stub, passed as 0.
    AddToArea(pArea, ptSource, 0, 0);

    // Subpixel launch position (1/1024 fixed point), seeded from the launch
    // origin. Frida-confirmed exact: X scaled << 10, Y also 4/3 y-scaled (the
    // iso squash) so AimAtPoint's decode reproduces m_pos. Without this the
    // flight would start from (0, 0).
    m_posAccumX = ptSource.x << 10;
    m_posAccumY = (ptSource.y << 12) / 3;

    // Initial facing toward the target, both points 4/3 y-scaled. The original
    // reads m_pos here -- which the preceding AddToArea set to this same launch
    // origin; AddToArea is not yet wired, so aim from ptSource directly.
    CPoint ptStartScaled;
    ptStartScaled.x = ptSource.x;
    ptStartScaled.y = (ptSource.y * 4) / 3;
    CPoint ptTargetScaled;
    ptTargetScaled.x = ptTarget.x;
    ptTargetScaled.y = (ptTarget.y * 4) / 3;
    m_facing = static_cast<SHORT>(CGameSprite::GetDirection(ptStartScaled, ptTargetScaled));
}

// 0x52BD20 (vtable slot 33 -- the per-tick motion integrator; AIUpdate's "aim")
//
// Ghidra recovers no function at the vtable target; transcribed from capstone
// disassembly (0x323 bytes, ret 8 -> __thiscall(this, int x, int y)). Steps the
// projectile one tick toward world point (x, y): computes the 16-direction
// facing, a velocity-scaled unit step in 1/1024 fixed point, accumulates it into
// the subpixel position, decodes that back to m_pos, and syncs an attached
// object's height.
//
// All work fields live in the CProjectile gap region in the binary
// (+0x9C..0xE0, +0x1DC) and are modelled by-name on CProjectileTravelling per
// the layout-drift note. The y axis is pre-scaled 4/3 to undo the isometric
// squash before the facing/distance maths (CGameSprite::GetDirection then
// applies its own iso ratios). The rand() term (_rand 0x7E8160) spreads the
// step within the [field_B4, field_BC) band when set -- 0 for Magic Missile, so
// the path stays straight. Depends on Fire seeding the subpixel position
// (m_posAccumX/Y), which is currently stubbed.
void CProjectileTravelling::AimAtPoint(int x, int y)
{
    // Facing toward the target; both points y-scaled 4/3 (undo the iso squash).
    CPoint ptStart;
    ptStart.x = m_pos.x;
    ptStart.y = (m_pos.y * 4) / 3;
    CPoint ptTarget;
    ptTarget.x = x;
    ptTarget.y = (y * 4) / 3;
    m_facing = static_cast<SHORT>(CGameSprite::GetDirection(ptStart, ptTarget));

    // Velocity-scaled unit step (1/1024 fixed point).
    int dx = x - m_pos.x;
    int dyScaled = ptTarget.y - ptStart.y;
    int dist = static_cast<int>(
        sqrt(static_cast<double>(dx * dx + dyScaled * dyScaled)) + 0.5);
    if (dist == 0) {
        m_stepX = 1;
        m_stepY = 1;
    } else {
        m_stepX = ((dx << 10) * m_velocity) / dist;
        m_stepY = ((dyScaled << 10) * m_velocity) / dist;
        m_targetX = x;
        m_targetY = y;

        // Fold in the per-tick carry, then a random spread within the band.
        m_stepX += field_AC;
        m_stepY += field_B0;
        int spreadX = field_BC - field_B4;
        if (spreadX > 0) {
            m_stepX += field_B4 + rand() % spreadX;
        }
        int spreadY = field_C0 - field_B8;
        if (spreadY > 0) {
            m_stepY += field_B8 + rand() % spreadY;
        }
    }

    // Advance the subpixel position by the step.
    m_posAccumX += m_stepX;
    m_posAccumY += m_stepY;

    // Bleed each carry down by one modulus per tick (zero once exhausted).
    if (field_AC < 0) {
        field_AC = (field_AC <= -field_E0) ? field_AC + field_E0 : 0;
    } else if (field_AC > 0) {
        field_AC = (field_AC >= field_E0) ? field_AC - field_E0 : 0;
    }
    if (field_B0 < 0) {
        field_B0 = (field_B0 <= -field_E0) ? field_B0 + field_E0 : 0;
    } else if (field_B0 > 0) {
        field_B0 = (field_B0 >= field_E0) ? field_B0 - field_E0 : 0;
    }

    // Decode the subpixel position back to the cell position (undo the 1/1024
    // fixed point and, for y, the 4/3 iso scale).
    m_pos.x = m_posAccumX >> 10;
    m_pos.y = ((m_posAccumY * 3) / 4) >> 10;

    // Keep the attached object (m_nTargetId) at the projectile's height.
    if (m_nTargetId != CGameObjectArray::INVALID_INDEX) {
        CGameObject* pObj;
        BYTE rc;
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(m_nTargetId,
                CGameObjectArray::THREAD_ASYNCH, &pObj, INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
        if (rc != CGameObjectArray::SUCCESS) {
            return;
        }
        pObj->m_posZ = m_posZ;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(m_nTargetId,
            CGameObjectArray::THREAD_ASYNCH, INFINITE);
    }
}

// 0x5297D0 (vtable slot 32 -- base blit flags)
//
// Trivial getter: `mov eax, [ecx+0xe6]; ret`. Returns the render-flags field
// the ctor seeds to 0x20000. (A Magic Missile render trace observed 0x20008 --
// the extra 0x8 is OR'd into the field elsewhere on the launch path, not by
// this getter.)
DWORD CProjectileTravelling::GetRenderFlags()
{
    return m_renderFlags;
}

// 0x52B6B0
//
// Cell draw rect + reference point, mirror/shadow aware. Transcribed from the
// original's base and mirror blocks; the shadow-cell combine (m_hasShadowCell)
// is not exercised by Magic Missile and the original's cell aliasing there is
// unresolved, so it falls back to the base cell bounds.
void CProjectileTravelling::GetCellBounds(CRect& rBounds, CPoint& ptRef)
{
    CPoint center;
    CSize size;

    if (m_mirror == 0 && m_hasShadowCell == 0) {
        m_pVidCell->GetCurrentCenterPoint(ptRef, FALSE);
        m_pVidCell->GetCurrentFrameSize(size, FALSE);
        rBounds.SetRect(0, 0, size.cx, size.cy);
    } else if (m_hasShadowCell != 0) {
        m_pVidCell->GetCurrentCenterPoint(ptRef, FALSE);
        m_pVidCell->GetCurrentFrameSize(size, FALSE);
        rBounds.SetRect(0, 0, size.cx, size.cy);
    }

    if (m_mirror != 0) {
        m_pVidCell->GetCurrentCenterPoint(center, FALSE);
        m_pVidCell->GetCurrentFrameSize(size, FALSE);
        if (1540 < m_direction && m_direction < 3596) {
            center.y = size.cy - center.y;
        }
        center.y += m_posZ;
        int minX = field_1CA;
        int minY = field_1CE;
        ptRef.x = center.x;
        ptRef.y = center.y;
        if (ptRef.x < minX) ptRef.x = minX;
        if (ptRef.y < minY) ptRef.y = minY;
        rBounds.SetRect(0, 0, size.cx + (ptRef.x - center.x), size.cy + (ptRef.y - center.y));
        int extX = (ptRef.x - minX) + field_1CA * 2;
        int extY = field_1CE * 2 + (ptRef.y - minY);
        if (rBounds.right < extX) rBounds.right = extX;
        if (rBounds.bottom < extY) rBounds.bottom = extY;
    }
}

// 0x52B190 (vtable slot 19 -- Render)
//
// Tile-based draw of a travelling projectile. The visibility/passability gates,
// the field-driven blit flags (mirror/tint/shadow), the tint, and the
// CInfinity FX pipeline are transcribed from the original; the render-config
// field semantics were Frida-confirmed (Magic Missile). The draw geometry
// follows the verified sibling CProjectileBAM::Render pattern -- the original's
// mirror-rect stack-local aliasing at 0x52B190 is decompile-ambiguous; verify
// with a render-rect Frida trace if a visual offset shows. Documented stubs:
// the shadow-cell second pass, the palette-swap path, and the multi-segment
// pass (FUN_0052C8C0 when (short)field_1D8 > 1) -- all disabled for the traced
// cast.
void CProjectileTravelling::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    (void)pVidMode;
    if (pArea == NULL) {
        return;
    }

    DWORD flags = GetRenderFlags();
    if (m_mirror != 0) {
        flags |= 0x200;
    }

    // Tile-visibility gate (32x32 visibility tiles).
    LONG tileIndex = (m_pos.y / 32) * pArea->m_visibility.m_nWidth + (m_pos.x / 32);
    if (!pArea->m_visibility.IsTileVisible(tileIndex)) {
        return;
    }

    // Passability/occlusion gate via the projectile terrain table (16x16 / 12y
    // search cells); 0xFF == blocked.
    static const BYTE TERRAIN[16] = { 0xFF, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
                                      0x05, 0x05, 0xFF, 0x05, 0x05, 0xFF, 0x05, 0x05 };
    CPoint searchPos(m_pos.x / 16, m_pos.y / 12);
    if (pArea->m_search.GetMobileCost(searchPos, TERRAIN, 3, TRUE) == 0xFF) {
        return;
    }
    if (m_visible == 0) {
        return;
    }

    if (m_tinted != 0) {
        flags |= 0x10000;
    }
    if (m_hasShadowCell != 0) {
        flags |= 0x4;
    }

    CRect rFX;
    CPoint ptRef;
    GetCellBounds(rFX, ptRef);

    CPoint newPos;
    newPos.x = m_pos.x;
    if (m_mirror == 0 && m_hasShadowCell == 0) {
        newPos.y = m_pos.y - m_posZ;
    } else {
        newPos.y = m_pos.y;
    }
    if (m_useHeightOffset != 0) {
        newPos.y += pArea->GetHeightOffset(m_pos, m_listType);
    }

    CRect rGCBounds;
    rGCBounds.left = newPos.x - ptRef.x;
    rGCBounds.top = newPos.y - ptRef.y;
    rGCBounds.right = rGCBounds.left + rFX.Width();
    rGCBounds.bottom = rGCBounds.top + rFX.Height();

    // Clip to the area viewport (CGameArea +0x514..0x560 -- unmodelled fields,
    // accessed by raw offset).
    const BYTE* pAreaBytes = reinterpret_cast<const BYTE*>(pArea);
    CRect rViewport;
    rViewport.left = *reinterpret_cast<const int*>(pAreaBytes + 0x55C);
    rViewport.top = *reinterpret_cast<const int*>(pAreaBytes + 0x560);
    rViewport.right = (*reinterpret_cast<const int*>(pAreaBytes + 0x51C)
                       - *reinterpret_cast<const int*>(pAreaBytes + 0x514)) + rViewport.left;
    rViewport.bottom = (*reinterpret_cast<const int*>(pAreaBytes + 0x520)
                        - *reinterpret_cast<const int*>(pAreaBytes + 0x518)) + rViewport.top;
    if (!IntersectRect(&rViewport, &rGCBounds, &rViewport)) {
        return;
    }

    // Direction-based mirror flags + base blit flag.
    if (2568 < m_direction) {
        flags |= 0x10;
    }
    if (1540 < m_direction && m_direction < 3596) {
        flags |= 0x20;
    }
    flags |= 0x80;

    COLORREF tintColor = RGB(0xFF, 0xFF, 0xFF);
    if (m_tinted != 0 || m_mirror != 0) {
        tintColor = pArea->GetTintColor(m_pos, m_listType);
    }

    CInfinity* pInfinity = pArea->GetInfinity();
    pInfinity->FXPrep(rFX, flags, nSurface, newPos, ptRef);
    if (pInfinity->FXLock(rFX, flags)) {
        if (m_tinted != 0) {
            m_pVidCell->SetTintColor(tintColor);
        }
        if (m_hasShadowCell != 0 && m_pShadowCell != NULL) {
            pInfinity->FXRender(m_pShadowCell, ptRef.x, ptRef.y, flags, 0);
        }
        // m_paletteSwap != 0 also swaps the cell palette before this draw
        // (CResBitmap colour table) -- documented; not exercised by the trace.
        pInfinity->FXRender(m_pVidCell, ptRef.x, ptRef.y, flags, 0x80);
        pInfinity->FXRenderClippingPolys(newPos.x, newPos.y, 0, ptRef, rViewport, FALSE, flags);
        pInfinity->FXUnlock(flags, NULL, CPoint(0, 0));
        pInfinity->FXBltFrom(nSurface, rFX, newPos.x, newPos.y, ptRef.x, ptRef.y, flags);
    }
}

#include "CProjectile.h"

#include <math.h>

#include "CBaldurChitin.h"
#include "CGameEffect.h"
#include "CGameArea.h"
#include "CGameObjectArray.h"
#include "CGameSprite.h"
#include "CInfinity.h"
#include "CInfGame.h"

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

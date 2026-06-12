#include "CGameTemporal.h"

#include <stdlib.h>

#include "CBaldurChitin.h"
#include "CGameArea.h"
#include "CGameObjectArray.h"
#include "CGameSprite.h"
#include "CInfGame.h"
#include "CPathSearch.h"
#include "CUtil.h"

// 0x85BD6C
const BYTE CGameTemporal::COLLISION_REBOUND = 0;

// 0x85BD6D
const BYTE CGameTemporal::COLLISION_DESTROY = 1;

// 0x85BD6E
const BYTE CGameTemporal::COLLISION_PASSTHROUGH = 2;

// 0x70FD50
CGameTemporal::CGameTemporal(USHORT animationID, BYTE* colorRangeValues, const CString& soundName,
    CGameArea* pArea, const CPoint& posExact, LONG posZ, const CPoint& posDelta,
    SHORT duration, BYTE durationFade, BYTE collision)
{
    CPoint ptOrigin(0, 0);
    SHORT nDirection = CGameSprite::GetDirection(ptOrigin, posDelta);

    m_animation.SetAnimationType(animationID, colorRangeValues, nDirection);

    m_sound.SetResRef(CResRef(soundName), TRUE, TRUE);
    m_sound.SetChannel(14, reinterpret_cast<DWORD>(pArea));

    m_animationRunning = 1;
    m_posExact = posExact;
    m_posDelta = posDelta;
    m_duration = duration;
    m_durationFade = durationFade;
    m_collision = collision;

    // Default projectile terrain-cost table (.data 0x8A8154): terrain types
    // 0, 10 and 13 are impassable, everything else costs 5.
    m_visibleTerrainTable[0] = CPathSearch::COST_IMPASSABLE;
    m_visibleTerrainTable[1] = 5;
    m_visibleTerrainTable[2] = 5;
    m_visibleTerrainTable[3] = 5;
    m_visibleTerrainTable[4] = 5;
    m_visibleTerrainTable[5] = 5;
    m_visibleTerrainTable[6] = 5;
    m_visibleTerrainTable[7] = 5;
    m_visibleTerrainTable[8] = 5;
    m_visibleTerrainTable[9] = 5;
    m_visibleTerrainTable[10] = CPathSearch::COST_IMPASSABLE;
    m_visibleTerrainTable[11] = 5;
    m_visibleTerrainTable[12] = 5;
    m_visibleTerrainTable[13] = CPathSearch::COST_IMPASSABLE;
    m_visibleTerrainTable[14] = 5;
    m_visibleTerrainTable[15] = 5;

    if (g_pBaldurChitin->GetObjectGame()->m_cObjectArray.Add(&m_id, this, -1)
        == CGameObjectArray::SUCCESS) {
        CPoint pos(posExact.x >> CGameSprite::EXACT_SCALE, posExact.y >> CGameSprite::EXACT_SCALE);
        AddToArea(pArea, pos, posZ, CGameObject::LIST_FRONT);
    } else {
        delete this;
    }
}

// 0x710000
CGameTemporal::~CGameTemporal()
{
}

// 0x710090
void CGameTemporal::AIUpdate()
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->m_nTimeStop != 0 && pGame->m_nTimeStopCaster != m_id) {
        return;
    }

    if (m_duration == 0) {
        // Permanent until the animation sequence runs out.
        if (m_animation.IsEndOfSequence()) {
            RemoveFromArea();
            return;
        }
    } else {
        m_duration--;
        if (m_duration == 0) {
            RemoveFromArea();
            return;
        }
    }

    int nOldCellX = m_pos.x / CPathSearch::GRID_SQUARE_SIZEX;
    int nOldCellY = m_pos.y / CPathSearch::GRID_SQUARE_SIZEY;

    m_posExact.x += m_posDelta.x;
    m_posExact.y += m_posDelta.y;
    m_pos.x = m_posExact.x >> CGameSprite::EXACT_SCALE;
    m_pos.y = m_posExact.y >> CGameSprite::EXACT_SCALE;

    int nNewCellX = m_pos.x / CPathSearch::GRID_SQUARE_SIZEX;
    int nNewCellY = m_pos.y / CPathSearch::GRID_SQUARE_SIZEY;

    if (nOldCellX != nNewCellX || nOldCellY != nNewCellY) {
        SHORT nTableIndex;
        BYTE cost = m_pArea->m_search.GetLOSCost(CPoint(nNewCellX, nNewCellY),
            m_visibleTerrainTable, nTableIndex, FALSE);
        if (cost == CPathSearch::COST_IMPASSABLE) {
            if (m_collision == COLLISION_DESTROY) {
                RemoveFromArea();
                return;
            }

            if (m_collision == COLLISION_REBOUND) {
                CPoint ptOld(m_posExact);
                if (nOldCellX != nNewCellX) {
                    m_posExact.x += -m_posDelta.x * 2;
                    m_pos.x = m_posExact.x >> CGameSprite::EXACT_SCALE;
                    m_posDelta.x = -m_posDelta.x;
                }

                if (nOldCellY != nNewCellY) {
                    m_posExact.y += -m_posDelta.y * 2;
                    m_pos.y = m_posExact.y >> CGameSprite::EXACT_SCALE;
                    m_posDelta.y = -m_posDelta.y;
                }

                SHORT nDirection = CGameSprite::GetDirection(ptOld, m_posExact);
                m_animation.ChangeDirection(nDirection);
            } else {
                // COLLISION_PASSTHROUGH: keep drifting, hidden while inside
                // the wall.
                m_animationRunning = 0;
            }
        } else {
            m_animationRunning = 1;
        }
    }

    m_animation.IncrementFrame();

    if (rand() % 200 == 0
        && !m_sound.IsSoundPlaying()) {
        m_sound.Play(m_pos.x, m_pos.y, 0, FALSE);
    }
}

// 0x710330
void CGameTemporal::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    // __FILE__: C:\Projects\Icewind2\src\Baldur\ObjCreature.cpp
    // __LINE__: 14202
    UTIL_ASSERT(pVidMode != NULL);

    if (!m_animationRunning) {
        return;
    }

    CInfinity* pInfinity = &m_pArea->m_cInfinity;

    LONG nTileIndex = (m_pos.y / CVisibilityMap::SQUARE_SIZEY) * m_pArea->m_visibility.m_nWidth
        + m_pos.x / CVisibilityMap::SQUARE_SIZEX;
    if (!m_pArea->m_visibility.IsTileVisible(nTileIndex)) {
        return;
    }

    BOOL bFadeOut = FALSE;
    if (m_duration < m_durationFade) {
        bFadeOut = TRUE;
    }

    CRect rView(pInfinity->nCurrentX,
        pInfinity->nCurrentY,
        pInfinity->nCurrentX + (pInfinity->rViewPort.right - pInfinity->rViewPort.left),
        pInfinity->nCurrentY + (pInfinity->rViewPort.bottom - pInfinity->rViewPort.top));

    CRect rFx;
    CPoint ptReference;
    m_animation.CalculateFxRect(rFx, ptReference, m_posZ);

    CPoint ptNewPos(m_pos.x, m_pos.y);
    ptNewPos.y += m_pArea->GetHeightOffset(m_pos, CGameObject::LIST_BACK);

    CRect rGCBounds;
    m_animation.CalculateGCBoundsRect(rGCBounds, ptNewPos, ptReference, m_posZ,
        rFx.right - rFx.left, rFx.bottom - rFx.top);

    CRect rDest;
    if (!rDest.IntersectRect(rGCBounds, rView)) {
        return;
    }

    CPoint ptTint(m_pos.x, m_pos.y + m_posZ);
    COLORREF rgbTintColor = m_pArea->GetTintColor(ptTint, m_listType);

    m_animation.Render(pInfinity, pVidMode, nSurface, rFx, ptNewPos, ptReference,
        0x30000, rgbTintColor, rDest, FALSE, bFadeOut, m_posZ, 0);
}

// 0x710550
void CGameTemporal::RemoveFromArea()
{
    CGameObject::RemoveFromArea();

    BYTE nResult = g_pBaldurChitin->GetObjectGame()->m_cObjectArray.Delete(m_id,
        CGameObjectArray::THREAD_ASYNCH, NULL, -1);
    if (nResult != CGameObjectArray::SUCCESS) {
        // __FILE__: C:\Projects\Icewind2\src\Baldur\ObjCreature.cpp
        // __LINE__: 14270
        UTIL_ASSERT(FALSE);
    }
}

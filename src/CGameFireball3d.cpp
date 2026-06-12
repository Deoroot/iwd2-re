#include "CGameFireball3d.h"

#include <math.h>

#include "CBaldurChitin.h"
#include "CGameArea.h"
#include "CGameObjectArray.h"
#include "CGameSprite.h"
#include "CGameTemporal.h"
#include "CInfGame.h"
#include "CPathSearch.h"
#include "CUtil.h"
#include "CVidMode.h"
#include "CWarp.h"

// 0x85BD70
const BYTE CGameFireball3d::TYPE_FIREBALL = 0;

// 0x7105B0
//
// Builds the burst at the strike point. Each type picks its explosion sound,
// cell, ring/center temporal animation sets and palette recolour; the shared
// tail precomputes the expanding particle table -- the ellipse arc pixel
// lists (CVidMode::GetEllipseArcPixelList) walked once horizontally and once
// vertically, each arc slot getting four quadrant-mirrored particles whose
// step is speed-scaled toward it -- then registers the object and spawns the
// center temporal (none for type 0).
CGameFireball3d::CGameFireball3d(BYTE nType, BYTE* colorRangeValues, CGameArea* pArea,
    const CPoint& pos, SHORT nRadius, BYTE nSpeed, BYTE nCollision, USHORT nHoldDuration)
    : m_spriteSplashPalette(CVidPalette::TYPE_RANGE)
{
    memcpy(m_colorRangeValues, colorRangeValues, sizeof(m_colorRangeValues));

    m_duration = static_cast<BYTE>((nRadius - 1) / nSpeed + 1);
    m_collision = nCollision;
    m_holdDuration = nHoldDuration;

    switch (nType) {
    case 0:
        m_sndExplosion.SetResRef(CResRef("EFF_M21"), TRUE, TRUE);
        m_sndExplosion.SetChannel(14, reinterpret_cast<DWORD>(pArea));
        m_sndExplosion.Play(pos.x, pos.y, 0, FALSE);
        m_sSoundTemporal = "";
        m_animationID = 0;
        m_animationIDStatic = 0;
        m_spriteSplashVidCell.SetResRef(CResRef("SPFIREPI"), FALSE, TRUE, TRUE);
        break;

    case 1:
    case 2:
        m_sndExplosion.SetResRef(CResRef("EFF_M18a"), TRUE, TRUE);
        m_sndExplosion.SetChannel(14, reinterpret_cast<DWORD>(pArea));
        m_sndExplosion.Play(pos.x, pos.y, 0, FALSE);
        m_sSoundTemporal = "EFF_M18c";
        m_animationID = 0x500;
        m_animationIDStatic = 0x510;
        m_spriteSplashVidCell.SetResRef(CResRef("SPBOOM"), FALSE, TRUE, TRUE);
        m_spriteSplashVidCell.SetPalette(m_spriteSplashPalette);
        m_spriteSplashPalette.SetRange(0, colorRangeValues[0],
            *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
        m_spriteSplashPalette.SetRange(1, colorRangeValues[1],
            *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
        break;

    case 3:
        m_sndExplosion.SetResRef(CResRef("EFF_M34"), TRUE, TRUE);
        m_sndExplosion.SetChannel(14, reinterpret_cast<DWORD>(pArea));
        m_sndExplosion.Play(pos.x, pos.y, 0, FALSE);
        m_sSoundTemporal = "";
        m_animationID = 0x600;
        m_animationIDStatic = 0x610;
        m_spriteSplashVidCell.SetResRef(CResRef("SPBOOM"), FALSE, TRUE, TRUE);
        m_spriteSplashVidCell.SetPalette(m_spriteSplashPalette);
        m_spriteSplashPalette.SetRange(0, colorRangeValues[0],
            *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
        m_spriteSplashPalette.SetRange(1, colorRangeValues[1],
            *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
        break;

    case 4:
        m_sndExplosion.SetResRef(CResRef("EFF_M31b"), TRUE, TRUE);
        m_sndExplosion.SetChannel(14, reinterpret_cast<DWORD>(pArea));
        m_sndExplosion.Play(pos.x, pos.y, 0, FALSE);
        m_sSoundTemporal = "";
        m_animationID = 0x700;
        m_animationIDStatic = 0x710;
        m_spriteSplashVidCell.SetResRef(CResRef("SPBOOM"), FALSE, TRUE, TRUE);
        m_spriteSplashVidCell.SetPalette(m_spriteSplashPalette);
        m_spriteSplashPalette.SetRange(0, colorRangeValues[0],
            *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
        break;

    case 5:
        m_sndExplosion.SetResRef(CResRef("EFF_M19"), TRUE, TRUE);
        m_sndExplosion.SetChannel(14, reinterpret_cast<DWORD>(pArea));
        m_sndExplosion.Play(pos.x, pos.y, 0, FALSE);
        m_sSoundTemporal = "";
        m_animationID = 0x800;
        m_animationIDStatic = 0x810;
        m_spriteSplashVidCell.SetResRef(CResRef("SPBOOM"), FALSE, TRUE, TRUE);
        m_spriteSplashVidCell.SetPalette(m_spriteSplashPalette);
        m_spriteSplashPalette.SetRange(0, colorRangeValues[0],
            *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
        break;

    default:
        // __FILE__: C:\Projects\Icewind2\src\Baldur\ObjCreature.cpp
        // __LINE__: 14401
        UTIL_ASSERT(FALSE);
    }

    m_spriteSplashVidCell.SequenceSet(0);
    if (nType == 4 || nType == 5) {
        // These play their splash backwards from the last frame.
        m_spriteSplashVidCell.FrameSet(static_cast<BYTE>(m_spriteSplashVidCell.GetSequenceLength(
            m_spriteSplashVidCell.m_nCurrentSequence, FALSE)) - 1);
    } else {
        m_spriteSplashVidCell.FrameSet(0);
    }

    m_pParticles = NULL;
    int nAxis = (nRadius - 1) / CPathSearch::GRID_SQUARE_SIZEX;
    m_nEllipse.x = nAxis + 1;
    m_nEllipse.y = nAxis + 1;
    m_flagEllipse = new BYTE[(nAxis + 2 + nAxis + 1) * (nAxis + 2 + nAxis + 1)];
    if (m_flagEllipse != NULL) {
        m_nEllipse.x = (nRadius - 1) / (CPathSearch::GRID_SQUARE_SIZEX * 2) + 1;
        m_nEllipse.y = m_nEllipse.x;

        CVidMode* pVidMode = NULL;
        if (g_pBaldurChitin->pActiveEngine != NULL) {
            pVidMode = g_pBaldurChitin->pActiveEngine->pVidMode;
        }

        int nArcX = pVidMode->GetEllipseArcPixelList(m_nEllipse.x, m_nEllipse.y, m_flagEllipse);
        m_arcLengthX = nArcX + 1;
        m_flagEllipse[nArcX] = 1;

        int nArcY = pVidMode->GetEllipseArcPixelList(m_nEllipse.y, m_nEllipse.x,
            m_flagEllipse + m_arcLengthX);
        m_arcLengthY = nArcY + 1;
        m_flagEllipse[m_arcLengthX + nArcY] = 1;

        m_nEllipse.x = (nRadius - 1) / CPathSearch::GRID_SQUARE_SIZEX + 1;
        m_nEllipse.y = m_nEllipse.x;

        int nQuadrant = m_arcLengthX + m_arcLengthY;
        m_pParticles = new SParticle[nQuadrant * 4];
        if (m_pParticles != NULL) {
            m_pParticleStatus = new BYTE[nQuadrant * 4];
            if (m_pParticleStatus != NULL) {
                memset(m_pParticleStatus, 0, nQuadrant * 4);

                // Horizontal arc walk: one particle column per arc step, the
                // walk dropping a grid row whenever the pixel list says so.
                int nCenterX = pos.x;
                int nCenterY = (pos.y * 4) / 3;
                int nWalkX = pos.x;
                int nWalkY = nCenterY + nRadius;
                for (SHORT i = 0; i < m_arcLengthX; i++) {
                    int nDeltaY = nWalkY - nCenterY;
                    int nDeltaX = nWalkX - nCenterX;
                    int nDist = static_cast<int>(
                        sqrt(static_cast<double>(nDeltaX * nDeltaX + nDeltaY * nDeltaY)));

                    SParticle* pQ0 = &m_pParticles[i];
                    SParticle* pQ1 = &m_pParticles[nQuadrant + i];
                    SParticle* pQ2 = &m_pParticles[nQuadrant * 2 + i];
                    SParticle* pQ3 = &m_pParticles[nQuadrant * 3 + i];
                    pQ0->x = 0;
                    pQ0->y = 0;
                    pQ1->x = 0;
                    pQ1->y = 0;
                    pQ2->x = 0;
                    pQ2->y = 0;
                    pQ3->x = 0;
                    pQ3->y = 0;

                    pQ0->stepX = nDeltaX * nSpeed * 0x400 / nDist;
                    pQ1->stepX = pQ0->stepX;
                    if (pQ0->stepX == 0) {
                        // The x-mirrored quadrants would duplicate the axis
                        // particle: mark them spent.
                        m_pParticleStatus[nQuadrant * 2 + i] = 2;
                        m_pParticleStatus[nQuadrant * 3 + i] = 2;
                    }
                    pQ2->stepX = -pQ0->stepX;
                    pQ3->stepX = -pQ0->stepX;

                    pQ0->stepY = nDeltaY * nSpeed * 0x400 / nDist;
                    pQ1->stepY = -pQ0->stepY;
                    pQ2->stepY = pQ0->stepY;
                    pQ3->stepY = -pQ0->stepY;

                    nWalkX += CPathSearch::GRID_SQUARE_SIZEX * 2;
                    nWalkY -= m_flagEllipse[i] * CPathSearch::GRID_SQUARE_SIZEY * 2;
                }

                // Vertical arc walk, filling the second half of each quadrant.
                nWalkX = pos.x + nRadius;
                nWalkY = nCenterY;
                for (SHORT i = 0; i < m_arcLengthY; i++) {
                    int nDeltaX = nWalkX - nCenterX;
                    int nDeltaY = nWalkY - nCenterY;
                    int nDist = static_cast<int>(
                        sqrt(static_cast<double>(nDeltaX * nDeltaX + nDeltaY * nDeltaY)));

                    SParticle* pQ0 = &m_pParticles[m_arcLengthX + i];
                    SParticle* pQ1 = &m_pParticles[nQuadrant + m_arcLengthX + i];
                    SParticle* pQ2 = &m_pParticles[nQuadrant * 2 + m_arcLengthX + i];
                    SParticle* pQ3 = &m_pParticles[nQuadrant * 3 + m_arcLengthX + i];
                    pQ0->x = 0;
                    pQ0->y = 0;
                    pQ1->x = 0;
                    pQ1->y = 0;
                    pQ2->x = 0;
                    pQ2->y = 0;
                    pQ3->x = 0;
                    pQ3->y = 0;

                    pQ0->stepX = nDeltaX * nSpeed * 0x400 / nDist;
                    pQ1->stepX = pQ0->stepX;
                    pQ2->stepX = -pQ0->stepX;
                    pQ3->stepX = -pQ0->stepX;

                    pQ0->stepY = nDeltaY * nSpeed * 0x400 / nDist;
                    pQ1->stepY = -pQ0->stepY;
                    pQ2->stepY = pQ0->stepY;
                    pQ3->stepY = -pQ0->stepY;
                    if (m_pParticles[i].stepY == 0) {
                        // The y-mirrored quadrants would duplicate the axis
                        // particle. (The original tests slot i here, not
                        // m_arcLengthX + i.)
                        m_pParticleStatus[nQuadrant + m_arcLengthX + i] = 2;
                        m_pParticleStatus[nQuadrant * 3 + m_arcLengthX + i] = 2;
                    }

                    nWalkY += CPathSearch::GRID_SQUARE_SIZEY * 2;
                    nWalkX -= m_flagEllipse[m_arcLengthX + i] * CPathSearch::GRID_SQUARE_SIZEX * 2;
                }

                // Default projectile terrain-cost table (.data 0x8A8154):
                // terrain types 0, 10 and 13 are impassable, everything else
                // costs 5.
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
                    AddToArea(pArea, pos, 0, CGameObject::LIST_FRONT);

                    if (m_animationID != 0) {
                        // The center temporal: the static animation at the
                        // burst point, living two ticks per ring plus the
                        // hold duration.
                        new CGameTemporal(m_animationIDStatic,
                            m_colorRangeValues,
                            m_sSoundTemporal,
                            m_pArea,
                            CPoint(m_pos.x << CGameSprite::EXACT_SCALE,
                                m_pos.y << CGameSprite::EXACT_SCALE),
                            0,
                            CPoint(0, 0),
                            static_cast<SHORT>(m_duration * 2 + m_holdDuration),
                            0,
                            m_collision);
                    }

                    return;
                }
            }
        }
    }

    delete this;
}

// 0x711C90 / 0x711CB0
CGameFireball3d::~CGameFireball3d()
{
    if (m_flagEllipse != NULL) {
        delete[] m_flagEllipse;
    }

    if (m_pParticles != NULL) {
        delete[] m_pParticles;
    }

    if (m_pParticleStatus != NULL) {
        delete[] m_pParticleStatus;
    }
}

// 0x711D90 (vtable slot 3)
//
// The particle tick. Counts a ring down per tick, advances the four particle
// quadrants along their steps, bounces/destroys/hides them against the
// terrain per the collision mode, drops a randomized interior temporal and
// one outward-drifting ring-front temporal per placement-grid cell at each
// live particle, and retires the burst once every particle is spent (or the
// rings ran out) and the splash cell finished its sequence.
void CGameFireball3d::AIUpdate()
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->m_nTimeStop != 0 && pGame->m_nTimeStopCaster != m_id) {
        return;
    }

    BOOL bAllPlaced = TRUE;

    m_duration--;
    if (m_duration == 0) {
        if (m_spriteSplashVidCell.IsEndOfSequence(FALSE)) {
            RemoveFromArea();
            return;
        }

        // Hold on the last ring until the splash finishes.
        m_duration++;
        m_spriteSplashVidCell.FrameAdvance();
        return;
    }

    // The arc buffer doubles as the per-tick ring-front placement grid.
    memset(m_flagEllipse, 0, (m_nEllipse.y * 2 + 1) * (m_nEllipse.x * 2 + 1));

    if ((m_arcLengthX + m_arcLengthY) * 4 > 0) {
        for (SHORT i = 0; i < (m_arcLengthX + m_arcLengthY) * 4; i++) {
            if (m_pParticleStatus[i] == 2) {
                continue;
            }

            SParticle* pParticle = &m_pParticles[i];
            pParticle->x += pParticle->stepX;
            pParticle->y += pParticle->stepY;

            CPoint ptCell(((pParticle->x >> 10) + m_pos.x) / CPathSearch::GRID_SQUARE_SIZEX,
                ((pParticle->y * 3 >> 12) + m_pos.y) / CPathSearch::GRID_SQUARE_SIZEY);

            SHORT nTableIndex;
            BYTE cost = m_pArea->m_search.GetLOSCost(ptCell, m_visibleTerrainTable,
                nTableIndex, FALSE);
            if (cost == CPathSearch::COST_IMPASSABLE) {
                if (m_collision == CGameTemporal::COLLISION_DESTROY) {
                    m_pParticleStatus[i] = 2;
                }

                if (m_collision != CGameTemporal::COLLISION_REBOUND) {
                    if (m_pParticleStatus[i] != 0) {
                        continue;
                    }

                    bAllPlaced = FALSE;
                    continue;
                }

                // Rebound: mirror whichever axis crossed a cell boundary.
                if ((((pParticle->x - pParticle->stepX) >> 10) + m_pos.x)
                        / CPathSearch::GRID_SQUARE_SIZEX
                    != ptCell.x) {
                    pParticle->x += pParticle->stepX * -2;
                    pParticle->stepX = -pParticle->stepX;
                }

                if ((((pParticle->y - pParticle->stepY) * 3 >> 12) + m_pos.y)
                        / CPathSearch::GRID_SQUARE_SIZEY
                    != ptCell.y) {
                    pParticle->y += pParticle->stepY * -2;
                    pParticle->stepY = -pParticle->stepY;
                }
            }

            // Interior temporal, randomized by the ring age (the original
            // divides the particle's y by the grid width here, not the
            // height).
            int nGridRow = ((pParticle->y / CPathSearch::GRID_SQUARE_SIZEX >> 10) + m_nEllipse.y)
                * (m_nEllipse.x * 2 + 1);
            if (m_animationID != 0) {
                UINT nChance = ((static_cast<UINT>(m_duration) * m_duration * m_duration) / 1000
                                   + 10)
                    * 3 / 2;
                if (nChance == 0 || rand() % nChance == 0) {
                    new CGameTemporal(m_animationIDStatic,
                        m_colorRangeValues,
                        m_sSoundTemporal,
                        m_pArea,
                        CPoint(pParticle->x + (m_pos.x << 10),
                            (pParticle->y * 3 >> 2) + (m_pos.y << 10)),
                        0,
                        CPoint(0, 0),
                        static_cast<SHORT>(m_duration * 2 + m_holdDuration),
                        0,
                        m_collision);
                }
            }

            // Ring front: one outward-drifting temporal per placement-grid
            // cell.
            BYTE* pGridCell = &m_flagEllipse[(pParticle->x / CPathSearch::GRID_SQUARE_SIZEX >> 10)
                + m_nEllipse.x + nGridRow];
            BYTE nPlaced = *pGridCell;
            *pGridCell = nPlaced + 1;
            if (nPlaced == 0 && m_pParticleStatus[i] == 0) {
                new CGameTemporal(m_animationID,
                    m_colorRangeValues,
                    m_sSoundTemporal,
                    m_pArea,
                    CPoint(pParticle->x + (m_pos.x << 10),
                        (pParticle->y * 3 >> 2) + (m_pos.y << 10)),
                    0,
                    CPoint(pParticle->stepX, pParticle->stepY * 3 >> 2),
                    m_duration,
                    0,
                    m_collision);
                m_pParticleStatus[i] = 1;
            } else if (m_pParticleStatus[i] == 0) {
                bAllPlaced = FALSE;
            }
        }

        if (!bAllPlaced) {
            m_spriteSplashVidCell.FrameAdvance();
            return;
        }
    }

    if (m_spriteSplashVidCell.IsEndOfSequence(FALSE)) {
        RemoveFromArea();
        return;
    }

    m_spriteSplashVidCell.FrameAdvance();
}

// 0x7122E0 (vtable slot 19)
//
// The burst itself draws nothing; its visuals are the temporals AIUpdate
// spawns.
void CGameFireball3d::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    (void)pArea;
    (void)nSurface;

    // __FILE__: C:\Projects\Icewind2\src\Baldur\ObjCreature.cpp
    // __LINE__: 14661
    UTIL_ASSERT(pVidMode != NULL);
}

// 0x712310 (vtable slot 18)
void CGameFireball3d::RemoveFromArea()
{
    CGameObject::RemoveFromArea();

    BYTE nResult = g_pBaldurChitin->GetObjectGame()->m_cObjectArray.Delete(m_id,
        CGameObjectArray::THREAD_ASYNCH, NULL, -1);
    if (nResult != CGameObjectArray::SUCCESS) {
        UTIL_ASSERT(FALSE);
    }
}

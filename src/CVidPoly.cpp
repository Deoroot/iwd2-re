#include "CVidPoly.h"

#include "CChitin.h"
#include "CUtil.h"
#include "CVidMode.h"
#include "CVideo3d.h"
#include "CWarp.h"

#include <new>
#include <string.h>

// 0xA07B20  Shared scratch the edge tables are built into. CVidPolyEdgeCache copies
// in and out of this fixed-address buffer, so cached edges' links stay valid.
static _EdgeDescription g_aEdgeScratch[CVPOLY_MAX_VERTICIES];

// 0xA09F20  Global LRU cache of recently built edge tables.
static CVidPolyEdgeCache g_edgeCache;

// 0x85EAA4
const BYTE CVidPoly::m_aDitherMask[] = {
    // clang-format off
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    0xAA, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55, 0x55,
    // clang-format on
};

// 0x7C0DB0
CVidPoly::CVidPoly()
{
    field_0 = 0;
    m_pVertices = NULL;
    m_pAET = NULL;
    m_pET = NULL;
    m_nVertices = -1;
    m_pDrawHLineFunction = &CVidPoly::DrawHLine16;
}

// 0x7C0DD0
LONG CVidPoly::CalculateLineVIntersection(const CPoint& lineStart, const CPoint& lineEnd, LONG vertical, const CPoint& linePrev)
{
    if (lineStart.x == vertical) {
        if ((lineEnd.x <= vertical || linePrev.x <= vertical)
            && (lineEnd.x >= vertical || linePrev.x >= vertical)) {
            return lineStart.y;
        }
    } else if (lineEnd.x != vertical) {
        if ((lineStart.x >= vertical || lineEnd.x >= vertical)
            && (lineStart.x <= vertical || lineEnd.x <= vertical)) {
            return lineStart.y + (vertical - lineStart.x) * (lineEnd.y - lineStart.y) / (lineEnd.x - lineStart.x);
        }
    }

    return -1;
}

// 0x7C0E40
BOOLEAN CVidPoly::IsPtInPoly(const CPoint* pPoly, SHORT nPoly, const CPoint& pt)
{
    SHORT numIntersectingEdges = 0;
    SHORT cnt;
    LONG interSectionY;

    // __FILE__: C:\Projects\Icewind2\src\chitin\ChVidPoly.cpp
    // __LINE__: 166
    UTIL_ASSERT(pPoly != NULL && nPoly > 0);

    if (nPoly == 1) {
        return pPoly[0] == pt;
    } else if (nPoly == 2) {
        interSectionY = CalculateLineVIntersection(pPoly[0], pPoly[2], pt.x, pPoly[1]);
        return interSectionY == pt.y;
    }

    for (cnt = 0; cnt < nPoly; cnt++) {
        if (cnt == nPoly - 1) {
            interSectionY = CalculateLineVIntersection(pPoly[cnt], pPoly[0], pt.x, pPoly[cnt - 1]);
        } else if (cnt != 0) {
            interSectionY = CalculateLineVIntersection(pPoly[cnt], pPoly[cnt + 1], pt.x, pPoly[cnt - 1]);
        } else {
            interSectionY = CalculateLineVIntersection(pPoly[0], pPoly[cnt + 1], pt.x, pPoly[nPoly - 1]);
        }

        if (interSectionY == pt.y) {
            return TRUE;
        }

        if (interSectionY != -1 && interSectionY < pt.y) {
            numIntersectingEdges++;
        }
    }

    return (numIntersectingEdges & 1);
}

// 0x7C0F40
BOOL CVidPoly::FillConvexPoly(WORD* pSurface, LONG lPitch, const CRect& rClipRect, DWORD dwColor, DWORD dwFlags, const CPoint& ptRef)
{
    SHORT nRightXDir;
    SHORT nLeftXDir;
    UINT nMinYIndex;
    UINT nMinYPt;
    UINT nMaxYPt;
    UINT nRightX;
    UINT nLeftX;
    UINT nRightIndex;
    UINT nLeftIndex;
    INT nRightDx;
    INT nRightDy;
    UINT nRightAdjUp;
    UINT nRightRun;
    INT nRightErrTerm;
    INT nLeftDx;
    INT nLeftDy;
    UINT nLeftAdjUp;
    UINT nLeftRun;
    INT nLeftErrTerm;
    SHORT nIndex;
    CRect rClip;
    CPoint ptRefAdjusted;

    ptRefAdjusted.x = ptRef.x;
    ptRefAdjusted.y = ptRef.y;

    rClip.left = rClipRect.left;
    rClip.top = rClipRect.top;
    rClip.right = rClipRect.right - 1;
    rClip.bottom = rClipRect.bottom - 1;

    nLeftXDir = 1;
    nRightXDir = 1;

    if (m_nVertices < 3) {
        return FALSE;
    }

    // __FILE__: C:\Projects\Icewind2\src\chitin\ChVidPoly.cpp
    // __LINE__: 267
    UTIL_ASSERT_MSG(m_nVertices < CVPOLY_MAX_VERTICIES, "Excessive poly vertex count");

    SetHLineFunction(dwFlags);

    nMinYIndex = 0;
    nMinYPt = m_pVertices[nMinYIndex].y;
    nMaxYPt = m_pVertices[nMinYIndex].y;
    ;
    for (nIndex = 1; nIndex < m_nVertices; nIndex++) {
        if (m_pVertices[nIndex].y < nMinYPt) {
            nMinYPt = m_pVertices[nIndex].y;
            nMinYIndex = nIndex;
        } else if (m_pVertices[nIndex].y > nMaxYPt) {
            nMaxYPt = m_pVertices[nIndex].y;
        }
    }

    nRightX = m_pVertices[nMinYIndex].x;
    nLeftX = m_pVertices[nMinYIndex].x;
    nRightIndex = (nMinYIndex + 1) % m_nVertices;
    nLeftIndex = (nMinYIndex + m_nVertices - 1) % m_nVertices;

    nRightDx = m_pVertices[nRightIndex].x - nRightX;
    if (nRightDx < 0) {
        nRightXDir = -1;
        nRightDx = -nRightDx;
    }
    nRightDy = m_pVertices[nRightIndex].y - nMaxYPt;

    if (nRightDy > 0) {
        nRightRun = nRightDx / nRightDy;
        nRightAdjUp = nRightDx % nRightDy;
        nRightErrTerm = nRightAdjUp - nRightDy;
    } else {
        nRightRun = nRightDx;
        nRightAdjUp = nRightDx;
        nRightErrTerm = nRightDx;
    }

    nLeftDx = m_pVertices[nLeftIndex].x - nLeftX;
    if (nLeftDx < 0) {
        nLeftXDir = -1;
        nLeftDx = -nLeftDx;
    }
    nLeftDy = m_pVertices[nLeftIndex].y - nMaxYPt;

    if (nLeftDy > 0) {
        nLeftRun = nLeftDx / nLeftDy;
        nLeftAdjUp = nLeftDx % nLeftDy;
        nLeftErrTerm = nRightAdjUp - nLeftDy;
    } else {
        nLeftRun = nLeftDx;
        nLeftAdjUp = nLeftDx;
        nLeftErrTerm = nLeftDx;
    }

    if ((dwFlags & 0x8) != 0) {
        pSurface = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(pSurface) + lPitch * (rClip.bottom - nMinYPt));
        lPitch = -lPitch;
    } else {
        pSurface = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(pSurface) + lPitch * (nMinYPt - rClip.top));
    }

    ptRefAdjusted.y += nMinYPt - rClip.top;

    while (nMinYPt <= nMaxYPt) {
        if (nMinYPt == m_pVertices[nRightIndex].y) {
            do {
                nRightX = m_pVertices[nRightIndex].x;
                nRightIndex = (nRightIndex + 1) % m_nVertices;
                nRightDy = m_pVertices[nRightIndex].y - nMinYPt;
            } while (nRightDy == 0);

            nRightDx = m_pVertices[nRightIndex].x - nRightX;
            if (nRightDx < 0) {
                nRightXDir = -1;
                nRightDx = -nRightDx;
            } else {
                nRightXDir = 1;
            }

            nRightRun = nRightDx / nRightDy;
            nRightAdjUp = nRightDx % nRightDy;
            nRightErrTerm = nRightAdjUp - nRightDy;
        }

        if (nMinYPt >= m_pVertices[nLeftIndex].y) {
            do {
                nLeftX = m_pVertices[nLeftIndex].x;
                nLeftIndex = (nLeftIndex + m_nVertices - 1) % m_nVertices;
                nLeftDy = m_pVertices[nLeftIndex].y - nMinYPt;
            } while (nLeftDy == 0);

            nLeftDx = m_pVertices[nLeftIndex].x - nLeftX;
            if (nLeftDx < 0) {
                nLeftXDir = -1;
                nLeftDx = -nLeftDx;
            } else {
                nLeftXDir = 1;
            }

            nLeftRun = nLeftDx / nLeftDy;
            nLeftAdjUp = nLeftDx % nLeftDy;
            nLeftErrTerm = nLeftAdjUp - nLeftDy;
        }

        // NOTE: Signed compare.
        if (static_cast<LONG>(nMinYPt) >= rClip.top && static_cast<LONG>(nMinYPt) < rClip.bottom) {
            INT xMin;
            INT xMax;
            if (nLeftX > nRightX) {
                xMax = min(static_cast<LONG>(nLeftX), rClip.right) - rClip.left;
                xMin = max(static_cast<LONG>(nRightX) - rClip.left, 0);
            } else {
                xMax = min(static_cast<LONG>(nRightX), rClip.right) - rClip.left;
                xMin = max(static_cast<LONG>(nLeftX) - rClip.left, 0);
            }

            (this->*m_pDrawHLineFunction)(pSurface,
                xMin,
                xMax,
                dwColor,
                rClip,
                ptRefAdjusted);
        }

        nLeftX += nLeftRun * nLeftXDir;
        nLeftErrTerm += nLeftAdjUp;
        if (nLeftErrTerm > 0) {
            nLeftX += nLeftXDir;
            nLeftErrTerm -= nLeftAdjUp + nLeftDx;
        }

        nRightX += nRightRun * nRightXDir;
        nRightErrTerm += nRightAdjUp;
        if (nRightErrTerm > 0) {
            nRightX += nRightXDir;
            nRightErrTerm -= nRightAdjUp + nRightDx;
        }

        nMinYPt++;
        ptRefAdjusted.y++;
        pSurface = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(pSurface) + lPitch);
    }

    return TRUE;
}

// 0x7C13A0
BOOL CVidPoly::FillPoly(WORD* pSurface, LONG lPitch, const CRect& rClipRect, DWORD dwColor, DWORD dwFlags, const CPoint& ptRef)
{
    CPoint ptRefAdjusted;
    CRect rClip;

    ptRefAdjusted.x = ptRef.x;
    ptRefAdjusted.y = ptRef.y;

    rClip.left = rClipRect.left;
    rClip.top = rClipRect.top;
    rClip.right = rClipRect.right - 1;
    rClip.bottom = rClipRect.bottom - 1;

    LONG lScanlinePitch = lPitch;

    if (m_nVertices < 3) {
        return FALSE;
    }

    // __FILE__: C:\Projects\Icewind2\src\chitin\ChVidPoly.cpp
    // __LINE__: 546
    UTIL_ASSERT_MSG(m_nVertices < CVPOLY_MAX_VERTICIES, "Excessive poly vertex count");

    if (m_nVertices >= CVPOLY_MAX_VERTICIES) {
        return FALSE;
    }

    SetHLineFunction(dwFlags);

    // Build the edge table, reusing a cached one when possible (flag 0x2 forces a rebuild).
    if ((dwFlags & 0x2) == 0) {
        if (!g_edgeCache.FindCached(this)) {
            BuildEdgeTable(g_aEdgeScratch);
            g_edgeCache.Cache(this);
        }
    } else {
        BuildEdgeTable(g_aEdgeScratch);
    }

    m_pAET = NULL;

    SHORT nScanline = static_cast<SHORT>(m_pET->nYMin);

    if ((dwFlags & 0x8) != 0) {
        pSurface = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(pSurface) + lPitch * (rClip.bottom - nScanline));
        lScanlinePitch = -lPitch;
    } else {
        pSurface = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(pSurface) + lPitch * (nScanline - rClip.top));
    }

    ptRefAdjusted.y += nScanline - rClip.top;

    while (m_pET != NULL || m_pAET != NULL) {
        // Move every edge that opens on this scanline from the ET into the AET,
        // keeping the AET sorted by current X.
        _EdgeDescription* pEdge = m_pET;
        _EdgeDescription* pActive = m_pAET;
        _EdgeDescription** ppInsert = &m_pAET;
        if (pEdge != NULL) {
            do {
                if (pEdge->nYMin != static_cast<INT>(nScanline)) {
                    break;
                }

                _EdgeDescription* pScan = pActive;
                while ((pActive = pScan,
                           pActive != NULL && pActive->nX < static_cast<INT>(static_cast<SHORT>(pEdge->nX)))) {
                    ppInsert = &pActive->pNext;
                    pScan = pActive->pNext;
                }

                *ppInsert = pEdge;
                m_pET = m_pET->pNext;
                pEdge->pNext = pActive;
                ppInsert = &pEdge->pNext;
                pEdge = m_pET;
            } while (pEdge != NULL);
        }

        // Draw the spans between consecutive AET edge pairs, clipped to rClip.
        if (rClip.top <= nScanline && nScanline <= rClip.bottom) {
            _EdgeDescription* pSpan = m_pAET;
            while (pSpan != NULL) {
                INT xMin;
                if (rClip.left < pSpan->nX) {
                    xMin = pSpan->nX - rClip.left;
                } else {
                    xMin = 0;
                }

                _EdgeDescription* pSpanEnd = pSpan->pNext;
                INT xMax = pSpanEnd->nX;
                if (xMax < rClip.right) {
                    xMax = xMax - rClip.left;
                } else {
                    xMax = rClip.right - rClip.left;
                }

                (this->*m_pDrawHLineFunction)(pSurface, xMin, xMax, dwColor, rClip, ptRefAdjusted);

                pSpan = pSpanEnd->pNext;
            }
        }

        AdvanceActiveEdges();

        // Re-sort the AET by X; advancing the edges may have reordered them.
        if (m_pAET != NULL) {
            bool bSwapped;
            do {
                _EdgeDescription* pCur = m_pAET;
                _EdgeDescription** ppLink = &m_pAET;
                bSwapped = false;
                _EdgeDescription* pNext = pCur->pNext;
                if (pNext == NULL) {
                    break;
                }

                do {
                    if (pNext->nX < pCur->nX) {
                        *ppLink = pNext;
                        bSwapped = true;
                        pCur->pNext = pNext->pNext;
                        pNext->pNext = pCur;
                    }

                    ppLink = &(*ppLink)->pNext;
                    pCur = *ppLink;
                    pNext = pCur->pNext;
                } while (pNext != NULL);
            } while (bSwapped);
        }

        nScanline++;
        ptRefAdjusted.y++;
        pSurface = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(pSurface) + lScanlinePitch);
    }

    return TRUE;
}

// 0x7C15F0
BOOL CVidPoly::FillConvexPoly3d(const CRect& rClip, DWORD dwColor, DWORD dwFlags, const CPoint& ptRef)
{
    if (m_nVertices < 3) {
        return FALSE;
    }

    CVideo3d::glColor4f(static_cast<float>(GetRValue(dwColor)) / 255.0f,
        static_cast<float>(GetRValue(dwColor)) / 255.0f,
        static_cast<float>(GetRValue(dwColor)) / 255.0f,
        static_cast<float>(dwColor >> 24) / 255.0f);
    g_pChitin->GetCurrentVideoMode()->CheckResults3d(0);

    if ((dwFlags & 0x2) != 0) {
        CVideo3d::glPolygonStipple(m_aDitherMask);
        g_pChitin->GetCurrentVideoMode()->CheckResults3d(0);

        CVideo3d::glEnable(GL_POLYGON_STIPPLE);
        g_pChitin->GetCurrentVideoMode()->CheckResults3d(0);
    }

    CVideo3d::glDisable(GL_BLEND);
    g_pChitin->GetCurrentVideoMode()->CheckResults3d(0);

    CVideo3d::glDisable(GL_TEXTURE_2D);
    g_pChitin->GetCurrentVideoMode()->CheckResults3d(0);

    CVideo3d::glBegin(GL_POLYGON);
    for (INT nIndex = 0; nIndex < m_nVertices - 1; nIndex++) {
        INT nXFrom = m_pVertices[nIndex].x;
        INT nYFrom = m_pVertices[nIndex].y;
        INT nXTo = m_pVertices[nIndex + 1].x;
        INT nYTo = m_pVertices[nIndex + 1].y;
        if (CVidMode::ClipLine(nXFrom, nYFrom, nXTo, nYTo, rClip)) {
            CVideo3d::glVertex3f(static_cast<float>(nXFrom) + CVideo3d::SUB_PIXEL_SHIFT,
                static_cast<float>(nYFrom) + CVideo3d::SUB_PIXEL_SHIFT,
                0.0f);
            CVideo3d::glVertex3f(static_cast<float>(nXTo) + CVideo3d::SUB_PIXEL_SHIFT,
                static_cast<float>(nYTo) + CVideo3d::SUB_PIXEL_SHIFT,
                0.0f);
        }
    }

    CVideo3d::glEnd();
    g_pChitin->GetCurrentVideoMode()->CheckResults3d(0);

    if ((dwFlags & 0x2) != 0) {
        CVideo3d::glDisable(GL_POLYGON_STIPPLE);
        g_pChitin->GetCurrentVideoMode()->CheckResults3d(0);
    }

    return TRUE;
}

// 0x7C18B0
void CVidPoly::SetPoly(WORD* pVertices, WORD nVertices)
{
    m_pVertices = reinterpret_cast<CVIDPOLY_VERTEX*>(pVertices);
    m_nVertices = nVertices;
}

// 0x7C18D0
void CVidPoly::SetPoly(CVIDPOLY_VERTEX* pVertices, WORD nVertices)
{
    m_pVertices = pVertices;
    m_nVertices = nVertices;
}

// 0x7C18F0
void CVidPoly::AdvanceActiveEdges()
{
    _EdgeDescription* pEdge = m_pAET;
    _EdgeDescription* pPrev = NULL;

    while (pEdge != NULL) {
        INT nCount = pEdge->nCount;
        pEdge->nCount = nCount - 1;

        // Active edges survive one extra scanline once the edge table is exhausted.
        INT nExpire = (m_pET == NULL) ? -1 : 0;
        if (nCount - 1 == nExpire) {
            if (pPrev == NULL) {
                m_pAET = pEdge->pNext;
            } else {
                pPrev->pNext = pEdge->pNext;
            }
        } else {
            pEdge->nX += pEdge->nWholeStep;
            INT nErrTerm = pEdge->nErrTerm;
            pEdge->nErrTerm = nErrTerm + pEdge->nErrAdjUp;
            pPrev = pEdge;
            if (pEdge->nErrTerm >= 0) {
                pEdge->nX += pEdge->nXDir;
                pEdge->nErrTerm -= pEdge->nDy;
            }
        }

        pEdge = pEdge->pNext;
    }
}

// 0x7C1970
void CVidPoly::BuildEdgeTable(_EdgeDescription* pEdges)
{
    INT nVertices = m_nVertices;

    m_pET = NULL;

    INT nIndex = 0;
    if (nVertices <= 0) {
        return;
    }

    do {
        INT nNext = nIndex + 1;
        INT nNextIndex = static_cast<SHORT>(nNext % nVertices);

        WORD yCur = m_pVertices[nIndex].y;
        WORD yNext = m_pVertices[nNextIndex].y;

        INT nXStart;
        INT nXEnd;
        INT nYMin;
        INT nYMax;
        if (yCur < yNext) {
            nXStart = m_pVertices[nIndex].x;
            nXEnd = m_pVertices[nNextIndex].x;
            nYMax = yNext;
            nYMin = yCur;
        } else {
            nXStart = m_pVertices[nNextIndex].x;
            nXEnd = m_pVertices[nIndex].x;
            nYMax = yCur;
            nYMin = yNext;
        }

        INT nDy = nYMax - nYMin;
        if (nDy != 0) {
            INT nDx = nXEnd - nXStart;
            INT nXDir = (nDx >= 0) ? 1 : -1;
            INT nAbsDx = (nDx < 0) ? -nDx : nDx;

            pEdges->nErrTerm = 1 - nDy;
            pEdges->nXDir = nXDir;
            pEdges->pNext = NULL;
            pEdges->nX = nXStart;
            pEdges->nYMin = nYMin;
            pEdges->nCount = nDy;
            pEdges->nDy = nDy;
            pEdges->nWholeStep = (nAbsDx / nDy) * nXDir;
            pEdges->nErrAdjUp = nAbsDx % nDy;

            // Insert into the edge table, sorted by nYMin then nX.
            _EdgeDescription* pScan = m_pET;
            _EdgeDescription** ppLink = &m_pET;
            _EdgeDescription* pCur;
            while ((pCur = pScan) != NULL
                && pCur->nYMin <= nYMin
                && (pCur->nYMin != nYMin || pCur->nX <= nXStart)) {
                ppLink = &pCur->pNext;
                pScan = pCur->pNext;
            }
            *ppLink = pEdges;
            pEdges->pNext = pCur;

            pEdges++;
        }

        nIndex = nNext;
        nVertices = m_nVertices;
    } while (nIndex < nVertices);
}

// 0x7C1AA0
void CVidPoly::DrawHLine16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned short* pSurface16 = reinterpret_cast<unsigned short*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface16 += xMin;

        for (int x = 0; x < width; x++) {
            *pSurface16++ = static_cast<unsigned short>(dwColor);
        }
    }
}

// 0x7C1AF0
void CVidPoly::DrawHLineMirrored16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned short* pSurface16 = reinterpret_cast<unsigned short*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface16 += rSurface.Width() - xMin;

        for (int x = 0; x < width; x++) {
            *pSurface16-- = static_cast<unsigned short>(dwColor);
        }
    }
}

// 0x7C1B50
void CVidPoly::DrawHLineDithered16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned short* pSurface16 = reinterpret_cast<unsigned short*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface16 += xMin;

        if ((ptRef.y & 1) != 0) {
            if (((xMin + ptRef.x) & 1) != 0) {
                pSurface16++;
                width--;
            }
        } else {
            if (((xMin + ptRef.x) & 1) == 0) {
                pSurface16++;
                width--;
            }
        }

        width /= 2;
        for (int x = 0; x < width; x++) {
            *pSurface16 = static_cast<unsigned short>(dwColor);
            pSurface16 += 2;
        }
    }
}

// 0x7C1BB0
void CVidPoly::DrawHLineDitheredMirrored16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned short* pSurface16 = reinterpret_cast<unsigned short*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface16 += rSurface.Width() - xMin;

        if ((ptRef.y & 1) != 0) {
            if (((rSurface.Width() + xMin + ptRef.x - 1) & 1) != 0) {
                pSurface16--;
                width--;
            }
        } else {
            if (((rSurface.Width() + xMin + ptRef.x - 1) & 1) == 0) {
                pSurface16--;
                width--;
            }
        }

        width /= 2;
        for (int x = 0; x < width; x++) {
            *pSurface16 = static_cast<unsigned short>(dwColor);
            pSurface16 -= 2;
        }
    }
}

// 0x7C1C20
void CVidPoly::SetHLineFunction(DWORD dwFlags)
{
    switch (g_pChitin->cVideo.m_nBpp) {
    case 32:
        if ((dwFlags & 0x1) != 0) {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineMirrored32;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLine32;
            }
        } else if ((dwFlags & 0x10) != 0) {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineShadedMirrored32;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineShaded32;
            }
        } else {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineDitheredMirrored32;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineDithered32;
            }
        }
        break;
    case 24:
        if ((dwFlags & 0x1) != 0) {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineMirrored24;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLine24;
            }
        } else if ((dwFlags & 0x10) != 0) {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineShadedMirrored24;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineShaded24;
            }
        } else {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineDitheredMirrored24;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineDithered24;
            }
        }
        break;
    default:
        if ((dwFlags & 0x1) != 0) {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineMirrored16;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLine16;
            }
        } else if ((dwFlags & 0x10) != 0) {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineShadedMirrored16;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineShaded16;
            }
        } else {
            if ((dwFlags & 0x4) != 0) {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineDitheredMirrored16;
            } else {
                m_pDrawHLineFunction = &CVidPoly::DrawHLineDithered16;
            }
        }
        break;
    }
}

// Constructed inline by CVidPolyEdgeCache::Cache (operator new at 0x7C1F1B).
CVidPolyEdgeCacheEntry::CVidPolyEdgeCacheEntry()
{
    m_nCount = 0;
    m_pET = NULL;
    m_pEdges = NULL;
    m_pVertices = NULL;
}

// 0x7C1D40
CVidPolyEdgeCacheEntry::~CVidPolyEdgeCacheEntry()
{
    if (m_pEdges != NULL) {
        ::operator delete(m_pEdges);
    }
}

// 0x7C1DE0
BOOL CVidPolyEdgeCache::FindCached(CVidPoly* pPoly)
{
    m_cs.Lock();

    POSITION pos = GetHeadPosition();
    while (pos != NULL) {
        POSITION posEntry = pos;
        CVidPolyEdgeCacheEntry* pEntry = GetNext(pos);
        if (pEntry != NULL
            && pEntry->m_pVertices == pPoly->m_pVertices
            && pEntry->m_nCount == pPoly->m_nVertices) {
            // Hit: restore the cached edge table into the shared scratch buffer.
            if (pEntry->m_pEdges != NULL && pEntry->m_pET != NULL && pEntry->m_pVertices != NULL) {
                pPoly->m_pET = pEntry->m_pET;
                memcpy(g_aEdgeScratch, pEntry->m_pEdges, pEntry->m_nCount * sizeof(_EdgeDescription));
                pPoly->m_nVertices = pEntry->m_nCount;
                pPoly->m_pVertices = pEntry->m_pVertices;
            }

            // Promote the entry to most-recently-used.
            RemoveAt(posEntry);
            AddHead(pEntry);

            m_cs.Unlock();
            return TRUE;
        }
    }

    m_cs.Unlock();
    return FALSE;
}

// 0x7C1EC0
void CVidPolyEdgeCache::Cache(CVidPoly* pPoly)
{
    m_cs.Lock();

    // The original releases the lock from a __try/__finally; an RAII guard gives the
    // same "unlock on every exit" under the project's C++ exception model.
    struct Unlocker {
        CCriticalSection* pcs;
        ~Unlocker() { pcs->Unlock(); }
    } unlocker = { &m_cs };

    // Evict least-recently-used entries while the cache is full.
    while (GetCount() >= 16) {
        CVidPolyEdgeCacheEntry* pOldest = RemoveTail();
        if (pOldest != NULL) {
            delete pOldest;
        }
    }

    if (pPoly != NULL) {
        CVidPolyEdgeCacheEntry* pEntry = ::new (std::nothrow) CVidPolyEdgeCacheEntry;
        if (pEntry != NULL) {
            if (pPoly->m_nVertices != 0) {
                pEntry->m_pEdges = static_cast<_EdgeDescription*>(
                    ::operator new(pPoly->m_nVertices * sizeof(_EdgeDescription), std::nothrow));
                if (pEntry->m_pEdges != NULL) {
                    memcpy(pEntry->m_pEdges, g_aEdgeScratch, pPoly->m_nVertices * sizeof(_EdgeDescription));
                    pEntry->m_pET = pPoly->m_pET;
                    pEntry->m_nCount = pPoly->m_nVertices;
                    pEntry->m_pVertices = pPoly->m_pVertices;
                }
            }

            AddTail(pEntry);
        }
    }
}

// 0x7C1FD0
// Shaded span: every pixel of the fill colour is modulated by the brightness of the
// pixel already on the surface, giving the translucent highlight overlay. The original
// delegates the per-pixel maths to two CVidMode helpers reached through the active
// engine's video mode — a weighted luminance of the destination pixel (0x79AD90) and a
// per-channel scale of the fill colour by that luminance (0x79AD20); both are inlined
// here against CVidMode's RGB masks/shifts.
void CVidPoly::DrawHLineShaded16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    CWarp* pActiveEngine = g_pChitin->pActiveEngine;
    CVidMode* pVidMode = (pActiveEngine != NULL) ? pActiveEngine->pVidMode : NULL;

    int width = xMax - xMin + 1;
    if (width > 0) {
        WORD color16 = static_cast<WORD>(dwColor);
        unsigned short* pSurface16 = reinterpret_cast<unsigned short*>(pSurface) + xMin;

        for (int x = 0; x < width; x++) {
            WORD dst = *pSurface16;

            // 0x79AD90: weighted destination luminance (green doubled).
            DWORD luminance =
                  (((pVidMode->m_dwBBitMask & dst) >> pVidMode->m_dwBBitShift) << pVidMode->field_CA)
                + (((pVidMode->m_dwGBitMask & dst) >> pVidMode->m_dwGBitShift) << pVidMode->field_C6) * 2
                + (((pVidMode->m_dwRBitMask & dst) >> pVidMode->m_dwRBitShift) << pVidMode->field_C2);
            DWORD scale = luminance >> 2;

            // 0x79AD20: scale each channel of the fill colour by scale / 256.
            *pSurface16 = static_cast<unsigned short>(
                  (((((pVidMode->m_dwRBitMask & color16) >> pVidMode->m_dwRBitShift) * scale) >> 8) << pVidMode->m_dwRBitShift)
                | (((((pVidMode->m_dwGBitMask & color16) >> pVidMode->m_dwGBitShift) * scale) >> 8) << pVidMode->m_dwGBitShift)
                | (((((pVidMode->m_dwBBitMask & color16) >> pVidMode->m_dwBBitShift) * scale) >> 8) << pVidMode->m_dwBBitShift));

            pSurface16++;
        }
    }
}

// 0x7C2040
// Right-to-left twin of DrawHLineShaded16 (mirror FX); same destination-luminance
// modulation, addressed from rSurface.Width() - xMin downwards.
void CVidPoly::DrawHLineShadedMirrored16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    CWarp* pActiveEngine = g_pChitin->pActiveEngine;
    CVidMode* pVidMode = (pActiveEngine != NULL) ? pActiveEngine->pVidMode : NULL;

    int width = xMax - xMin + 1;
    if (width > 0) {
        WORD color16 = static_cast<WORD>(dwColor);
        unsigned short* pSurface16 = reinterpret_cast<unsigned short*>(pSurface) + (rSurface.Width() - xMin);

        for (int x = 0; x < width; x++) {
            WORD dst = *pSurface16;

            DWORD luminance =
                  (((pVidMode->m_dwBBitMask & dst) >> pVidMode->m_dwBBitShift) << pVidMode->field_CA)
                + (((pVidMode->m_dwGBitMask & dst) >> pVidMode->m_dwGBitShift) << pVidMode->field_C6) * 2
                + (((pVidMode->m_dwRBitMask & dst) >> pVidMode->m_dwRBitShift) << pVidMode->field_C2);
            DWORD scale = luminance >> 2;

            *pSurface16 = static_cast<unsigned short>(
                  (((((pVidMode->m_dwRBitMask & color16) >> pVidMode->m_dwRBitShift) * scale) >> 8) << pVidMode->m_dwRBitShift)
                | (((((pVidMode->m_dwGBitMask & color16) >> pVidMode->m_dwGBitShift) * scale) >> 8) << pVidMode->m_dwGBitShift)
                | (((((pVidMode->m_dwBBitMask & color16) >> pVidMode->m_dwBBitShift) * scale) >> 8) << pVidMode->m_dwBBitShift));

            pSurface16--;
        }
    }
}

// 0x7D6970
void CVidPoly::DrawHLine24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned char* pSurface8 = reinterpret_cast<unsigned char*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface8 += xMin * 3;

        for (int x = 0; x < width; x++) {
            *pSurface8++ = GetRValue(dwColor);
            *pSurface8++ = GetGValue(dwColor);
            *pSurface8++ = GetBValue(dwColor);
        }
    }
}

// 0x7D69B0
void CVidPoly::DrawHLineMirrored24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned char* pSurface8 = reinterpret_cast<unsigned char*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface8 += (rSurface.Width() - xMin) * 3;

        for (int x = 0; x < width; x++) {
            pSurface8[0] = GetRValue(dwColor);
            pSurface8[1] = GetGValue(dwColor);
            pSurface8[2] = GetBValue(dwColor);
            pSurface8 -= 3;
        }
    }
}

// 0x7D6A00
void CVidPoly::DrawHLineDithered24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned char* pSurface8 = reinterpret_cast<unsigned char*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface8 += xMin;

        if ((ptRef.y & 1) != 0) {
            if (((xMin + ptRef.x) & 1) != 0) {
                pSurface8 += 3;
                width--;
            }
        } else {
            if (((xMin + ptRef.x) & 1) == 0) {
                pSurface8 += 3;
                width--;
            }
        }

        width /= 2;
        for (int x = 0; x < width; x++) {
            pSurface8[0] = GetRValue(dwColor);
            pSurface8[1] = GetGValue(dwColor);
            pSurface8[2] = GetBValue(dwColor);
            pSurface8 += 6;
        }
    }
}

// 0x7D6A70
void CVidPoly::DrawHLineDitheredMirrored24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned char* pSurface8 = reinterpret_cast<unsigned char*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface8 += (rSurface.Width() - xMin) * 3;

        if ((ptRef.y & 1) != 0) {
            if (((rSurface.Width() + xMin + ptRef.x - 1) & 1) != 0) {
                pSurface8 -= 3;
                width--;
            }
        } else {
            if (((rSurface.Width() + xMin + ptRef.x - 1) & 1) == 0) {
                pSurface8 -= 3;
                width--;
            }
        }

        width /= 2;
        for (int x = 0; x < width; x++) {
            pSurface8[0] = GetRValue(dwColor);
            pSurface8[1] = GetGValue(dwColor);
            pSurface8[2] = GetBValue(dwColor);
            pSurface8 -= 6;
        }
    }
}

// 0x7D6AF0
void CVidPoly::DrawHLine32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned int* pSurface32 = reinterpret_cast<unsigned int*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface32 += xMin;

        for (int x = 0; x < width; x++) {
            *pSurface32++ = dwColor;
        }
    }
}

// 0x7D6B20
void CVidPoly::DrawHLineMirrored32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned int* pSurface32 = reinterpret_cast<unsigned int*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface32 += rSurface.Width() - xMin;

        for (int x = 0; x < width; x++) {
            *pSurface32-- = dwColor;
        }
    }
}

// 0x7D6B60
void CVidPoly::DrawHLineDithered32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned int* pSurface32 = reinterpret_cast<unsigned int*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface32 += xMin;

        if (g_pChitin->cVideo.Is3dAccelerated()) {
            for (int x = 0; x < width; x++) {
                unsigned int rgb = *pSurface32;
                *pSurface32++ = (rgb & 0xFFFFFF) | ((rgb >> 1) & 0x7F000000);
            }
        } else {
            if ((ptRef.y & 1) != 0) {
                if (((xMin + ptRef.x) & 1) != 0) {
                    pSurface32++;
                    width--;
                }
            } else {
                if (((xMin + ptRef.x) & 1) == 0) {
                    pSurface32++;
                    width--;
                }
            }

            width /= 2;
            for (int x = 0; x < width; x++) {
                *pSurface32 = dwColor;
                pSurface32 += 2;
            }
        }
    }
}

// 0x7D6BF0
void CVidPoly::DrawHLineDitheredMirrored32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    unsigned int* pSurface32 = reinterpret_cast<unsigned int*>(pSurface);

    int width = xMax - xMin + 1;
    if (width > 0) {
        pSurface32 += rSurface.Width() - xMin;

        if (g_pChitin->cVideo.Is3dAccelerated()) {
            for (int x = 0; x < width; x++) {
                unsigned int rgb = *pSurface32;
                *pSurface32-- = (rgb & 0xFFFFFF) | ((rgb >> 1) & 0x7F000000);
            }
        } else {
            if ((ptRef.y & 1) != 0) {
                if (((rSurface.Width() + xMin + ptRef.x - 1) & 1) != 0) {
                    pSurface32--;
                    width--;
                }
            } else {
                if (((rSurface.Width() + xMin + ptRef.x - 1) & 1) == 0) {
                    pSurface32--;
                    width--;
                }
            }

            width /= 2;
            for (int x = 0; x < width; x++) {
                *pSurface32 = dwColor;
                pSurface32 -= 2;
            }
        }
    }
}

// 0x7D6CA0
void CVidPoly::DrawHLineShaded24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    // TODO: Incomplete.
}

// 0x7D6DA0
void CVidPoly::DrawHLineShadedMirrored24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    // TODO: Incomplete.
}

// 0x7D6EB0
void CVidPoly::DrawHLineShaded32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    // TODO: Incomplete.
}

// 0x7D6FD0
void CVidPoly::DrawHLineShadedMirrored32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef)
{
    // TODO: Incomplete.
}

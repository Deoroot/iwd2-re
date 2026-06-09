#ifndef CVIDPOLY_H_
#define CVIDPOLY_H_

#include "mfc.h"

#define CVPOLY_MAX_VERTICIES 256

typedef struct {
    WORD x;
    WORD y;
} CVIDPOLY_VERTEX;

// A single polygon edge in the scanline fill's edge table / active edge table.
// Built by CVidPoly::BuildEdgeTable into the shared scratch buffer and stepped per
// scanline (DDA) by CVidPoly::AdvanceActiveEdges. 9 dwords / 0x24 bytes.
struct _EdgeDescription {
    _EdgeDescription* pNext;   // 0x00  singly-linked list (ET, then AET)
    INT nX;                    // 0x04  current X at the active scanline
    INT nYMin;                 // 0x08  topmost scanline of the edge (ET sort key)
    INT nXDir;                 // 0x0C  X step sign, +1 or -1
    INT nWholeStep;            // 0x10  whole-pixel X advance per scanline (signed)
    INT nErrTerm;              // 0x14  DDA error accumulator
    INT nErrAdjUp;             // 0x18  error increment per scanline
    INT nDy;                   // 0x1C  edge height (error reload)
    INT nCount;                // 0x20  scanlines remaining before the edge expires
};

class CVidPoly {
public:
    typedef void (CVidPoly::*DrawHLineFunc)(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);

    static const BYTE m_aDitherMask[];

    CVidPoly();

    static LONG CalculateLineVIntersection(const CPoint& lineStart, const CPoint& lineEnd, LONG vertical, const CPoint& linePrev);
    static BOOLEAN IsPtInPoly(const CPoint* pPoly, SHORT nPoly, const CPoint& pt);
    BOOL FillConvexPoly(WORD* pSurface, LONG lPitch, const CRect& rClip, DWORD dwColor, DWORD dwFlags, const CPoint& ptRef);
    BOOL FillPoly(WORD* pSurface, LONG lPitch, const CRect& rClip, DWORD dwColor, DWORD dwFlags, const CPoint& ptRef);
    BOOL FillConvexPoly3d(const CRect& rClip, DWORD dwColor, DWORD dwFlags, const CPoint& ptRef);
    void SetPoly(WORD* pVertices, WORD nVertices);
    void SetPoly(CVIDPOLY_VERTEX* pVertices, WORD nVertices);
    void DrawHLine16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineMirrored16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineDithered16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineDitheredMirrored16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void SetHLineFunction(DWORD dwFlags);
    void BuildEdgeTable(_EdgeDescription* pEdges);
    void AdvanceActiveEdges();

    void DrawHLineShaded16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineShadedMirrored16(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);

    void DrawHLine24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineMirrored24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineDithered24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineDitheredMirrored24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLine32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineMirrored32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineDithered32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineDitheredMirrored32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineShaded24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineShadedMirrored24(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineShaded32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);
    void DrawHLineShadedMirrored32(void* pSurface, int xMin, int xMax, DWORD dwColor, const CRect& rSurface, const CPoint& ptRef);

    int field_0;
    CVIDPOLY_VERTEX* m_pVertices;
    INT m_nVertices;
    _EdgeDescription* m_pET;    // edge table head (sorted by nYMin)
    _EdgeDescription* m_pAET;   // active edge table head (sorted by nX)
    DrawHLineFunc m_pDrawHLineFunction;
};

// One memoised edge table, keyed by the polygon's (vertex array, vertex count).
// The cached edges are a verbatim copy of the shared scratch buffer; m_pET points
// back into that fixed-address scratch, so a hit restores the bytes and the links
// resolve again. vtable 0x85EB30; the virtual dtor frees the edge array.
class CVidPolyEdgeCacheEntry : public CObject {
public:
    INT m_nCount;                  // 0x04  vertex count (cache key)
    _EdgeDescription* m_pEdges;    // 0x08  copy of the built edge table
    _EdgeDescription* m_pET;       // 0x0C  edge table head (into the scratch buffer)
    CVIDPOLY_VERTEX* m_pVertices;  // 0x10  vertex array pointer (cache key)

    CVidPolyEdgeCacheEntry();
    virtual ~CVidPolyEdgeCacheEntry();
};

// Global LRU cache of recently built edge tables (0xA09F20). Found entries are
// promoted to the head; the tail is evicted once the list exceeds 16 entries.
class CVidPolyEdgeCache : public CTypedPtrList<CPtrList, CVidPolyEdgeCacheEntry*> {
public:
    CCriticalSection m_cs;   // 0x1C

    BOOL FindCached(CVidPoly* pPoly);
    void Cache(CVidPoly* pPoly);
};

#endif /* CVIDPOLY_H_ */

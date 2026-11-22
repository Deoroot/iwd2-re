#ifndef CVISIBILITY_H_
#define CVISIBILITY_H_

#include "CInfinity.h"
#include "CVidMode.h"

class CGameArea;

#pragma pack(push, 2)
class CVisibilityMap {
public:
    static const BYTE EXPLORED_RANGE_0;
    static const BYTE EXPLORED_RANGE_1;
    static const BYTE EXPLORED_RANGE_2;
    static const BYTE EXPLORED_RANGE_3;
    static const BYTE EXPLORED_RANGE_4;
    static const BYTE EXPLORED_RANGE_5;
    static const BYTE EXPLORED_RANGE_6;

    static const BYTE SQUARE_SIZEX;
    static const BYTE SQUARE_SIZEY;

    static const BYTE EXPLORED;
    static const TILE_CODE EXPLORED_FULL;

    static const BYTE EXPLORED_FLAG;
    static const BYTE VISIBILITY_MASK;
    static const BYTE UNEXPLORED;

    static const BYTE UNEXPLORED_N;
    static const BYTE UNEXPLORED_NE;
    static const BYTE UNEXPLORED_E;
    static const BYTE UNEXPLORED_SE;
    static const BYTE UNEXPLORED_S;
    static const BYTE UNEXPLORED_SW;
    static const BYTE UNEXPLORED_W;
    static const BYTE UNEXPLORED_NW;
    static const TILE_CODE UNEXPLORED_FULL;

    static const BYTE FOGOWAR_EXPLORED_MASK;
    static const BYTE FOGOWAR_EXPLORED_SHIFT;
    static const BYTE FOGOWAR_MASK;
    static const BYTE FOGOWAR_SHIFT;

    static POLYCOLOR FOGOWARSHADE;
    static POLYCOLOR EXPLORESHADE;
    static POLYCOLOR CLEARSHADE;

    CVisibilityMap();
    ~CVisibilityMap();
    BOOL AddCharacter(const CPoint& pos, LONG charId, const BYTE* pVisibleTerrainTable);
    void UpdateVisibilityEllipses();
    void ClimbWall(const CPoint& ptStart, const CPoint& ptEnd, BYTE charId, const BYTE* pVisibileTerrainTable, SHORT nHighest);
    static void BltDitherPattern(LPDIRECTDRAWSURFACE pSurface, const TILE_CODE& tileCode);
    static void BltFogOWar(LPDIRECTDRAWSURFACE pSurface, const TILE_CODE& tileCode);
    BOOL IsCharacterIdOnMap(LONG charId);
    BYTE GetSubTileCode(LONG nTile);
    void GetTileCode(SHORT n64x64Tile, TILE_CODE& tileCode);
    void Init(CGameArea* pArea);
    void Marshal(BYTE** pData, DWORD* pnData);
    void Unmarshal(BYTE* pData, DWORD nData);
    void MarkTileLine(const CPoint& ptView, const CPoint& ptSearchMapView, LONG nDestX, LONG nDestY, const CPoint& ptOffset, BYTE charId, const BYTE* pVisibleTerrainTable, BOOLEAN bLookingUp);
    void PrivateAddCharacter(const CPoint& ptPos, BYTE visId, const BYTE* pVisibleTerrainTable);
    void RemoveCharacter(const CPoint& pos, LONG charId, const BYTE* pVisibleTerrainTable);
    void RemoveHorizontalOctantsStamp(const CPoint& pt, BYTE charId);
    void RemoveVerticalOctantsStamp(const CPoint& pt, BYTE charId);
    void SetAreaExplored();
    void SetAreaUnexplored();
    void StampHorizontalOctants(const CPoint& ptView, const CPoint& ptOffset, const CPoint& ptSearchMapView, BYTE charId, const BYTE* pVisibleTerrainTable);
    void StampVerticalOctants(const CPoint& ptView, const CPoint& ptOffset, const CPoint& ptSearchMapView, BYTE charId, const BYTE* pVisibleTerrainTable);
    void Uninit();
    void UpDate(const CPoint& ptOldPos, const CPoint& ptNewPos, LONG charId, const BYTE* pVisibleTerrainTable);
    BOOLEAN IsTileExplored(LONG nIndex);
    BOOLEAN IsTileVisible(LONG nIndex);
    void SetAreaVisible(BOOLEAN bVisible);

    static void BltFogOWar3d(INT x, INT y, const CRect& rDest, const TILE_CODE& tileCode);
    static void BltExploration3d(BYTE code, const CRect& rDest);
    static void BltVisibility3d(BYTE code, const CRect& rDest);

    SHORT PointToTile(const CPoint& pos);
    LONG GetBossTile(LONG n64x64Tile);
    BYTE GetCharacterPos(LONG charId);
    void SetTileExplored(LONG nIndex);
    void SetTileUnexplored(LONG nIndex);
    void SetTileVisible(LONG nIndex, BYTE charId);

    /* 0000 */ BYTE* m_pMap;
    /* 0004 */ BYTE m_nHorzArcCount;
    /* 0006 */ SHORT m_nWidth;
    /* 0008 */ SHORT m_nHeight;
    /* 000A */ LONG m_nMapSize;
    /* 000E */ short m_nSearchRangeH;
    /* 0010 */ short m_nSearchRangeV;
    /* 0012 */ BYTE* m_pHorzArcPixels;
    /* 0016 */ BYTE* m_pVertArcPixels;
    /* 001A */ BYTE m_nVertArcCount;
    /* 001B */ BOOLEAN m_bOutDoor;
    /* 001C */ CCriticalSection m_critSect;
    /* 003C */ CSearchBitmap* m_pSearchMap;
    /* 0040 */ LONG m_aCharacterIds[6];
    /* 0058 */ BYTE* m_pDynamicHeight;
};
#pragma pack(pop)

// CVisibilityMap::CVisibilityMap (0x551770): WORDs +0xE/+0x10 (0x5517D7,
// 0x5517DD), pointers +0x12/+0x16 (0x5517D1, 0x5517D4), BYTEs +0x1A/+0x1B
// (0x5517B9, 0x5517EC), critical section `lea edi,[esi+0x1c]` (0x55178B),
// +0x3C (0x5517CE), the id array `lea edi,[esi+0x40]` (0x5517C1).  Init
// (0x5527E0) stores width*height to +0xA (0x552847).  CGameArea zeroes
// m_visibility.m_pDynamicHeight at +0x860+0x58 = +0x8B8 (0x469EA0) and builds
// m_visibility at +0x860 (0x469C15) and the next member at +0x8BC (0x469C25):
// 0x5C bytes.  Offsets past the critical section are binary - 0x20 + sizeof()
// because a Debug MFC CCriticalSection is 0x24 (see CInfinity.h).
static_assert(offsetof(CVisibilityMap, m_nMapSize) == 0x0A, "CVisibilityMap::m_nMapSize");
static_assert(offsetof(CVisibilityMap, m_nSearchRangeH) == 0x0E, "CVisibilityMap::m_nSearchRangeH");
static_assert(offsetof(CVisibilityMap, m_pHorzArcPixels) == 0x12, "CVisibilityMap::m_pHorzArcPixels");
static_assert(offsetof(CVisibilityMap, m_pVertArcPixels) == 0x16, "CVisibilityMap::m_pVertArcPixels");
static_assert(offsetof(CVisibilityMap, m_nVertArcCount) == 0x1A, "CVisibilityMap::m_nVertArcCount");
static_assert(offsetof(CVisibilityMap, m_critSect) == 0x1C, "CVisibilityMap::m_critSect");
static_assert(offsetof(CVisibilityMap, m_pSearchMap) == 0x3C - 0x20 + sizeof(CCriticalSection), "CVisibilityMap::m_pSearchMap");
static_assert(offsetof(CVisibilityMap, m_aCharacterIds) == 0x40 - 0x20 + sizeof(CCriticalSection), "CVisibilityMap::m_aCharacterIds");
static_assert(offsetof(CVisibilityMap, m_pDynamicHeight) == 0x58 - 0x20 + sizeof(CCriticalSection), "CVisibilityMap::m_pDynamicHeight");
static_assert(sizeof(CVisibilityMap) == 0x5C - 0x20 + sizeof(CCriticalSection), "CGameArea: m_visibility 0x860..0x8BC");

#endif /* CVISIBILITY_H_ */

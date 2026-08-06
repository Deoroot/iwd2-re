#include "CTiledObject.h"

#include "CInfinity.h"
#include "CResWED.h"
#include "CUtil.h"

// 0x84EC08
const WORD CTiledObject::STATE_PRIMARY_TILE = 1;

// 0x84EC0A
const WORD CTiledObject::STATE_SECONDARY_TILE = 2;

// Bit 1 of WED_POLYLIST::nType takes the polygon out of the wall/cover set. A
// tiled object's two polygon lists are mutually exclusive: the list matching the
// rendered state is enabled and the other one is disabled.
static const BYTE POLY_DISABLED = 0x02;

// NOTE: Inlined.
//
// The polygon offsets in WED_TILEDOBJECT are relative to the start of the WED
// file, which is what CResWED::m_pHeader points at.
static void SetTiledObjectPolys(CResWED* pResWed, DWORD nOffset, WORD nPolys, BOOL bDisable)
{
    if (nPolys == 0xFFFF || nPolys == 0) {
        return;
    }

    WED_POLYLIST* pPolys = reinterpret_cast<WED_POLYLIST*>(
        reinterpret_cast<unsigned char*>(pResWed->m_pHeader) + nOffset);

    for (WORD cnt = 0; cnt < nPolys; cnt++) {
        if (bDisable) {
            pPolys[cnt].nType |= POLY_DISABLED;
        } else {
            pPolys[cnt].nType &= static_cast<BYTE>(~POLY_DISABLED);
        }
    }
}

// NOTE: Inlined.
CTiledObject::CTiledObject()
{
    m_posAreaList = NULL;
    m_pResWed = NULL;
    m_nWedIndex = 0;
    m_wAIState = 0;
    m_wRenderState = 0;
}

// 0x54E5B0
void CTiledObject::CheckTileState(CInfinity& cInfinity)
{
    WORD wAIState = m_wAIState;

    // __FILE__: C:\Projects\Icewind2\src\Baldur\CTiledObject.cpp
    // __LINE__: 84
    UTIL_ASSERT(m_pResWed != NULL);

    if (wAIState == m_wRenderState || m_nWedIndex == -1) {
        return;
    }

    m_wRenderState = wAIState;

    WORD* pObjectTileList = m_pResWed->GetTiledObjectList();
    WORD* pTileList = m_pResWed->GetTileList(0);

    if (pObjectTileList == NULL) {
        // __FILE__: C:\Projects\Icewind2\src\Baldur\CTiledObject.cpp
        // __LINE__: 99
        UTIL_ASSERT(FALSE);
        return;
    }

    WED_TILEDOBJECT* pObject = m_pResWed->GetTiledObject(static_cast<USHORT>(m_nWedIndex));

    WORD nFirstTile = pObject->nStartingTile;
    WORD nLastTile = static_cast<WORD>(pObject->nStartingTile + pObject->nNumTiles);

    if (m_wRenderState == STATE_PRIMARY_TILE) {
        for (WORD cnt = nFirstTile; cnt < nLastTile; cnt++) {
            WED_TILEDATA* pTileData = m_pResWed->GetTileData(0, pObjectTileList[cnt]);
            pTileData->wFlags = m_wRenderState;

            cInfinity.SwapVRamTiles(pTileData->nSecondary,
                pTileList[pTileData->nStartingTile]);
        }

        SetTiledObjectPolys(m_pResWed,
            pObject->nOffsetToPrimaryPolys,
            pObject->nNumPrimaryPolys,
            FALSE);
        SetTiledObjectPolys(m_pResWed,
            pObject->nOffsetToSecondaryPolys,
            pObject->nNumSecondaryPolys,
            TRUE);
    } else if (m_wRenderState == STATE_SECONDARY_TILE) {
        for (WORD cnt = nFirstTile; cnt < nLastTile; cnt++) {
            WED_TILEDATA* pTileData = m_pResWed->GetTileData(0, pObjectTileList[cnt]);
            pTileData->wFlags = m_wRenderState;

            cInfinity.SwapVRamTiles(pTileList[pTileData->nStartingTile],
                pTileData->nSecondary);
        }

        SetTiledObjectPolys(m_pResWed,
            pObject->nOffsetToPrimaryPolys,
            pObject->nNumPrimaryPolys,
            TRUE);
        SetTiledObjectPolys(m_pResWed,
            pObject->nOffsetToSecondaryPolys,
            pObject->nNumSecondaryPolys,
            FALSE);
    } else {
        // __FILE__: C:\Projects\Icewind2\src\Baldur\CTiledObject.cpp
        // __LINE__: 130
        UTIL_ASSERT(FALSE);
    }
}

// 0x54E830
void CTiledObject::Initialize(CResWED* pResWed, CResRef resID, POSITION posAreaList, WORD wInitialState)
{
    CResRef cRes;

    m_pResWed = pResWed;
    m_posAreaList = posAreaList;

    WED_TILEDOBJECT* pObject = m_pResWed->GetTiledObjects();
    ULONG nObjects = m_pResWed->GetNumTiledObjects();

    WORD cnt = 0;
    if (nObjects != 0) {
        do {
            cRes = pObject->resID;
            if (cRes == resID) {
                m_nWedIndex = cnt;
                break;
            }

            pObject++;
            cnt++;
        } while (cnt < nObjects);
    }

    m_resId = resID;

    if (cnt == nObjects) {
        // __FILE__: C:\Projects\Icewind2\src\Baldur\CTiledObject.cpp
        // __LINE__: 186
        UTIL_ASSERT_MSG(FALSE, "Tiled object resref not found");
        m_nWedIndex = -1;
    }

    m_wAIState = wInitialState;
    m_wRenderState = STATE_PRIMARY_TILE;

    if (m_nWedIndex != -1) {
        SetTiledObjectPolys(m_pResWed,
            pObject->nOffsetToPrimaryPolys,
            pObject->nNumPrimaryPolys,
            FALSE);
        SetTiledObjectPolys(m_pResWed,
            pObject->nOffsetToSecondaryPolys,
            pObject->nNumSecondaryPolys,
            TRUE);
    }
}

// 0x54E980
void CTiledObject::SetNewResWED(CResWED* pNewResWED)
{
    CResRef cRes;

    m_pResWed = pNewResWED;

    WED_TILEDOBJECT* pObject = m_pResWed->GetTiledObjects();
    ULONG nObjects = m_pResWed->GetNumTiledObjects();

    WORD cnt = 0;
    if (nObjects != 0) {
        do {
            cRes = pObject->resID;
            if (cRes == m_resId) {
                m_nWedIndex = cnt;
                break;
            }

            pObject++;
            cnt++;
        } while (cnt < nObjects);
    }

    if (cnt == nObjects) {
        // __FILE__: C:\Projects\Icewind2\src\Baldur\CTiledObject.cpp
        // __LINE__: 251
        UTIL_ASSERT_MSG(FALSE, "Tiled object resref not found");
        m_nWedIndex = -1;
    }

    m_wRenderState = STATE_PRIMARY_TILE;

    if (m_nWedIndex != -1) {
        SetTiledObjectPolys(m_pResWed,
            pObject->nOffsetToPrimaryPolys,
            pObject->nNumPrimaryPolys,
            FALSE);
        SetTiledObjectPolys(m_pResWed,
            pObject->nOffsetToSecondaryPolys,
            pObject->nNumSecondaryPolys,
            TRUE);
    }
}

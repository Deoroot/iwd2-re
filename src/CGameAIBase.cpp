#include "CGameAIBase.h"

#include "C2DArray.h"
#include "CAIConditionResponse.h"
#include "CAIResponse.h"
#include "CAIUtil.h"
#include "CAIScript.h"
#include "CAITrigger.h"
#include "CBaldurChitin.h"
#include "CBaldurProjector.h"
#include "CGameArea.h"
#include "CGameContainer.h"
#include "CGameDoor.h"
#include "CGameEffect.h"
#include "CGameJournal.h"
#include "CGameSpawning.h"
#include "CGameSound.h"
#include "CGameSprite.h"
#include "CGameStatic.h"
#include "CGameTiledObject.h"
#include "CGameTimer.h"
#include "CGameTrigger.h"
#include "CInfGame.h"
#include "CPathSearch.h"
#include "CProjectile.h"
#include "CScreenCharacter.h"
#include "CScreenChapter.h"
#include "CScreenInventory.h"
#include "CScreenWorld.h"
#include "CSpell.h"
#include "CTimerWorld.h"
#include "CUtil.h"
#include "CVariableHash.h"
#include "FileFormat.h"
#include "Icewind586B70.h"

// 0x8485C4
const SHORT CGameAIBase::ACTION_DONE = -1;

// 0x8485C6
const SHORT CGameAIBase::ACTION_INTERRUPTABLE = 1;

// 0x8485C8
const SHORT CGameAIBase::ACTION_NORMAL = 0;

// 0x8485CA
const SHORT CGameAIBase::ACTION_ERROR = -2;

// 0x8485CC
const SHORT CGameAIBase::ACTION_NO_ACTION = 2;

// 0x8485CE
const SHORT CGameAIBase::ACTION_STOPPED = -3;

// 0x8485E8
const BYTE CGameAIBase::EFFECT_LIST_TIMED = 1;

// 0x8485E9
const BYTE CGameAIBase::EFFECT_LIST_EQUIPED = 2;

// 0x8D1408
const CString CGameAIBase::DEAD_GLOBAL_PREFIX("_DEAD");

// 0x8D1810
CAIAction CGameAIBase::m_aiAction;

static void SplitScriptVariableName(const CString& sCombined, CString& sScope, CString& sName)
{
    // Ghidra 0x453840/0x45EDE0: script parser stores scope+name in string1.
    // The binary treats the first six chars as scope (GLOBAL/LOCALS/MYAREA/area resref).
    sScope = sCombined.Left(6);
    int nNameLength = sCombined.GetLength() - 6;
    sName = nNameLength > 0 ? sCombined.Right(nNameLength) : CString("");
}

// 0x44C4B0
CGameAIBase::CGameAIBase()
{
    m_nLastActionReturn = 0;
    m_objectType = CGameObject::TYPE_AIBASE;
    m_lAttacker.Set(CAIObjectType::NOONE);
    m_lOrderedBy.Set(CAIObjectType::NOONE);
    field_EA.Set(CAIObjectType::NOONE);
    field_126.Set(CAIObjectType::NOONE);
    field_162.Set(CAIObjectType::NOONE);
    m_lHitter.Set(CAIObjectType::NOONE);
    m_lHelp.Set(CAIObjectType::NOONE);
    m_lTrigger.Set(CAIObjectType::NOONE);
    m_lSeen.Set(CAIObjectType::NOONE);
    m_lTalkedTo.Set(CAIObjectType::NOONE);
    m_lHeard.Set(CAIObjectType::NOONE);
    field_306.Set(CAIObjectType::NOONE);
    field_342.Set(CAIObjectType::NOONE);
    field_37E.Set(CAIObjectType::NOONE);
    field_3BA.Set(CAIObjectType::NOONE);
    m_curResponseNum = -1;
    m_curResponseSetNum = -1;
    m_curScriptNum = -1;
    m_curAction = CAIAction::NULL_ACTION;
    m_interrupt = FALSE;
    m_actionCount = 0;
    field_54C = 0;
    field_550 = 0;
    field_552 = 0;
    field_44A = 0;
    m_overrideScript = NULL;
    m_special1Script = NULL;
    m_teamScript = NULL;
    m_special2Script = NULL;
    m_combatScript = NULL;
    m_special3Script = NULL;
    m_movementScript = NULL;
    m_inCutScene = FALSE;
    m_reactionRoll = 10;
    field_550 = rand() % 120;

    CAITrigger trigger(CAITrigger::ONCREATION, 0);
    m_pendingTriggers.AddTail(new CAITrigger(trigger));

    m_firstCall = TRUE;
    m_forceActionPick = FALSE;
    field_580 = 0;
    m_nLastActionReturn = -1;
    field_588 = 0;
    field_58C = 0;
    field_594 = 0;
    field_595 = 1;
    field_596 = 0;
    m_randValue = rand() & 0x7FFF;
}

// 0x44D160
CGameAIBase::~CGameAIBase()
{
    POSITION pos;

    pos = m_pendingTriggers.GetHeadPosition();
    while (pos != NULL) {
        CAITrigger* pTrigger = m_pendingTriggers.GetNext(pos);
        if (pTrigger != NULL) {
            delete pTrigger;
        }
    }

    pos = m_queuedActions.GetHeadPosition();
    while (pos != NULL) {
        CAIAction* pAction = m_queuedActions.GetNext(pos);
        if (pAction != NULL) {
            delete pAction;
        }
    }

    pos = m_timers.GetHeadPosition();
    while (pos != NULL) {
        CGameTimer* pTimer = m_timers.GetNext(pos);
        if (pTimer != NULL) {
            delete pTimer;
        }
    }

    if (m_overrideScript != NULL) {
        delete m_overrideScript;
        m_overrideScript = NULL;
    }

    if (m_special1Script != NULL) {
        delete m_special1Script;
        m_special1Script = NULL;
    }

    if (m_teamScript != NULL) {
        delete m_teamScript;
        m_teamScript = NULL;
    }

    if (m_special2Script != NULL) {
        delete m_special2Script;
        m_special2Script = NULL;
    }

    if (m_combatScript != NULL) {
        delete m_combatScript;
        m_combatScript = NULL;
    }

    if (m_special3Script != NULL) {
        delete m_special3Script;
        m_special3Script = NULL;
    }

    if (m_movementScript != NULL) {
        delete m_movementScript;
        m_movementScript = NULL;
    }
}

// 0x44D100
const BYTE* CGameAIBase::GetVisibleTerrainTable()
{
    return CGameObject::DEFAULT_VISIBLE_TERRAIN_TABLE;
}

// 0x44D110
const BYTE* CGameAIBase::GetTerrainTable()
{
    return CGameObject::DEFAULT_TERRAIN_TABLE;
}

// 0x44CBC0
// HACK: the binary inherits CGameObject::CanSaveGame here (slot 0x28 == 0x44CBC0,
// which sets strError=16502 and returns FALSE). But CGameDoor/CGameContainer/
// HACK: CGameAIBase does not override CanSaveGame at all -- its vtable slot
// 0x0028 holds 0x44CBC0, i.e. the inherited CGameObject::CanSaveGame
// (strError = 16502, return FALSE). We return TRUE instead, because
// CGameArea::CanSaveGame walks the area's vert-sort lists and blocks the save
// if ANY object answers FALSE, so a faithful FALSE here blocks all saving.
// -- replaces 0x44CBC0
//
// Blocker, narrowed 2026-07-31: of the seven CGameAIBase subclasses, five
// override slot 0x0028 to 0x47C830 (strError = -1, return TRUE) and
// CGameSprite has its own at 0x75E890. CGameContainer, CGameDoor and
// CGameTrigger were the three still missing and are now declared, so the only
// subclass left inheriting the FALSE is CGameTiledObject. Its vtable
// (0x84C700) resolves from a single anchor, which is too weak to bet saving
// on -- confirm that table, then delete this override and let the base answer.
BOOLEAN CGameAIBase::CanSaveGame(STRREF& strError)
{
    strError = -1;
    return TRUE;
}

// 0x44D120
BOOLEAN CGameAIBase::CompressTime(DWORD deltaTime)
{
    CheckTimers(deltaTime / CTimerWorld::TIMESCALE_MSEC_PER_SEC);
    return TRUE;
}

// 0x453840
BOOL CGameAIBase::EvaluateStatusTrigger(const CAITrigger& trigger)
{
    switch (trigger.m_triggerID) {
    case CAITRIGGER_TRUE:
        return TRUE;

    // 0x45AB39 is a bare `xor esi, esi`, and the table sends BOTH of these to
    // it: `HasInnateAbility` is not merely unrecovered, it is hard-wired FALSE
    // in IWD2's engine.
    case CAITRIGGER_FALSE:
    case CAITRIGGER_HASINNATEABILITY:
        return FALSE;

    case CAITRIGGER_GLOBAL:
    case CAITRIGGER_GLOBALGT:
    case CAITRIGGER_GLOBALLT: {
        CString sScope;
        CString sName;
        SplitScriptVariableName(trigger.GetString1(), sScope, sName);
        LONG nTriggerValue = trigger.GetSpecifics();
        CVariableHash* pHash = NULL;

        if (sScope == CString("GLOBAL")) {
            pHash = g_pBaldurChitin->GetObjectGame()->GetVariables();
        } else if (sScope == CString("LOCALS")) {
            if ((GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                pHash = static_cast<CGameSprite*>(this)->GetLocalVariables();
            }
        } else {
            CString sAreaName = sScope;
            if (sScope == CString("MYAREA") && m_pArea != NULL) {
                sAreaName = CResRef(reinterpret_cast<BYTE*>(m_pArea->m_header.m_areaName)).GetResRefStr();
            }
            CGameArea* pArea = g_pBaldurChitin->GetObjectGame()->GetArea(sAreaName);
            if (pArea != NULL) {
                pHash = pArea->GetVariables();
            }
        }

        CVariable* pVar = pHash != NULL ? pHash->FindKey(sName) : NULL;
        LONG nValue = pVar != NULL ? pVar->m_intValue : 0;

        if (trigger.m_triggerID == CAITRIGGER_GLOBAL) {
            return nValue == nTriggerValue;
        }
        if (trigger.m_triggerID == CAITRIGGER_GLOBALGT) {
            return nValue > nTriggerValue;
        }
        return nValue < nTriggerValue;
    }

    case CAITRIGGER_ENTIREPARTYONMAP: {
        CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
        SHORT nCharacters = pGame->GetNumCharacters();
        if (nCharacters <= 0) {
            return FALSE;
        }

        for (SHORT nPortrait = 0; nPortrait < nCharacters; ++nPortrait) {
            LONG nCharacterId = pGame->GetCharacterId(nPortrait);
            CGameSprite* pSprite = NULL;
            BYTE rc = pGame->GetObjectArray()->GetShare(
                nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
            if (rc != CGameObjectArray::SUCCESS || pSprite == NULL) {
                return FALSE;
            }

            BOOL bDead = (pSprite->m_derivedStats.m_generalState & STATE_DEAD) != 0;
            BOOL bOnMap = bDead || pSprite->GetArea() == m_pArea;
            pGame->GetObjectArray()->ReleaseShare(
                nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);

            if (!bOnMap) {
                return FALSE;
            }
        }

        return TRUE;
    }

    case CAITRIGGER_INCUTSCENEMODE:
        return g_pBaldurChitin->GetObjectGame()->GetGameSave()->m_mode == 322;

    case CAITRIGGER_ACTIONLISTEMPTY:
        // 0x455356: idle check used by the waypoint-patrol scripts (10ELDGUM
        // and friends) to wait for the previous MoveToPoint to finish.
        return m_queuedActions.GetCount() == 0
            && m_curAction.GetActionID() == CAIAction::NO_ACTION;

    case CAITRIGGER_NEARLOCATION: {
        // 0x458501: NearLocation(O:Object*,I:PointX,I:PointY,I:Range) --
        // squared search-grid distance of the object to (x,y) vs Range^2.
        // x/y of -1 resolve to the caller's position, -2 to its saved location.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectWithType(this, CGameObject::TYPE_AIBASE, FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        LONG x = cause.GetSpecifics();
        LONG y = cause.GetInt1();
        if (x == -1) {
            x = GetPos().x;
        } else if (x == -2 && (GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
            x = static_cast<CGameSprite*>(this)->GetBaseStats()->m_savedLocationX;
        }
        if (y == -1) {
            y = GetPos().y;
        } else if (y == -2 && (GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
            y = static_cast<CGameSprite*>(this)->GetBaseStats()->m_savedLocationY;
        }

        LONG nRange = cause.GetInt2();
        BOOL bHolds = FALSE;
        if (x > 0 && y > 0 && nRange > 0) {
            CPoint posObject = pObject->GetPos();
            LONG dx = (posObject.x - x) / CPathSearch::GRID_SQUARE_SIZEX;
            LONG dy = (posObject.y - y) / CPathSearch::GRID_SQUARE_SIZEY;
            bHolds = dx * dx + dy * dy <= nRange * nRange;
        }

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_RANGE: {
        // 0x454851: Range(O:Object*,I:Range*,I:diffmode) -- squared search-grid
        // distance to the object vs (range+1)^2, compared per diffmode.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectWithType(this, CGameObject::TYPE_AIBASE, FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        // DEFERRED: doors (TYPE 0x21) use the door-center helper 0x48B2C0; the
        // plain position is close enough until that helper is recovered.
        CPoint ptTarget = pObject->GetPos();
        CPoint gridTarget(ptTarget.x / CPathSearch::GRID_SQUARE_SIZEX,
            ptTarget.y / CPathSearch::GRID_SQUARE_SIZEY);

        CPoint ptSelf = GetPos();
        CPoint gridSelf(ptSelf.x / CPathSearch::GRID_SQUARE_SIZEX,
            ptSelf.y / CPathSearch::GRID_SQUARE_SIZEY);

        LONG nDistSq = (gridTarget.x - gridSelf.x) * (gridTarget.x - gridSelf.x)
            + (gridTarget.y - gridSelf.y) * (gridTarget.y - gridSelf.y);
        LONG nRangeSq = (cause.GetSpecifics() + 1) * (cause.GetSpecifics() + 1);

        BOOL bHolds;
        switch (cause.GetInt1()) {
        case 1: // EQUAL
            bHolds = nDistSq == nRangeSq;
            break;
        case 2: // LESS_THAN
            bHolds = nDistSq < nRangeSq;
            break;
        case 3: // GREATER_THAN
            bHolds = nRangeSq < nDistSq;
            break;
        default:
            bHolds = FALSE;
            break;
        }

        if (bHolds) {
            field_342.Set(pObject->GetAIType());
        }

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_ALLEGIANCE: {
        // 0x453C35: Allegiance(O:Object*,I:Allegiance*EA).  Compares only the
        // decoded cause's EA byte -- no object is resolved and no share taken.
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);
        return cause.m_triggerCause.m_nEnemyAlly == cause.m_specificID;
    }

    case CAITRIGGER_EXISTS: {
        // 0x453C9C: Exists(O:Object*) -- true if the cause resolves to a live
        // object.  Decodes and resolves through m_triggerCause directly.
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);

        CGameObject* pObject = cause.m_triggerCause.GetObjectFromId(this, TRUE);
        BOOL bHolds = pObject != NULL;

        if (pObject != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
        return bHolds;
    }

    case CAITRIGGER_DEAD: {
        // 0x4566A6: Dead(O:Object*) -- true when the object is gone, is not a
        // sprite, or carries STATE_DEAD in EITHER stat block.  Only a live
        // sprite makes this false.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectFromId(this, TRUE);
        BOOL bHolds = TRUE;

        if (pObject != NULL) {
            if (pObject->GetObjectType() == CGameObject::TYPE_SPRITE) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObject);
                if ((pSprite->GetDerivedStats()->m_generalState & STATE_DEAD) == 0
                    && (pSprite->GetBaseStats()->m_generalState & STATE_DEAD) == 0) {
                    bHolds = FALSE;
                }
            }

            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
        return bHolds;
    }

    case CAITRIGGER_ISACTIVE: {
        // 0x45A534: IsActive(O:Object*) -- three different "active" notions
        // depending on what the cause resolved to.  Anything else is FALSE.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObject(this, FALSE);
        BOOL bHolds = FALSE;

        if (pObject != NULL) {
            BYTE nType = pObject->GetObjectType();
            if (nType == CGameObject::TYPE_SOUND) {
                bHolds = static_cast<CGameSound*>(pObject)->IsActive();
            } else if (nType == CGameObject::TYPE_SPRITE) {
                bHolds = static_cast<CGameSprite*>(pObject)->GetActive();
            } else if (nType == CGameObject::TYPE_TRIGGER) {
                bHolds = static_cast<CGameTrigger*>(pObject)->IsTrapActive();
            }

            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
        return bHolds;
    }

    case CAITRIGGER_CREATUREHIDDEN: {
        // 0x4582E0: CreatureHidden(O:Object*) -- the target's own stealth flag.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = pSprite->GetBaseStats()->m_bStealthMode;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HPGT: {
        // 0x45435E, tail shared at 0x4540F0: base (not derived) hit points
        // strictly greater than the trigger's value.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = pSprite->GetBaseStats()->m_hitPoints > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HP: {
        // 0x45431C: base hit points exactly equal to the trigger's value.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = pSprite->GetBaseStats()->m_hitPoints == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HPLT: {
        // 0x454390, tail shared at 0x4543BD with HPPercentLT: base hit points
        // strictly less than the trigger's value.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = pSprite->GetBaseStats()->m_hitPoints < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HPPERCENT: {
        // 0x4543D4: base hit points as a whole percent of the DERIVED maximum.
        // The binary scales by 100 before the divide (lea/lea/shl at 0x454405),
        // so this truncates once, not twice.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nPercent = 100 * pSprite->GetBaseStats()->m_hitPoints
            / pSprite->GetDerivedStats()->m_nMaxHitPoints;
        BOOL bHolds = nPercent == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HPPERCENTLT: {
        // 0x454433, joining HPLT's tail at 0x4543BD.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nPercent = 100 * pSprite->GetBaseStats()->m_hitPoints
            / pSprite->GetDerivedStats()->m_nMaxHitPoints;
        BOOL bHolds = nPercent < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HPPERCENTGT: {
        // 0x454482.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nPercent = 100 * pSprite->GetBaseStats()->m_hitPoints
            / pSprite->GetDerivedStats()->m_nMaxHitPoints;
        BOOL bHolds = nPercent > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HPLOST: {
        // 0x4544E3: derived maximum minus BASE current, i.e. damage taken.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLost = pSprite->GetDerivedStats()->m_nMaxHitPoints
            - pSprite->GetBaseStats()->m_hitPoints;
        BOOL bHolds = nLost == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HPLOSTGT: {
        // 0x454534.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLost = pSprite->GetDerivedStats()->m_nMaxHitPoints
            - pSprite->GetBaseStats()->m_hitPoints;
        BOOL bHolds = nLost > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HPLOSTLT: {
        // 0x454587, tail shared at 0x4551AB.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLost = pSprite->GetDerivedStats()->m_nMaxHitPoints
            - pSprite->GetBaseStats()->m_hitPoints;
        BOOL bHolds = nLost < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_ISCREATUREAREAFLAG: {
        // 0x4545C6: mask test against the base stats' creature flags.  Note
        // the operand order -- the binary reads GetSpecifics BEFORE the stats
        // block (0x4545E8 then 0x4545F3).
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = (cause.GetSpecifics() & pSprite->GetBaseStats()->m_flags) != 0;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_CHECKSTAT: {
        // 0x455402: CheckStat(O:Object*,I:Value,I:StatID*Stats) -- an arbitrary
        // derived stat addressed by STATS.IDS offset.  GetInt1 carries the
        // stat id and is evaluated BEFORE GetDerivedStats -- the two calls
        // sit at 0x455428 and 0x455432, i.e. argument-then-object order.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nStat = pSprite->GetDerivedStats()->GetAtOffset(static_cast<SHORT>(cause.GetInt1()));
        BOOL bHolds = nStat == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_CHECKSTATGT: {
        // 0x455455, tail shared at 0x459A21 by every GT arm of this family.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nStat = pSprite->GetDerivedStats()->GetAtOffset(static_cast<SHORT>(cause.GetInt1()));
        BOOL bHolds = nStat > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_CHECKSTATLT: {
        // 0x455498, tail shared at 0x456AA3 by every LT arm of this family.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nStat = pSprite->GetDerivedStats()->GetAtOffset(static_cast<SHORT>(cause.GetInt1()));
        BOOL bHolds = nStat < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_CHECKSKILL: {
        // 0x455EDD: a direct index into the derived skill array rather than a
        // stat offset.  The load is `movsx` (0x455F13), so the skill reads
        // SIGNED -- m_nSkills is declared BYTE but the engine stores it as a
        // signed char, which CDerivedStats::Clamp already assumes.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nSkill = static_cast<CHAR>(pSprite->GetDerivedStats()->m_nSkills[cause.GetInt1()]);
        BOOL bHolds = nSkill == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_CHECKSKILLGT: {
        // 0x455F32.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nSkill = static_cast<CHAR>(pSprite->GetDerivedStats()->m_nSkills[cause.GetInt1()]);
        BOOL bHolds = nSkill > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_CHECKSKILLLT: {
        // 0x455F75, joining the LT tail one instruction later than the others
        // (0x456AA5) because the value is already in place.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nSkill = static_cast<CHAR>(pSprite->GetDerivedStats()->m_nSkills[cause.GetInt1()]);
        BOOL bHolds = nSkill < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_LEVELINCLASS: {
        // 0x455FB8: LevelInClass(O:Object*,I:Level,I:Class*Class) -- the level
        // in ONE class, GetInt1 naming it.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLevel = pSprite->GetDerivedStats()->GetClassLevel(cause.GetInt1());
        BOOL bHolds = nLevel == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_LEVELINCLASSGT: {
        // 0x45600B.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLevel = pSprite->GetDerivedStats()->GetClassLevel(cause.GetInt1());
        BOOL bHolds = nLevel > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_LEVELINCLASSLT: {
        // 0x45604E.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLevel = pSprite->GetDerivedStats()->GetClassLevel(cause.GetInt1());
        BOOL bHolds = nLevel < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_LEVEL: {
        // 0x457A98: the TOTAL character level -- GetClassMaskLevel over every
        // class bit (0xFFF, pushed at 0x457ABC), not GetClassLevel.  This arm
        // ignores GetInt1 entirely.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLevel = pSprite->GetDerivedStats()->GetClassMaskLevel(0xFFF);
        BOOL bHolds = nLevel == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_LEVELGT: {
        // 0x457AE4.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLevel = pSprite->GetDerivedStats()->GetClassMaskLevel(0xFFF);
        BOOL bHolds = nLevel > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_LEVELLT: {
        // 0x457B20.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nLevel = pSprite->GetDerivedStats()->GetClassMaskLevel(0xFFF);
        BOOL bHolds = nLevel < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_MORALE: {
        // 0x45475E: morale is a BYTE in the base stats, and the binary
        // compares BYTES -- it truncates the trigger's value into BL and
        // issues `cmp cl, bl` (0x45479C).  A trigger value above 255 therefore
        // wraps here rather than never matching.  GetSpecifics is called
        // BEFORE the stats block, which is why the value is a local.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BYTE nMorale = static_cast<BYTE>(cause.GetSpecifics());
        BOOL bHolds = pSprite->GetBaseStats()->m_morale == nMorale;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_MORALEGT: {
        // 0x4547A8: `cmp bl, [eax+0x264]` then sbb/neg, i.e. an UNSIGNED byte
        // comparison, not the signed one a LONG compare would give.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BYTE nMorale = static_cast<BYTE>(cause.GetSpecifics());
        BOOL bHolds = pSprite->GetBaseStats()->m_morale > nMorale;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_MORALELT: {
        // 0x4547ED: the same compare with the operands the other way round.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BYTE nMorale = static_cast<BYTE>(cause.GetSpecifics());
        BOOL bHolds = pSprite->GetBaseStats()->m_morale < nMorale;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HAPPINESS: {
        // 0x456754: unlike morale this goes through CGameSprite::GetHappiness,
        // which returns a SHORT the binary sign-extends (movsx at 0x456781)
        // before a full 32-bit compare.  The call precedes GetSpecifics.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nHappiness = pSprite->GetHappiness();
        BOOL bHolds = nHappiness == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HAPPINESSGT: {
        // 0x456795, tail at 0x45683F.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nHappiness = pSprite->GetHappiness();
        BOOL bHolds = nHappiness > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HAPPINESSLT: {
        // 0x4567C0.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nHappiness = pSprite->GetHappiness();
        BOOL bHolds = nHappiness < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_REPUTATION: {
        // 0x45497D: a party member answers with the PARTY reputation, anyone
        // else with the creature's own byte at base stats +0x3C.  The game
        // object is fetched twice, once per branch.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nReputation;
        if (g_pBaldurChitin->GetObjectGame()->GetCharacterPortraitNum(pSprite->GetId()) != -1) {
            nReputation = g_pBaldurChitin->GetObjectGame()->GetReputation();
        } else {
            nReputation = pSprite->GetBaseStats()->m_reputation;
        }

        BOOL bHolds = nReputation == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_REPUTATIONGT: {
        // 0x4549FD, sharing HPGT's tail at 0x4540F0 on the party branch.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nReputation;
        if (g_pBaldurChitin->GetObjectGame()->GetCharacterPortraitNum(pSprite->GetId()) != -1) {
            nReputation = g_pBaldurChitin->GetObjectGame()->GetReputation();
        } else {
            nReputation = pSprite->GetBaseStats()->m_reputation;
        }

        BOOL bHolds = nReputation > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_REPUTATIONLT: {
        // 0x454A6E.  Unlike its siblings this arm inlines both comparisons,
        // one per branch (0x454AD2 and 0x454AF7), instead of joining a tail.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nReputation;
        if (g_pBaldurChitin->GetObjectGame()->GetCharacterPortraitNum(pSprite->GetId()) != -1) {
            nReputation = g_pBaldurChitin->GetObjectGame()->GetReputation();
        } else {
            nReputation = pSprite->GetBaseStats()->m_reputation;
        }

        BOOL bHolds = nReputation < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_PARTYGOLD:
        // 0x456626: the purse in the save block.  It is an UNSIGNED DWORD, and
        // the ordered arms are sbb/neg pairs (0x456674, 0x45669D), so all
        // three comparisons are unsigned -- which the natural DWORD-vs-LONG
        // expression already gives.  No sprite is resolved here, so nothing is
        // shared and nothing is released.
        return g_pBaldurChitin->GetObjectGame()->GetGameSave()->m_nPartyGold
            == static_cast<DWORD>(trigger.GetSpecifics());

    case CAITRIGGER_PARTYGOLDGT:
        // 0x456654.
        return g_pBaldurChitin->GetObjectGame()->GetGameSave()->m_nPartyGold
            > static_cast<DWORD>(trigger.GetSpecifics());

    case CAITRIGGER_PARTYGOLDLT:
        // 0x45667D.
        return g_pBaldurChitin->GetObjectGame()->GetGameSave()->m_nPartyGold
            < static_cast<DWORD>(trigger.GetSpecifics());

    case CAITRIGGER_NUMINPARTY: {
        // 0x456803: the live party size, sign-extended from a SHORT and
        // compared signed -- the opposite of PartyGold above.
        LONG nCharacters = g_pBaldurChitin->GetObjectGame()->GetNumCharacters();
        return nCharacters == trigger.GetSpecifics();
    }

    case CAITRIGGER_NUMINPARTYGT: {
        // 0x45682D.  Its tail at 0x45683F is the one HappinessGT jumps into.
        LONG nCharacters = g_pBaldurChitin->GetObjectGame()->GetNumCharacters();
        return nCharacters > trigger.GetSpecifics();
    }

    case CAITRIGGER_NUMINPARTYLT: {
        // 0x456859.
        LONG nCharacters = g_pBaldurChitin->GetObjectGame()->GetNumCharacters();
        return nCharacters < trigger.GetSpecifics();
    }

    case CAITRIGGER_TIME: {
        // 0x454F6E: the world clock's hour, a BYTE the binary zero-extends
        // (and ebx, 0xFF at 0x454F8F) before comparing.
        LONG nHour = g_pBaldurChitin->GetObjectGame()->GetWorldTimer()->GetCurrentHour();
        return nHour == trigger.GetSpecifics();
    }

    case CAITRIGGER_TIMEGT: {
        // 0x454FA8.
        LONG nHour = g_pBaldurChitin->GetObjectGame()->GetWorldTimer()->GetCurrentHour();
        return nHour > trigger.GetSpecifics();
    }

    case CAITRIGGER_TIMELT: {
        // 0x454FE2.
        LONG nHour = g_pBaldurChitin->GetObjectGame()->GetWorldTimer()->GetCurrentHour();
        return nHour < trigger.GetSpecifics();
    }

    case CAITRIGGER_RANDOMNUM: {
        // 0x45659C: RandomNum(I:Range,I:Value) -- a per-object die roll, not a
        // fresh random number.  The engine keeps `m_randValue` and this folds
        // it into 1..Range with a SIGNED idiv remainder plus one (0x4565AE).
        // Note the operands: the RANGE is GetSpecifics and the value tested
        // against is GetInt1, i.e. the reverse of the comparison families.
        LONG nRoll = m_randValue % trigger.GetSpecifics() + 1;
        return nRoll == trigger.GetInt1();
    }

    case CAITRIGGER_RANDOMNUMGT: {
        // 0x4565CA.
        LONG nRoll = m_randValue % trigger.GetSpecifics() + 1;
        return nRoll > trigger.GetInt1();
    }

    case CAITRIGGER_RANDOMNUMLT: {
        // 0x4565F8.
        LONG nRoll = m_randValue % trigger.GetSpecifics() + 1;
        return nRoll < trigger.GetInt1();
    }

    case CAITRIGGER_ISHEARTOFFURYMODEON:
        // 0x45894A: the option is handed back verbatim, not normalised to 0/1.
        return g_pBaldurChitin->GetObjectGame()->GetOptions()->m_nNightmareMode;

    case CAITRIGGER_ALIGNMENT: {
        // 0x453BDE: the ALIGNMEN.IDS half-masks -- 1/2/3 on the good-evil axis
        // and 0x10/0x20/0x30 on the law-chaos one -- are tested as BITS; every
        // other value is an exact match.  MSVC emitted the six as a compare
        // chain at 0x453BEC, and the mask branch ANDs BYTES (0x453C24).
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);

        LONG nAlignment = cause.m_specificID;
        if (nAlignment == 1 || nAlignment == 2 || nAlignment == 3
            || nAlignment == 0x10 || nAlignment == 0x20 || nAlignment == 0x30) {
            return (cause.m_triggerCause.m_nAlignment & static_cast<BYTE>(nAlignment)) != 0;
        }

        return cause.m_triggerCause.m_nAlignment == nAlignment;
    }

    case CAITRIGGER_CLASS: {
        // 0x453C59: not a comparison at all -- the id goes to
        // CAIObjectType::IsClassValid, which is what resolves CLASS.IDS' group
        // entries, and its BOOL is returned verbatim.
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);
        return cause.m_triggerCause.IsClassValid(static_cast<BYTE>(cause.m_specificID));
    }

    case CAITRIGGER_GENERAL: {
        // 0x453CC4: the decoded cause's GENERAL.IDS byte, zero-extended.
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);
        return cause.m_triggerCause.m_nGeneral == cause.m_specificID;
    }

    case CAITRIGGER_GENDER: {
        // 0x453C78: the same shape on the GENDER.IDS byte.
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);
        return cause.m_triggerCause.m_nGender == cause.m_specificID;
    }

    case CAITRIGGER_SUBRACE: {
        // 0x453B54: SubRace packs TWO ids into m_specificID -- the race in the
        // high word (an ARITHMETIC shift at 0x453B68) and the subrace in the
        // low one -- and both have to match.
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);

        if (cause.m_triggerCause.m_nRace != (cause.m_specificID >> 16)) {
            return FALSE;
        }

        if (cause.m_triggerCause.m_nSubRace != (cause.m_specificID & 0xFFFF)) {
            return FALSE;
        }

        return TRUE;
    }

    case CAITRIGGER_KIT: {
        // 0x453B95: the one arm of this family that resolves an object.  The
        // trigger's value is both the mask AND the expected result, so every
        // requested specialization bit must be set, not merely one of them.
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);

        CGameObject* pObject = cause.m_triggerCause.GetObjectWithType(this,
            CGameObject::TYPE_SPRITE,
            FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        DWORD nKit = static_cast<CGameSprite*>(pObject)->GetSpecialization();
        BOOL bHolds = (nKit & cause.m_specificID) == static_cast<DWORD>(cause.m_specificID);
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_RACE: {
        // 0x454832.  Race and Specifics are shaped differently from the rest
        // of the identity family: they decode the WHOLE trigger and read
        // through an accessor on GetCause(), where Alignment/General/Gender
        // decode only the cause and touch the field directly.  Both shapes are
        // in the binary, so neither is a tidy-up of the other.  Both join a
        // tail at 0x454F87 that Time also uses.
        CAITrigger cause(trigger);
        cause.Decode(this);

        BYTE nRace = cause.GetCause().GetRace();
        return nRace == cause.GetSpecifics();
    }

    case CAITRIGGER_SPECIFICS: {
        // 0x454F52, joining the same tail at 0x454F87.
        CAITrigger cause(trigger);
        cause.Decode(this);

        BYTE nSpecific = cause.GetCause().GetSpecific();
        return nSpecific == cause.GetSpecifics();
    }

    case CAITRIGGER_STATECHECK: {
        // 0x45537D: a mask test against the DERIVED general state, so it sees
        // the effects a creature is currently under, not its stored flags.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = (cause.GetSpecifics() & pSprite->GetDerivedStats()->m_generalState) != 0;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_NOTSTATECHECK: {
        // 0x4553C0: byte for byte the same arm, ending `inc` where StateCheck
        // ends `neg` -- so it is NONE of the bits, not "not all of them".
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = (cause.GetSpecifics() & pSprite->GetDerivedStats()->m_generalState) == 0;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_AREARESTDISABLED:
        // 0x453948: the area header's "cannot rest" flag, bit 1.  This arm
        // does check the area pointer first.
        if (m_pArea == NULL) {
            return FALSE;
        }
        return (m_pArea->m_header.m_flags & 2) != 0;

    case CAITRIGGER_ISEXTENDEDNIGHT:
        // 0x45A608: bit 6 of the header's area TYPE.  Unlike AreaRestDisabled
        // directly above, this arm does NOT null-check `m_pArea` -- it loads
        // the pointer and calls straight through (0x45A608, 0x45A60B).  Kept
        // faithful; the value is also handed back unnormalised, as 0 or 0x40.
        return m_pArea->GetHeader()->m_areaType & 0x40;

    case CAITRIGGER_ISCREATUREHIDDENINSHADOWS: {
        // 0x45A3F9: the target's live hiding state, which is a different
        // question from CreatureHidden's stored stealth-mode flag.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectWithType(this,
            CGameObject::TYPE_SPRITE,
            FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = static_cast<CGameSprite*>(pObject)->GetHiding();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_FALLENPALADIN: {
        // 0x45AAE9.  Two gates, and the first one is a surprise: the class
        // mask is tested against 7 (0x45AB33), which is
        // BARBARIAN|BARD|CLERIC -- NOT the paladin bit.  Those bit values are
        // not a guess: the binary's own GetSorcererWizardLevel masks 0x600 and
        // GetBardMonkRogueLevel masks 0x122, which pins the assignment.  Only
        // then does it test the CRE "fallen paladin" flag, bit 9 of the base
        // stats' flags.  Reproduced as the bytes read it; if this is an engine
        // bug it is the original's, and matching it is the point.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectWithType(this,
            CGameObject::TYPE_SPRITE,
            FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = FALSE;
        if (pObject->GetObjectType() == CGameObject::TYPE_SPRITE) {
            CGameSprite* pSprite = static_cast<CGameSprite*>(pObject);
            if ((pSprite->GetDerivedStats()->m_classMask
                    & (CLASSMASK_BARBARIAN | CLASSMASK_BARD | CLASSMASK_CLERIC))
                != 0) {
                bHolds = (pSprite->GetBaseStats()->m_flags & 0x200) != 0;
            }
        }

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_DELAY:
        // 0x4550C9: a STAGGERED periodic trigger, not a countdown.  The
        // object's tick counter is taken modulo the requested period and the
        // remainder compared with `field_550`, which the constructor seeds to
        // `rand() % 120` -- so each creature fires in its own slice of the
        // window instead of the whole area firing on the same tick.  The
        // comparison is `setle` (0x4550E8), i.e. inclusive.
        return field_54C % trigger.GetSpecifics() <= field_550;

    case CAITRIGGER_SEE: {
        // 0x454B03: See(O:Object*) -- the trigger every hostile creature script
        // uses to notice the party.  On a fresh sighting it records the object
        // in m_lSeen and posts SetLastObject so LastSeenBy() resolves in the
        // response block.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObject(this, FALSE);
        BOOL bHolds = CanSee(pObject, cause.GetSpecifics());

        if (bHolds) {
            if (!m_lSeen.Equal(pObject->GetAIType())) {
                // Spotting a party member reveals the spotter: m_canBeSeen is
                // the countdown CGameSprite::AIUpdate ticks back down to zero.
                if (g_pBaldurChitin->GetObjectGame()->GetCharacterPortraitNum(pObject->GetId()) != -1) {
                    m_canBeSeen = 4 * (VISIBLE_DELAY + 1);
                }

                m_lSeen.Set(pObject->GetAIType());
                g_pBaldurChitin->GetMessageHandler()->AddMessage(
                    new CMessageSetLastObject(pObject->GetAIType(), CAITRIGGER_SEE, m_id, m_id),
                    FALSE);
            }

            SetAIType342(pObject->GetAIType());
        }

        if (pObject != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
        return bHolds;
    }

    // 0x455E40: IsTeamBitOn(I:TeamFlag*TeamBit) -- mask test against the
    // team-allegiance bits written by SetTeamBit (0x729A3C).
    case CAITRIGGER_ISTEAMBITON:
        return trigger.GetSpecifics() & GetAICounter58C();

    case CAITRIGGER_SUMMONINGLIMIT: {
        // 0x4589FA: SummoningLimit(O:Object*,I:Num*).  The object is resolved
        // only so the trigger can fail when it names nobody -- the count that
        // is compared is the GLOBAL summon tally, with nothing about the
        // resolved sprite entering it.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = Icewind586B70::Instance()->GetCount() == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_SUMMONINGLIMITGT: {
        // 0x458A3F, comparison tail shared at 0x4569DD.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = Icewind586B70::Instance()->GetCount() > cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_SUMMONINGLIMITLT: {
        // 0x458A72.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = Icewind586B70::Instance()->GetCount() < cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_NUMCREATURE: {
        // 0x455237: NumCreature(S:Object*,I:Num*) -- how many objects matching
        // the trigger's object spec stand inside the CALLER'S visual range.
        // Nothing in the trigger supplies the radius, and the search is the
        // line-of-sight one over the VISIBLE terrain table.  This arm never
        // decodes, so it reads `trigger` rather than a cause copy.
        CTypedPtrList<CPtrList, LONG*> targets;
        m_pArea->GetAllInRange(m_pos,
            trigger.GetCause(),
            GetVisualRange(),
            GetVisibleTerrainTable(),
            targets,
            TRUE,
            FALSE);

        return targets.GetCount() == trigger.GetSpecifics();
    }

    case CAITRIGGER_NUMCREATUREGT: {
        // 0x455293.
        CTypedPtrList<CPtrList, LONG*> targets;
        m_pArea->GetAllInRange(m_pos,
            trigger.GetCause(),
            GetVisualRange(),
            GetVisibleTerrainTable(),
            targets,
            TRUE,
            FALSE);

        return targets.GetCount() > trigger.GetSpecifics();
    }

    case CAITRIGGER_NUMCREATURELT: {
        // 0x4552EF.
        CTypedPtrList<CPtrList, LONG*> targets;
        m_pArea->GetAllInRange(m_pos,
            trigger.GetCause(),
            GetVisualRange(),
            GetVisibleTerrainTable(),
            targets,
            TRUE,
            FALSE);

        return targets.GetCount() < trigger.GetSpecifics();
    }

    case CAITRIGGER_AREANAMEDIFFERS: {
        // 0x45739B, an id TRIGGER.IDS does not name -- the label above is
        // descriptive, from what this body does.  It compares String1 with the
        // current area's resref through CString::CompareNoCase and returns
        // THAT value, so the trigger holds exactly when the two names DIFFER.
        // Reproduced as the binary has it; the id is unnamed, so no compiled
        // script can emit it.
        CString sName = trigger.GetString1();
        CString sArea = reinterpret_cast<LPCTSTR>(m_pArea->GetHeader()->m_areaName);
        return sName.CompareNoCase(sArea);
    }

    case CAITRIGGER_BATTLESONGCOUNTER: {
        // 0x4573DA, also unnamed in TRIGGER.IDS: the area's bard-song battle
        // counter against the trigger's value.
        return m_pArea->GetBattleSongCounter() == trigger.GetSpecifics();
    }

    case CAITRIGGER_BATTLESONGCOUNTERLT: {
        // 0x4573F9, comparison tail shared at 0x456AA3.  Mind the order of
        // this triple: it runs equal / LESS / greater, not the equal / GT / LT
        // every other family in this switch uses.
        return m_pArea->GetBattleSongCounter() < trigger.GetSpecifics();
    }

    case CAITRIGGER_BATTLESONGCOUNTERGT: {
        // 0x457406, comparison tail shared at 0x459A21.
        return m_pArea->GetBattleSongCounter() > trigger.GetSpecifics();
    }

    case CAITRIGGER_AREATYPE: {
        // 0x457415, unnamed in TRIGGER.IDS: a mask test of the area header's
        // type word against the trigger's value, both narrowed to 16 bits
        // (and eax, 0xFFFF at 0x45742C) before the AND.
        return m_pArea->GetHeader()->m_areaType & (trigger.GetSpecifics() & 0xFFFF);
    }

    case CAITRIGGER_HASWEAPONEQUIPED: {
        // 0x456712: HasWeaponEquiped(O:Object*) -- true whenever the selected
        // weapon slot is anything OTHER than the fist slot, so an empty-handed
        // creature is the only false case.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = pSprite->GetEquipment()->m_selectedWeapon
            != CGameSpriteEquipment::SLOT_FIST;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_CHECKDOORFLAGS: {
        // 0x45A3AD: CheckDoorFlags(O:Object*,I:Flags*) -- a mask test against
        // the door's own flag word.  GetObjectWithType filters to TYPE_DOOR,
        // so a cause naming anything else simply fails.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectWithType(this,
            CGameObject::TYPE_DOOR,
            FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = cause.GetSpecifics()
            & static_cast<CGameDoor*>(pObject)->GetFlags();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_ISROTATION: {
        // 0x45A702: IsRotation(O:Object*,I:Rotation*) -- the sprite's facing.
        // GetDirection returns a SHORT and the binary sign-extends it
        // (movsx esi, ax at 0x45A73E) before the comparison.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectWithType(this,
            CGameObject::TYPE_SPRITE,
            FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = static_cast<CGameSprite*>(pObject)->GetDirection()
            == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_ISFACINGSAVEDROTATION: {
        // 0x45A752: the sprite's facing against its OWN saved facing, with
        // nothing from the trigger entering the comparison.  The saved value
        // is a BYTE and the direction a SHORT, and the binary compares them
        // 16 bits wide (sub si, ax at 0x45A79A), not 32.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectWithType(this,
            CGameObject::TYPE_SPRITE,
            FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        CGameSprite* pSprite = static_cast<CGameSprite*>(pObject);
        BOOL bHolds = static_cast<WORD>(pSprite->GetBaseStats()->m_savedLocationFacing)
            == static_cast<WORD>(pSprite->GetDirection());
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_SETLASTMARKEDOBJECT: {
        // 0x45A8A0: not a predicate at all -- it STORES the resolved object
        // as the caller's marked type and always returns TRUE.  A cause that
        // resolves to nobody stores NOONE rather than leaving the slot alone.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObject(this, FALSE);
        if (pObject == NULL) {
            SetAIType342(CAIObjectType::NOONE);
            return TRUE;
        }

        SetAIType342(pObject->GetAIType());
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return TRUE;
    }

    case CAITRIGGER_SETSPELLTARGET: {
        // 0x45A8E9: the same shape as SetLastMarkedObject, into the other
        // slot -- SetAIType3BA rather than SetAIType342.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObject(this, FALSE);
        if (pObject == NULL) {
            SetAIType3BA(CAIObjectType::NOONE);
            return TRUE;
        }

        SetAIType3BA(pObject->GetAIType());
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return TRUE;
    }

    case CAITRIGGER_ISPATHCRITICALOBJECT: {
        // 0x45A932: bit 13 of the CRE flags, the same base-stats word
        // FallenPaladin reads bit 9 of.  Note it resolves with GetObject and
        // not GetObjectWithType, so nothing checks the object really is a
        // sprite before GetBaseStats is called on it.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObject(this, FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = (static_cast<CGameSprite*>(pObject)->GetBaseStats()->m_flags
            >> 13) & 1;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_INTRAP: {
        // 0x45A5AC: InTrap(O:Object*) is asked OF the trap region and not of
        // the creature -- when the caller is not itself a TYPE_TRIGGER the
        // cause is never even resolved.  Two things here are the binary's, not
        // a slip: GetObject's second argument is TYPE_SPRITE where the
        // parameter is a BOOL checkBackList, and the no-object result is -2,
        // not FALSE.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = NULL;
        if (GetObjectType() == CGameObject::TYPE_TRIGGER) {
            pObject = cause.GetCause().GetObject(this, CGameObject::TYPE_SPRITE);
        }

        if (pObject == NULL) {
            return -2;
        }

        BOOL bHolds = static_cast<CGameTrigger*>(this)->IsOverActivate(pObject->GetPos());
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_NUMCREATURELESSPARTY: {
        // 0x4550F2, an id TRIGGER.IDS does not name -- the label is
        // descriptive.  It is NumCreature's search verbatim, with the WHOLE
        // party size subtracted from the count afterwards, whether or not any
        // party member was actually in range.
        CTypedPtrList<CPtrList, LONG*> targets;
        m_pArea->GetAllInRange(m_pos,
            trigger.GetCause(),
            GetVisualRange(),
            GetVisibleTerrainTable(),
            targets,
            TRUE,
            FALSE);

        LONG nCount = targets.GetCount()
            - g_pBaldurChitin->GetObjectGame()->GetNumCharacters();
        return nCount == trigger.GetSpecifics();
    }

    case CAITRIGGER_NUMCREATURELESSPARTYLT: {
        // 0x455151.  Like the battle-song triple, this one runs
        // equal / LESS / greater, so the LT body is the middle one.
        CTypedPtrList<CPtrList, LONG*> targets;
        m_pArea->GetAllInRange(m_pos,
            trigger.GetCause(),
            GetVisualRange(),
            GetVisibleTerrainTable(),
            targets,
            TRUE,
            FALSE);

        LONG nCount = targets.GetCount()
            - g_pBaldurChitin->GetObjectGame()->GetNumCharacters();
        return nCount < trigger.GetSpecifics();
    }

    case CAITRIGGER_NUMCREATURELESSPARTYGT: {
        // 0x4551C4.
        CTypedPtrList<CPtrList, LONG*> targets;
        m_pArea->GetAllInRange(m_pos,
            trigger.GetCause(),
            GetVisualRange(),
            GetVisibleTerrainTable(),
            targets,
            TRUE,
            FALSE);

        LONG nCount = targets.GetCount()
            - g_pBaldurChitin->GetObjectGame()->GetNumCharacters();
        return nCount > trigger.GetSpecifics();
    }

    case CAITRIGGER_TIMERACTIVE: {
        // 0x4586DA: TimerActive(I:ID*) walks the caller's OWN timer list.
        // There is no early out -- the whole list is walked even once a match
        // has set the result.
        BOOL bHolds = FALSE;
        POSITION pos = m_timers.GetHeadPosition();
        while (pos != NULL) {
            CGameTimer* pTimer = m_timers.GetNext(pos);
            if (pTimer != NULL && pTimer->GetId() == trigger.GetSpecifics()) {
                bHolds = TRUE;
            }
        }
        return bHolds;
    }

    case CAITRIGGER_ISSCRIPTNAME: {
        // 0x45395C: IsScriptName(S:Name*,O:Object*).  Only the CAUSE is
        // decoded here -- CAIObjectType::Decode, not CAITrigger::Decode -- and
        // it is filtered to TYPE_AIBASE.  The name is built as a fixed
        // 32-character CString from the object's m_scriptName, and unlike the
        // area-name arm at 0x45739B this one tests the comparison for ZERO, so
        // it holds when the two names MATCH.
        CAITrigger cause(trigger);
        cause.m_triggerCause.Decode(this);

        CGameObject* pObject = cause.m_triggerCause.GetObjectWithType(this,
            CGameObject::TYPE_AIBASE,
            FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        CString sName(static_cast<CGameAIBase*>(pObject)->GetScriptName(),
            SCRIPTNAME_SIZE);
        BOOL bHolds = sName.CompareNoCase(cause.GetString1()) == 0;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_SPELLSTATE:
    case CAITRIGGER_CHECKSPELLSTATE: {
        // 0x4588ED: one body for two opcodes -- the table sends 0x40AC, which
        // TRIGGER.IDS does not name, to CheckSpellState's arm.  The test is a
        // plain lookup in the derived stats' 256-bit spell-state set.  The
        // index is read BEFORE the stats pointer, so it is kept in a local
        // rather than written inline where the order would be unspecified.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        LONG nState = cause.GetSpecifics();
        BOOL bHolds = pSprite->GetDerivedStats()->m_spellStates[nState];
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_HASITEMPERSONAL: {
        // 0x45608F, another id TRIGGER.IDS does not name -- descriptive label.
        // It resolves a TYPE_AIBASE object but only searches when that object
        // turns out to be a sprite, so a door or container named as the cause
        // is simply false.  FindItemPersonal answers -1 when nothing matches.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObjectWithType(this,
            CGameObject::TYPE_AIBASE,
            FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = FALSE;
        if (pObject->GetObjectType() == CGameObject::TYPE_SPRITE) {
            bHolds = static_cast<CGameSprite*>(pObject)->FindItemPersonal(cause.GetString1(),
                0,
                FALSE)
                != -1;
        }

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_ISPLAYERNUMBER: {
        // 0x455E5D: IsPlayerNumber(O:Object*,I:Slot*).  The slot is one-based,
        // and the binary re-reads it from the trigger for each of the three
        // tests rather than keeping it.  The lookup is the FIXED-order party
        // array, not the portrait order GetCharacterId walks.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObject(this, FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = FALSE;
        if (cause.GetSpecifics() >= 1 && cause.GetSpecifics() <= 6) {
            LONG nId = g_pBaldurChitin->GetObjectGame()->GetFixedOrderCharacterId(
                static_cast<SHORT>(cause.GetSpecifics() - 1));
            bHolds = nId == pObject->GetId();
        }

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_OPENSTATE: {
        // 0x456B8D: OpenState(O:Object*,I:Open*) -- the value is not compared,
        // only tested for zero, so any non-zero asks "open" and zero asks
        // "closed".  A cause that resolves to something other than a door is
        // false, but the share is still released.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObject(this, FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = FALSE;
        if (pObject->GetObjectType() == CGameObject::TYPE_DOOR) {
            CGameDoor* pDoor = static_cast<CGameDoor*>(pObject);
            if (cause.GetSpecifics() != 0) {
                bHolds = pDoor->IsOpen();
            } else {
                bHolds = pDoor->IsOpen() == 0;
            }
        }

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_SEQUENCE: {
        // 0x458967: Sequence(O:Object*,I:Sequence*) answers for two kinds of
        // object, and NOT the same way.  A sprite's GetSequence is a SHORT the
        // binary sign-extends (movsx at 0x4589BB); a static's vid-cell
        // sequence id is a WORD it zero-extends (0x4589DC).  Anything else is
        // false.
        CAITrigger cause(trigger);
        cause.Decode(this);

        CGameObject* pObject = cause.GetCause().GetObject(this, FALSE);
        if (pObject == NULL) {
            return FALSE;
        }

        BOOL bHolds = FALSE;
        BYTE nType = pObject->GetObjectType();
        if (nType == CGameObject::TYPE_STATIC) {
            CVidCell* pVidCell = static_cast<CGameStatic*>(pObject)->GetVidCell();
            bHolds = pVidCell->GetCurrentSequenceId() == cause.GetSpecifics();
        } else if (nType == CGameObject::TYPE_SPRITE) {
            LONG nSequence = cause.GetSpecifics();
            bHolds = static_cast<CGameSprite*>(pObject)->GetSequence() == nSequence;
        }

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pObject->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    case CAITRIGGER_ISANIMATIONID: {
        // 0x45AB5A: the sprite's animation id, a WORD the binary zero-extends
        // before the comparison.
        CAITrigger cause(trigger);
        CGameSprite* pSprite = NULL;
        ResolveTriggerSprite(cause, &pSprite);

        if (pSprite == NULL) {
            return FALSE;
        }

        BOOL bHolds = pSprite->GetAnimation()->GetAnimationId() == cause.GetSpecifics();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(pSprite->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        return bHolds;
    }

    default:
        return CGameObject::EvaluateStatusTrigger(trigger);
    }
}

// 0x44D4B0
void CGameAIBase::CheckTimers(LONG cycles)
{
    POSITION pos = m_timers.GetHeadPosition();
    while (pos != NULL) {
        POSITION oldPos = pos;
        CGameTimer* pTimer = m_timers.GetNext(pos);
        pTimer->m_time -= cycles;
        if (pTimer->m_time <= 0) {
            CAITrigger trigger(CAITrigger::TIMEREXPIRED, pTimer->m_id);

            // NOTE: Uninline.
            SetTrigger(trigger);

            m_timers.RemoveAt(oldPos);
            delete pTimer;
        }
    }
}

// 0x44D640
void CGameAIBase::ClearActions(BOOL a1)
{
    if (a1) {
        POSITION pos = m_queuedActions.GetHeadPosition();
        while (pos != NULL) {
            POSITION curr = pos;
            CAIAction* pAction = m_queuedActions.GetNext(pos);
            if (pAction != NULL) {
                if (pAction->m_actionID != CAIAction::SPELL
                    && pAction->m_actionID != CAIAction::SPELLPOINT
                    && pAction->m_actionID != CAIAction::FORCESPELL
                    && pAction->m_actionID != CAIAction::FORCESPELLPOINT
                    && pAction->m_actionID != CAIAction::APPLYSPELL
                    && pAction->m_actionID != CAIAction::REALLYFORCESPELL
                    && pAction->m_actionID != CAIAction::SPELLNODEC
                    && pAction->m_actionID != CAIAction::SPELLPOINTNODEC) {
                    m_queuedActions.RemoveAt(curr);
                    delete pAction;
                }
            }
        }
    } else {
        POSITION pos = m_queuedActions.GetHeadPosition();
        while (pos != NULL) {
            CAIAction* pAction = m_queuedActions.GetNext(pos);
            if (pAction != NULL) {
                delete pAction;
            }
        }
        m_queuedActions.RemoveAll();
    }

    if (m_queuedActions.GetCount() == 0) {
        m_curResponseNum = -1;
        m_curResponseSetNum = -1;
        m_curScriptNum = -1;
    }
}

// 0x799E60
// No-op base (3-byte stub in the binary); CGameSprite overrides this at 0x733660
// to update the current target id + target marker.
void CGameAIBase::UpdateTarget(CGameObject* pObject)
{
}

// 0x44D730
void CGameAIBase::ClearTriggers()
{
    ApplyTriggers();

    POSITION pos = m_pendingTriggers.GetHeadPosition();
    while (pos != NULL) {
        CAITrigger* pTrigger = m_pendingTriggers.GetNext(pos);
        if (pTrigger != NULL) {
            delete pTrigger;
        }
    }
    m_pendingTriggers.RemoveAll();
}

// 0x44D780
void CGameAIBase::DoAction()
{
    SHORT actionReturn = ExecuteAction();

    if (actionReturn == ACTION_DONE
        || actionReturn == ACTION_ERROR
        || actionReturn == ACTION_STOPPED) {
        CAIAction action;
        SetCurrAction(GetNextAction(action));
        ResetCurrResponse();
    } else if (m_interrupt && actionReturn == ACTION_INTERRUPTABLE) {
        CAIAction action;
        m_actionCount++;
        SetCurrAction(GetNextAction(action));
        m_interrupt = FALSE;
    } else {
        m_actionCount++;
    }
}

// 0x45D050 (vtable 0xA4)
BOOL CGameAIBase::ResolveTriggerSprite(CAITrigger& trigger, CGameSprite** ppSprite)
{
    trigger.m_triggerCause.Decode(this);

    CGameObject* pObject = trigger.m_triggerCause.GetObjectWithType(this,
        CGameObject::TYPE_SPRITE,
        FALSE);
    *ppSprite = static_cast<CGameSprite*>(pObject);

    if (pObject != NULL && pObject->GetAIType().Equal(CAIObjectType::NOT_SPRITE)) {
        // Resolved to a placeholder rather than a real creature: drop it and
        // hand the caller NULL, releasing the share GetObjectWithType took.
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare((*ppSprite)->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        *ppSprite = NULL;
    }

    // Always FALSE here; callers use the result as a "the share is not mine to
    // release" flag, and the shared trigger epilogue keys its ReleaseShare on it.
    return FALSE;
}

// 0x44DAC0
BOOL CGameAIBase::CanSee(CGameObject* pObject, BOOL bIncludeDead)
{
    if (pObject == NULL) {
        return FALSE;
    }

    if (pObject->m_pArea != m_pArea) {
        return FALSE;
    }

    if (pObject->GetObjectType() == CGameObject::TYPE_SPRITE) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(pObject);

        if (pSprite->m_baseStats.m_bStealthMode) {
            return FALSE;
        }

        // bIncludeDead lets a caller notice corpses; See() itself passes the
        // trigger's specifics field, which is 0 for the plain See(O:Object*).
        if (!bIncludeDead
            && (pSprite->m_derivedStats.m_generalState & STATE_DEAD) != 0) {
            return FALSE;
        }
    }

    CPoint ptObject = pObject->GetPos();
    CPoint cellObject(ptObject.x / CPathSearch::GRID_SQUARE_SIZEX,
        ptObject.y / CPathSearch::GRID_SQUARE_SIZEY);

    CPoint ptSelf = GetPos();
    CPoint cellSelf(ptSelf.x / CPathSearch::GRID_SQUARE_SIZEX,
        ptSelf.y / CPathSearch::GRID_SQUARE_SIZEY);

    LONG nDistSq = (cellSelf.x - cellObject.x) * (cellSelf.x - cellObject.x)
        + (cellSelf.y - cellObject.y) * (cellSelf.y - cellObject.y);

    // Only a party-orderable sprite is required to stand on explored ground --
    // monsters see each other through the fog of war.
    BOOL bCheckExplored = FALSE;
    if (pObject->GetObjectType() == CGameObject::TYPE_SPRITE) {
        bCheckExplored = static_cast<CGameSprite*>(pObject)->Orderable(FALSE);
    }

    if (nDistSq > GetVisualRange() * GetVisualRange()) {
        return FALSE;
    }

    if (!m_pArea->CheckLOS(pObject->GetPos(),
            GetPos(),
            GetVisibleTerrainTable(),
            bCheckExplored)) {
        return FALSE;
    }

    return TRUE;
}

// 0x44DC10
SHORT CGameAIBase::ExecuteAction()
{
    // TODO: Incomplete; target-object and message-heavy actions still need recovery.
    SHORT actionReturn = ACTION_DONE;

    // ActionOverride (id 1) is a queue marker.  The script compiler emits it
    // followed by the inner action with m_actorID pre-baked to the override
    // target, so dequeue it here and let the switch dispatch the real action
    // in the same tick.
    if (m_curAction.m_actionID == 1) {
        SetCurrAction(GetNextAction(m_aiAction));
    }

    if (m_curAction.m_actionID == CAIAction::NO_ACTION) {
        actionReturn = ACTION_NO_ACTION;
    } else if (m_curAction.m_actionID == CAIAction::MOVEVIEWPOINT
        || m_curAction.m_actionID == CAIACTION_MOVEVIEWPOINTUNTILDONE) {
        actionReturn = MoveViewPoint();
    } else if (m_curAction.m_actionID == CAIAction::CLICKLBUTTONPOINT) {
        actionReturn = ClickLButtonPoint();
    } else if (m_curAction.m_actionID == CAIAction::CLICKRBUTTONPOINT) {
        actionReturn = ClickRButtonPoint();
    } else if (m_curAction.m_actionID == CAIAction::DOUBLECLICKLBUTTONPOINT) {
        actionReturn = DoubleClickLButtonPoint();
    } else if (m_curAction.m_actionID == CAIAction::DOUBLECLICKRBUTTONPOINT) {
        actionReturn = DoubleClickRButtonPoint();
    } else if (m_curAction.m_actionID == CAIAction::MOVECURSORPOINT) {
        actionReturn = MoveCursorPoint();
    } else if (m_curAction.m_actionID == CAIAction::CHANGEAISCRIPT) {
        actionReturn = ChangeAIScript();
    } else if (m_curAction.m_actionID == CAIAction::STARTTIMER) {
        actionReturn = StartTimer();
    } else if (m_curAction.m_actionID == CAIAction::WAIT
        || m_curAction.m_actionID == 0xCD) {
        actionReturn = Wait();
    } else if (m_curAction.m_actionID == CAIAction::SMALLWAIT) {
        actionReturn = SmallWait();
    } else if (m_curAction.m_actionID == CAIAction::SHOUT
        || m_curAction.m_actionID == CAIACTION_212) {
        actionReturn = Shout();
    } else if (m_curAction.m_actionID == 0x1E
        || m_curAction.m_actionID == 0x132) {
        // 0x1E = SetGlobal, 0x132 = SetGlobalRandom (ACTION.IDS).
        actionReturn = SetGlobal();
    } else if (m_curAction.m_actionID == 0x6D) {
        // 0x6D = IncrementGlobal (ACTION.IDS).
        actionReturn = IncrementGlobal();
    } else if (m_curAction.m_actionID == 0x143) {
        // 0x143 = WaitAnimation (ACTION.IDS).
        actionReturn = WaitAnimation();
    } else if (m_curAction.m_actionID == 0xE9) {
        // 0xE9 = HideCreature (ACTION.IDS).
        actionReturn = HideCreature();
    } else if (m_curAction.m_actionID == 0xF1) {
        // 0xF1 = FloatMessage (ACTION.IDS).
        actionReturn = FloatMessage();
    } else if (m_curAction.m_actionID == 0xB7) {
        // 0xB7 = MultiPlayerSync (ACTION.IDS).  Binary 0x466750 short-circuits
        // to ACTION_DONE in SP (DAT_008cf6dc+0x96e == DAT_0085e65c).  Skip
        // the MP handshake -- recovery deferred until MP path is restored.
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x78) {
        // 0x78 = StartCutScene (ACTION.IDS).
        actionReturn = StartCutScene();
    } else if (m_curAction.m_actionID == 0x7B) {
        // 0x7B = ClearAllActions (ACTION.IDS).  The intro area uses this
        // immediately before StartCutScene; clear party action queues without
        // touching the current script object's remaining response actions.
        CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
        for (SHORT nPortrait = 0; nPortrait < pGame->GetNumCharacters(); ++nPortrait) {
            LONG nCharacterId = pGame->GetCharacterId(nPortrait);
            CGameAIBase* pSprite = NULL;
            BYTE rc = pGame->GetObjectArray()->GetShare(
                nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                pSprite->ClearActions(FALSE);
                pGame->GetObjectArray()->ReleaseShare(
                    nCharacterId,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x7F) {
        // 0x7F = CutSceneId (ACTION.IDS).  StartCutScene consumes this as a
        // block-local actor switch; executing it directly is a no-op.
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xA1) {
        // 0xA1 = IncrementChapter (ACTION.IDS).
        actionReturn = IncrementChapter();
    } else if (m_curAction.m_actionID == 0xE8) {
        // 0xE8 = StartRandomTimer (ACTION.IDS).  Rolls a random time in
        // [m_specificID2, m_specificID3] inclusive into m_specificID2, then
        // dispatches the regular StartTimer handler (TimerID stays in
        // m_specificID).
        LONG lo = m_curAction.m_specificID2;
        LONG hi = m_curAction.m_specificID3;
        if (lo > hi) {
            LONG tmp = lo;
            lo = hi;
            hi = tmp;
        }
        LONG range = hi - lo + 1;
        if (range <= 1) {
            m_curAction.m_specificID2 = lo;
        } else {
            m_curAction.m_specificID2 = (rand() % range) + lo;
        }
        actionReturn = StartTimer();
    } else if (m_curAction.m_actionID == 0xFC) {
        // 0xFC = ChangeCurrentScript (ACTION.IDS).  Binary pokes
        // m_curScriptNum into m_specificID then falls into the 0x3C
        // (ChangeAIScript) handler, which reads m_specificID for the
        // script-slot index.
        m_curAction.m_specificID = static_cast<LONG>(m_curScriptNum);
        actionReturn = ChangeAIScript();
    } else if (m_curAction.m_actionID == 0xAC) {
        // 0xAC = ChangeTileState (ACTION.IDS).  Target is a CGameTiledObject.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_TILED_OBJECT);
        if (pObj != NULL && pObj->GetObjectType() == CGameObject::TYPE_TILED_OBJECT) {
            actionReturn = ChangeTileState(static_cast<CGameTiledObject*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xB1) {
        // 0xB1 = TriggerActivation (ACTION.IDS).  Target is a CGameTrigger.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_TRIGGER);
        if (pObj != NULL && pObj->GetObjectType() == CGameObject::TYPE_TRIGGER) {
            actionReturn = TriggerActivation(static_cast<CGameTrigger*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xC3) {
        // 0xC3 = Lock (ACTION.IDS).  Target is door or container.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_AIBASE);
        if (pObj != NULL) {
            actionReturn = Lock(static_cast<CGameAIBase*>(pObj));
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xC4) {
        // 0xC4 = Unlock (ACTION.IDS).  Target is door or container.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_AIBASE);
        if (pObj != NULL) {
            actionReturn = Unlock(static_cast<CGameAIBase*>(pObj));
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == CAIAction::FORCESPELL
        || m_curAction.m_actionID == CAIAction::REALLYFORCESPELL
        ) {
        // 0x71 = ForceSpell, 0xB5 = ReallyForceSpell. Normal Spell and
        // SpellNoDec actions are dispatched by CGameSprite::ExecuteAction.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_AIBASE);
        actionReturn = ForceSpellAction(pObj);
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == CAIAction::SPELLPOINT
        || m_curAction.m_actionID == CAIAction::FORCESPELLPOINT
        || m_curAction.m_actionID == CAIAction::SPELLPOINTNODEC) {
        // 0x5F = SpellPoint, 0x72 = ForceSpellPoint, 0xC0 = SpellPointNoDec.
        // Ghidra's binary jump table routes 0x72 to FUN_00461B80.  Sprites
        // never reach here for 0x5F/0xC0 anymore: CGameSprite::ExecuteAction
        // dispatches them to SpellPointSequence (jumptable case 0x2c), like
        // the binary.  Non-sprite AIBASEs keep the force path.
        actionReturn = ForceSpellPointAction();
    } else if (m_curAction.m_actionID == 0x10) {
        // 0x10 = GiveOrder (ACTION.IDS).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_AIBASE);
        if (pObj != NULL && (pObj->GetObjectType() & CGameObject::TYPE_AIBASE) != 0) {
            actionReturn = GiveOrder(static_cast<CGameAIBase*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0x3E) {
        // 0x3E = SendTrigger (ACTION.IDS).
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL && (pObj->GetObjectType() & CGameObject::TYPE_AIBASE) != 0) {
            actionReturn = SendTrigger(static_cast<CGameAIBase*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0x97) {
        // 0x97 = DisplayString (ACTION.IDS).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_AIBASE);
        if (pObj != NULL && (pObj->GetObjectType() & CGameObject::TYPE_AIBASE) != 0) {
            actionReturn = DisplayString(static_cast<CGameAIBase*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xC9) {
        // 0xC9 = DetectSecretDoor (ACTION.IDS).  Target is CGameDoor.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_DOOR);
        if (pObj != NULL && pObj->GetObjectType() == CGameObject::TYPE_DOOR) {
            actionReturn = DetectSecretDoor(static_cast<CGameDoor*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xD1) {
        // 0xD1 = SpawnPtActivate (ACTION.IDS).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPAWNING);
        if (pObj != NULL && pObj->GetObjectType() == CGameObject::TYPE_SPAWNING) {
            actionReturn = SpawnPtActivate(static_cast<CGameSpawning*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xD2) {
        // 0xD2 = SpawnPtDeactivate (ACTION.IDS).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPAWNING);
        if (pObj != NULL && pObj->GetObjectType() == CGameObject::TYPE_SPAWNING) {
            actionReturn = SpawnPtDeactivate(static_cast<CGameSpawning*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xD3) {
        // 0xD3 = SpawnPtSpawn (ACTION.IDS).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPAWNING);
        if (pObj != NULL && pObj->GetObjectType() == CGameObject::TYPE_SPAWNING) {
            actionReturn = SpawnPtSpawn(static_cast<CGameSpawning*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xD5 || m_curAction.m_actionID == 0xD6) {
        // 0xD5 = StartStatic, 0xD6 = StopStatic.  Binary pushes bStart = 1
        // for 0xD5 and bStart = 0 for 0xD6 (0x450171 vs 0x45019C).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_STATIC);
        if (pObj != NULL && pObj->GetObjectType() == CGameObject::TYPE_STATIC) {
            actionReturn = StaticStart(static_cast<CGameStatic*>(pObj),
                m_curAction.m_actionID == 0xD5 ? TRUE : FALSE);
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0xC5) {
        // 0xC5 = MoveGlobal (ACTION.IDS).  Target is a CGameSprite.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL && (pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
            actionReturn = MoveGlobal(static_cast<CGameSprite*>(pObj));
        }
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0x84) {
        // 0x84 = VerbalConstant (ACTION.IDS).  Resolves m_acteeID and
        // broadcasts CMessageVerbalConstant so the target plays the named
        // voice line (m_specificID).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            CMessage* msg = new CMessageVerbalConstant(
                m_curAction.m_specificID,
                m_id,
                pObj->m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x119) {
        // 0x119 = MarkObject (ACTION.IDS).  Stores the resolved target's
        // AI type in field_342 via SetAIType342; NOONE when unresolved.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj == NULL) {
            SetAIType342(CAIObjectType::NOONE);
        } else {
            SetAIType342(pObj->GetAIType());
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x131) {
        // 0x131 = SetMyTarget (ACTION.IDS).  Stores the resolved target's
        // AI type in field_37E via SetAIType37E; NOONE when unresolved.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj == NULL) {
            SetAIType37E(CAIObjectType::NOONE);
        } else {
            SetAIType37E(pObj->GetAIType());
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xA7) {
        // 0xA7 = StartMovie.
        actionReturn = StartMovie();
    } else if (m_curAction.m_actionID == 0xA9) {
        // 0xA9 = DestroyItem (ACTION.IDS).  Pure wire to existing helper.
        actionReturn = DestroyItem();
    } else if (m_curAction.m_actionID == 0xAA) {
        // 0xAA = RevealAreaOnMap.
        actionReturn = RevealAreaOnMap();
    } else if (m_curAction.m_actionID == 0xBB) {
        // 0xBB = StartMusic.
        actionReturn = StartMusic();
    } else if (m_curAction.m_actionID == 0xBE) {
        // 0xBE = FinalSave.
        actionReturn = FinalSave();
    } else if (m_curAction.m_actionID == 0xCA) {
        // 0xCA = FadeToColor.
        actionReturn = FadeToColor();
    } else if (m_curAction.m_actionID == 0xCB) {
        // 0xCB = FadeFromColor.
        actionReturn = FadeFromColor();
    } else if (m_curAction.m_actionID == 0xFD) {
        // 0xFD = FadeColorActivate.
        actionReturn = FadeColorActivate();
    } else if (m_curAction.m_actionID == 0x144) {
        // 0x144 = SetMusic.
        actionReturn = SetMusic();
    } else if (m_curAction.m_actionID == 0xEB) {
        // 0xEB = PlaySequence (ACTION.IDS).  Resolves m_acteeID, then
        // broadcasts a CMessageSetSequence so the target plays the
        // requested animation sequence (m_specificID).
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            CMessage* msg = new CMessageSetSequence(
                static_cast<BYTE>(m_curAction.m_specificID),
                pObj->m_id,
                m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x85) {
        // 0x85 = ClearActions (ACTION.IDS).  Resolves m_acteeID and forwards
        // to ClearActions(target), which queues a CMessageClearActions.
        // TODO: Binary also enqueues a NULL_ACTION terminator after clear.
        CGameObject* pObj = ResolveActionTarget();
        actionReturn = ClearActions(pObj);
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
    } else if (m_curAction.m_actionID == CAIAction::TAKEPARTYGOLD) {
        actionReturn = TakePartyGold();
    } else if (m_curAction.m_actionID == CAIAction::GIVEPARTYGOLD
        || m_curAction.m_actionID == CAIAction::GIVEGOLDFORCE) {
        actionReturn = GivePartyGold();
    } else if (m_curAction.m_actionID == 0x1A) {
        // 0x1A = CallLightning (ACTION.IDS 26).  Binary case 0x1a:
        // resolves the target, draws the lightning bolt via
        // CInfinity::CallLightning, then queues a Damage(0x0D) effect
        // on the target through CMessageAddEffect.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            CPoint ptTarget = pObj->GetPos();
            CGameArea* pArea = pObj->GetArea();
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);

            // Draw the lightning bolt from the sky.
            if (pArea != NULL) {
                pArea->GetInfinity()->CallLightning(ptTarget.x, ptTarget.y);
            }

            // Queue the damage effect (opcode 0x0D = Damage).
            // TODO: the damage dice come from m_specificID (spell ability
            // header); using a placeholder for now.
            CGameEffect* pEffect = new CGameEffect();
            pEffect->m_effectID = 0x0D;        // Damage opcode
            pEffect->m_targetType = 9;          // target type
            pEffect->m_effectAmount = 0x100;    // TODO: read from action params

            CMessage* pMsg = new CMessageAddEffect(pEffect, m_id, pObj->m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pMsg, FALSE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x32) {
        // 0x32 = MoveViewObject (ACTION.IDS 50).  Binary case 0x32.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            actionReturn = MoveViewObject(pObj);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0x34) {
        // 0x34 = ClickLButtonObject (ACTION.IDS 52).  Binary case 0x34.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            actionReturn = ClickLButtonObject(pObj);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0x36) {
        // 0x36 = ClickRButtonObject (ACTION.IDS 54).  Binary case 0x36.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            actionReturn = ClickRButtonObject(pObj);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0x38) {
        // 0x38 = DoubleClickLButtonObject (ACTION.IDS 56).  Binary case 0x38.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            actionReturn = DoubleClickLButtonObject(pObj);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0x3A) {
        // 0x3A = DoubleClickRButtonObject (ACTION.IDS 58).  Binary case 0x3a.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            actionReturn = DoubleClickRButtonObject(pObj);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
    } else if (m_curAction.m_actionID == 0x24
        || m_curAction.m_actionID == 0xB2
        || m_curAction.m_actionID == 0xEA
        || m_curAction.m_actionID == 0xF6) {
        // 0x24=Continue, 0xB2=BreakInstants, 0xEA=Debug, 0xF6=Log.
        // Binary cases share switchD_0044DC95_caseD_24 which is
        // sVar7 = -1 (ACTION_DONE) -- no side effects.
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x7E) {
        // 0x7E = Activate (ACTION.IDS 126).  Binary case 0x7e posts a
        // CMessageSetActive(TRUE) on the resolved target.
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            CMessage* msg = new CMessageSetActive(TRUE, m_id, pObj->m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x7D) {
        // 0x7D = Deactivate (ACTION.IDS 125).  Binary case 0x7d posts
        // CMessageSetActive(FALSE) -- but skips PCs (party-portrait check).
        CGameObject* pObj = ResolveActionTarget();
        if (pObj != NULL) {
            SHORT nPortrait = g_pBaldurChitin->GetObjectGame()
                                  ->GetCharacterPortraitNum(pObj->m_id);
            if (nPortrait == -1) {
                CMessage* msg = new CMessageSetActive(FALSE, m_id, pObj->m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xA2) {
        // 0xA2 = ReputationSet (ACTION.IDS 162).  Binary computes the
        // delta from m_specificID (1-20 scale) and current m_nReputation
        // (10-200 internal), then calls ReputationAdjustment(delta*10).
        CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
        SHORT delta = static_cast<SHORT>(m_curAction.m_specificID)
            - pGame->m_nReputation / CInfGame::REPUTATION_MULTIPLIER;
        pGame->ReputationAdjustment(
            static_cast<SHORT>(delta * CInfGame::REPUTATION_MULTIPLIER), FALSE);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xA3) {
        // 0xA3 = ReputationInc (ACTION.IDS 163).  Pure adjustment by
        // m_specificID * REPUTATION_MULTIPLIER.
        g_pBaldurChitin->GetObjectGame()->ReputationAdjustment(
            static_cast<SHORT>(m_curAction.m_specificID
                * CInfGame::REPUTATION_MULTIPLIER),
            FALSE);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x104) {
        // 0x104 = JumpToPointInstant (ACTION.IDS 260).  Binary case 0x104
        // resolves the target sprite and calls JumpToPoint(dest).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                static_cast<CGameSprite*>(pObj)->JumpToPoint(
                    m_curAction.m_dest, TRUE);
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x117) {
        // 0x117 = AdvanceTime (ACTION.IDS 279).  Pushes m_specificID into
        // the world timer and emits a FEEDBACK_TIMEPASS note.
        g_pBaldurChitin->GetObjectGame()->GetWorldTimer()
            ->AdvanceCurrentTime(m_curAction.m_specificID);
        // TODO: CInfGame::FeedBack(DAT_008518CA, m_specificID, ?) -- the
        // binary call site pushes only 2 args; the C++ signature has an
        // extra BOOLEAN that defaults via m_bShowQuestXP fallback.
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x83) {
        // 0x83 = Kill (ACTION.IDS 131).  Resolves target, skips work if
        // the derivedStats already carry STATE_DEAD, runs the sub_761650
        // prep, builds a CGameEffectDeath (inlined default ctor: effectID
        // 13 + m_deathType 1 over the CGameEffect base ctor's zeroing) with
        // m_dwFlags 4 (instant equip-style timing) and m_sourceID set to
        // the target itself (the binary writes target.m_id, not caster's,
        // into the source slot -- preserved as-is), then queues a
        // CMessageAddEffect on the target.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
            if ((pSprite->GetDerivedStats()->m_generalState & STATE_DEAD) == 0) {
                pSprite->sub_761650();
                CGameEffectDeath* pEffect = new CGameEffectDeath();
                pEffect->m_effectAmount = 0;
                pEffect->m_dwFlags = 4;
                pEffect->m_sourceID = pObj->m_id;
                CMessage* msg = new CMessageAddEffect(pEffect, m_id, pObj->m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x102) {
        // 0x102 = RestUntilHealed (ACTION.IDS 258).  Binary asks
        // CanRestParty (passing bit 1 of action flags as the
        // ignore-encounters arg), then either toggles m_bHealPartyOnRest
        // around a RestParty call or displays the rejection strref the
        // out-param carried back, tinted green (the standard rest-feedback
        // color 0x00FF00).
        STRREF strError = 0;
        unsigned char ignoreEncounters
            = (m_curAction.GetFlags() & 0x2) != 0 ? 1 : 0;
        BOOL canRest = g_pBaldurChitin->GetObjectGame()->CanRestParty(
            strError, 0, 1, ignoreEncounters);
        if (canRest == TRUE) {
            CInfGame::m_bHealPartyOnRest = TRUE;
            g_pBaldurChitin->GetObjectGame()->RestParty(1, 1);
            CInfGame::m_bHealPartyOnRest = FALSE;
        } else {
            STR_RES strRes;
            if (g_pBaldurChitin->GetTlkTable().Fetch(strError, strRes)) {
                CString sName("");
                g_pBaldurChitin->GetBaldurMessage()->DisplayText(
                    sName,
                    strRes.szText,
                    0x00FF00,
                    0x00FF00,
                    -1,
                    m_id,
                    m_id);
            }
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x10A) {
        // 0x10A = DisplayMessage (ACTION.IDS 266, signature
        // `DisplayMessage(I:StrRef*)`).  Binary fetches the TLK string for
        // m_specificID, then queues a CBaldurMessage::DisplayText with the
        // pre-fetched CString -- this routes through the TLK-override path
        // (CBaldurMessage::DisplayTextRef would skip overrides).  Name is
        // empty, marker is the -1 "no journal entry" sentinel, both color
        // slots use the standard NPC-chat tint 0xD7C8A0.
        STR_RES strRes;
        if (g_pBaldurChitin->GetTlkTable().Fetch(m_curAction.m_specificID, strRes)) {
            CString sName("");
            g_pBaldurChitin->GetBaldurMessage()->DisplayText(
                sName,
                strRes.szText,
                0xD7C8A0,
                0xD7C8A0,
                -1,
                m_id,
                m_id);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x10E) {
        // 0x10E = ClearSpriteEffects (ACTION.IDS 270).  Binary resolves
        // the target and calls ReapplyEquipmentEffects on it -- counter-
        // intuitive name but the action's job is to drop transient effects
        // and rebuild from equipment.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                static_cast<CGameSprite*>(pObj)->ReapplyEquipmentEffects();
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x40
        || m_curAction.m_actionID == 0x41) {
        // 0x40 = UndoExplore (ACTION.IDS 64), 0x41 = Explore (ACTION.IDS 65).
        // Binary posts CMessageSetAreaExplored on self -- 0x40 with FALSE
        // (unexplore), 0x41 with TRUE.
        CMessage* msg = new CMessageSetAreaExplored(
            m_curAction.m_actionID == 0x41 ? TRUE : FALSE,
            m_id,
            m_id);
        g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x88) {
        // 0x88 = UnlockScroll (ACTION.IDS 136).  Binary case 0x88 clears
        // CScreenWorld::m_scrollLockId (sets to -1).
        g_pBaldurChitin->m_pEngineWorld->m_scrollLockId = -1;
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x99
        || m_curAction.m_actionID == 0x9A
        || m_curAction.m_actionID == 0x9B
        || m_curAction.m_actionID == 0x9D
        || m_curAction.m_actionID == 0x9E
        || m_curAction.m_actionID == 0x9F) {
        // 0x99-0x9F = ChangeEnemyAlly / ChangeGeneral / ChangeRace /
        // ChangeSpecifics / ChangeGender / ChangeAlignment (ACTION.IDS
        // 153-155, 157-159).  Binary cases 0x99-0x9F share a uniform
        // shape: resolve target, build a copy of its AI type, mutate the
        // single byte field that names the action, call SetAIType with
        // updateLive=TRUE and updateStart=FALSE, then broadcast
        // CMessageSpriteUpdate (gated on SP or host-matches-target in
        // the binary; we always broadcast since observers expect the
        // update).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
                CAIObjectType newType(pSprite->GetAIType());
                BYTE value = static_cast<BYTE>(m_curAction.m_specificID);
                switch (m_curAction.m_actionID) {
                case 0x99: newType.SetEnemyAlly(value); break;
                case 0x9A: newType.SetGeneral(value);   break;
                case 0x9B: newType.SetRace(value);      break;
                case 0x9D: newType.SetSpecific(value);  break;
                case 0x9E: newType.SetGender(value);    break;
                case 0x9F: newType.SetAlignment(value); break;
                }
                pSprite->SetAIType(newType, TRUE, FALSE);
                CMessage* msg = new CMessageSpriteUpdate(pSprite, m_id, pSprite->m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x9C) {
        // 0x9C = ChangeClass (ACTION.IDS 156).  Binary case 0x9C allocates
        // three local CAIObjectTypes and immediately destructs them
        // without any field write or SetAIType call -- the IWD2 build
        // shipped this action as an effective no-op.  Preserve that
        // behaviour (returns ACTION_DONE without mutating the target).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x103) {
        // 0x103 = SetVisualRange (ACTION.IDS 259).  Binary case 0x103
        // posts CMessage92(self, self, m_specificID) -- the message
        // class number is unresolved but the binary call site uses the
        // m_id/m_id/m_specificID triple.
        CMessage* msg = new CMessage92(
            m_id, m_id, m_curAction.m_specificID);
        g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xFE) {
        // 0xFE = SetHP (ACTION.IDS 254).  Binary case 0xfe writes
        // min(m_specificID, target.maxHP) into target.m_hitPoints if the
        // target isn't already marked dead (state bit 0x800).  We need to
        // operate on the target sprite's m_baseStats.m_hitPoints
        // (offset 0x1c into CCreatureFileHeader == sprite + 0x5C0).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
                if ((pSprite->m_baseStats.m_generalState & 0x800) == 0) {
                    LONG hp = m_curAction.m_specificID;
                    SHORT maxHP = pSprite->GetDerivedStats()->m_nMaxHitPoints;
                    if (hp > maxHP) {
                        pSprite->m_baseStats.m_hitPoints = maxHP;
                    } else {
                        pSprite->m_baseStats.m_hitPoints = static_cast<SHORT>(hp);
                    }
                }
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xBA) {
        // 0xBA = EndCredits (ACTION.IDS 186).  Binary case 0xba jumps to
        // switchD_caseD_ba which calls CScreenWorld::ReadyEndCredits(FALSE).
        g_pBaldurChitin->m_pEngineWorld->ReadyEndCredits(FALSE);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x10F) {
        // 0x10F = StopJoinRequests (ACTION.IDS 271).  Saves the current
        // ListenToJoin flag into field_595 and disables join requests,
        // marking field_596 to allow a later ResetJoinRequests to
        // restore the prior state.
        if (field_596 == 0) {
            CMultiplayerSettings* pMP =
                g_pBaldurChitin->GetObjectGame()->GetMultiplayerSettings();
            field_595 = static_cast<unsigned char>(pMP->GetListenToJoinOption());
            pMP->SetListenToJoinOption(FALSE, FALSE);
            field_596++;
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x110) {
        // 0x110 = ResetJoinRequests (ACTION.IDS 272).  Restores the
        // saved ListenToJoin flag from field_595 and clears field_596.
        CMultiplayerSettings* pMP =
            g_pBaldurChitin->GetObjectGame()->GetMultiplayerSettings();
        pMP->SetListenToJoinOption(field_595 != 0 ? TRUE : FALSE, FALSE);
        field_596 = 0;
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xE6) {
        // 0xE6 = RestParty (ACTION.IDS 230).  Binary case 0xe6 calls
        // CInfGame::RestParty(1, 1).
        g_pBaldurChitin->GetObjectGame()->RestParty(1, 1);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xAD) {
        // 0xAD = AddJournalEntry (ACTION.IDS 173).  Binary case 0xad
        // calls m_cJournal.AddEntry(m_specificID, 0).
        g_pBaldurChitin->GetObjectGame()->m_cJournal.AddEntry(
            static_cast<STRREF>(m_curAction.m_specificID), 0);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xF4) {
        // 0xF4 = DeleteJournalEntry (ACTION.IDS 244).
        g_pBaldurChitin->GetObjectGame()->m_cJournal.DeleteEntry(
            static_cast<STRREF>(m_curAction.m_specificID));
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0xF5) {
        // 0xF5 = JournalEntryDone (ACTION.IDS 245).
        g_pBaldurChitin->GetObjectGame()->m_cJournal.SetQuestDone(
            static_cast<STRREF>(m_curAction.m_specificID));
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x10C) {
        // 0x10C = ClearPartyEffects (ACTION.IDS 268).  Iterates party
        // members; for each with m_baseStats.m_flags bit 0x800 set, calls
        // ReapplyEquipmentEffects to rebuild equipment-derived state.
        CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
        SHORT nChars = pGame->GetNumCharacters();
        for (SHORT i = 0; i < nChars; ++i) {
            LONG nCharId = pGame->GetCharacterId(i);
            CGameSprite* pSprite = NULL;
            BYTE rc = pGame->GetObjectArray()->GetShare(
                nCharId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
            if (rc == CGameObjectArray::SUCCESS && pSprite != NULL) {
                if ((pSprite->m_baseStats.m_flags & 0x800) != 0) {
                    pSprite->ReapplyEquipmentEffects();
                }
                pGame->GetObjectArray()->ReleaseShare(
                    nCharId, CGameObjectArray::THREAD_ASYNCH, INFINITE);
            }
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x42) {
        // 0x42 = DayNight (ACTION.IDS 66).  Binary case 0x42 computes
        // m_specificID * byte[0x84EC0C] * byte[0x84EC0D] * byte[0x84EC0E]
        // == hours * MSEC_PER_SEC * SEC_PER_MIN * MIN_PER_HOUR
        // == hours * MSEC_PER_HOUR (15 * 60 * 5 == 4500) and feeds it
        // into CTimerWorld::AdvanceCurrentTime.
        g_pBaldurChitin->GetObjectGame()->GetWorldTimer()
            ->AdvanceCurrentTime(m_curAction.m_specificID
                * CTimerWorld::TIMESCALE_MSEC_PER_HOUR);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x11F) {
        // 0x11F = SetHPPercent (ACTION.IDS 287).  Binary case 0x11f
        // clamps m_specificID to [0,100], divides by 100 to get a
        // fraction, then:
        //   specifics2 == 0: heal-only set to max(curHP, maxHP * frac);
        //                    skips write entirely when maxHP*frac <= curHP.
        //   specifics2 != 0: scale curHP by frac.
        // Dead targets (derivedStats.generalState bit 0x800 OR
        // baseStats.m_generalState bit 0x800) are skipped.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
                CDerivedStats* pDeriv = pSprite->GetDerivedStats();
                if ((pDeriv->m_generalState & 0x800) == 0
                    && (pSprite->m_baseStats.m_generalState & 0x800) == 0) {
                    float pct = static_cast<float>(m_curAction.m_specificID);
                    if (pct > 100.0f) pct = 100.0f;
                    else if (pct < 0.0f) pct = 0.0f;
                    float frac = pct / 100.0f;
                    SHORT curHP = pSprite->m_baseStats.m_hitPoints;
                    SHORT maxHP = pDeriv->m_nMaxHitPoints;
                    if (m_curAction.GetSpecifics2() == 0) {
                        if (static_cast<float>(maxHP) * frac
                            > static_cast<float>(curHP)) {
                            pSprite->m_baseStats.m_hitPoints =
                                static_cast<SHORT>(
                                    static_cast<float>(maxHP) * frac);
                        }
                    } else {
                        pSprite->m_baseStats.m_hitPoints =
                            static_cast<SHORT>(
                                static_cast<float>(curHP) * frac);
                    }
                }
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x130) {
        // 0x130 = AddHP (binary case 0x130).  Resolves the target and,
        // unless flagged dead (state bit 0x800), adds m_specificID to
        // target.m_baseStats.m_hitPoints capped at m_nMaxHitPoints.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
                if ((pSprite->m_baseStats.m_generalState & 0x800) == 0) {
                    LONG delta = m_curAction.m_specificID;
                    SHORT curHP = pSprite->m_baseStats.m_hitPoints;
                    SHORT maxHP = pSprite->GetDerivedStats()->m_nMaxHitPoints;
                    if (curHP + delta < maxHP) {
                        pSprite->m_baseStats.m_hitPoints =
                            curHP + static_cast<SHORT>(delta);
                    } else {
                        pSprite->m_baseStats.m_hitPoints = maxHP;
                    }
                }
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x138) {
        // 0x138 = SetApparentName (binary case 0x138).  Writes the new
        // STRREF (m_specificID) into target.m_baseStats.m_apparentName
        // when it differs from the current value, then broadcasts
        // CMessageSpriteUpdate so observers refresh the displayed name.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
                DWORD newRef = static_cast<DWORD>(m_curAction.m_specificID);
                if (pSprite->m_baseStats.m_apparentName != newRef) {
                    pSprite->m_baseStats.m_apparentName = newRef;
                    CMessage* msg = new CMessageSpriteUpdate(
                        pSprite, m_id, pSprite->m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(
                        msg, FALSE);
                }
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x139) {
        // 0x139 = SetName (binary case 0x139).  Same shape as 0x138 but
        // writes target.m_baseStats.m_name (the canonical STRREF).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
                DWORD newRef = static_cast<DWORD>(m_curAction.m_specificID);
                if (pSprite->m_baseStats.m_name != newRef) {
                    pSprite->m_baseStats.m_name = newRef;
                    CMessage* msg = new CMessageSpriteUpdate(
                        pSprite, m_id, pSprite->m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(
                        msg, FALSE);
                }
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x13F) {
        // 0x13F = ForceMarkedSpell (ACTION.IDS 319).  Force-casts a
        // spell that was previously marked by a duration effect (e.g.
        // Call Lightning's periodic lightning strike).  Resolves the
        // target (the spell's designated victim) and dispatches through
        // the same FireSpell path as ReallyForceSpell.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            actionReturn = ForceSpellAction(pObj);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        } else {
            actionReturn = ACTION_DONE;
        }
    } else if (m_curAction.m_actionID == 0x13E) {
        // 0x13E = SetCreatureFlag (binary case 0x13e).  Resolves the
        // target sprite and applies AND-clear (specifics2 == 0) or
        // OR-set (specifics2 != 0) of mask m_specificID to its
        // m_baseStats.m_flags.  Returns ACTION_INTERRUPTABLE when the
        // target is unresolved (binary path LAB_00451451 -> sVar7 = -2).
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj == NULL) {
            actionReturn = ACTION_INTERRUPTABLE;
        } else {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
                DWORD mask = static_cast<DWORD>(m_curAction.m_specificID);
                if (m_curAction.GetSpecifics2() == 0) {
                    pSprite->m_baseStats.m_flags &= ~mask;
                } else {
                    pSprite->m_baseStats.m_flags |= mask;
                }
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
            actionReturn = ACTION_DONE;
        }
    } else if (m_curAction.m_actionID == 0x145) {
        // 0x145 = SetAreaFlag (binary case 0x145).  Three-way bit-op on
        // m_pArea->m_header.m_flags selected by specifics2:
        //   1 -> AND  (clear masked bits)
        //   2 -> OR   (set masked bits)
        //   3 -> XOR  (toggle masked bits)
        //   other -> ACTION_INTERRUPTABLE.
        if (m_pArea != NULL) {
            DWORD mask = static_cast<DWORD>(m_curAction.m_specificID);
            switch (m_curAction.GetSpecifics2()) {
            case 1: m_pArea->m_header.m_flags &= mask; break;
            case 2: m_pArea->m_header.m_flags |= mask; break;
            case 3: m_pArea->m_header.m_flags ^= mask; break;
            default: actionReturn = ACTION_INTERRUPTABLE; break;
            }
            if (actionReturn != ACTION_INTERRUPTABLE) {
                actionReturn = ACTION_DONE;
            }
        } else {
            actionReturn = ACTION_INTERRUPTABLE;
        }
    } else if (m_curAction.m_actionID == 0x12F) {
        // 0x12F = SetRestEncounterChance (ACTION.IDS 303).  Binary case
        // 0x12f caps both probabilities at 100, writes them into the
        // area's rest-encounter header, and posts CMessageSetAreaRestEncounter
        // when the caster is in control.
        if (m_pArea != NULL) {
            CAreaFileRestEncounter* pRest = m_pArea->GetHeaderRestEncounter();
            if (pRest != NULL) {
                LONG dayProb = m_curAction.GetSpecifics();
                if (dayProb > 100) dayProb = 100;
                LONG nightProb = m_curAction.GetSpecifics2();
                if (nightProb > 100) nightProb = 100;
                pRest->m_probDay = static_cast<WORD>(dayProb);
                pRest->m_probNight = static_cast<WORD>(nightProb);
                if (InControl()) {
                    CMessage* msg = new CMessageSetAreaRestEncounter(
                        pRest->m_probDay,
                        pRest->m_probNight,
                        m_id,
                        m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(
                        msg, FALSE);
                }
            }
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x12D) {
        // 0x12D = SetExtendedNight (ACTION.IDS 301).  Binary case 0x12d
        // toggles bit 0x40 of m_pArea->m_header.m_areaType based on
        // m_specificID (0 clears, nonzero sets) then propagates the new
        // areaType to the area's CInfinity render.
        if (m_pArea != NULL) {
            WORD areaType = m_pArea->m_header.m_areaType;
            if (m_curAction.m_specificID == 0) {
                areaType &= 0xFFBF;
            } else {
                areaType |= 0x40;
            }
            m_pArea->m_header.m_areaType = areaType;
            CInfinity* pInfinity = m_pArea->GetInfinity();
            if (pInfinity != NULL) {
                pInfinity->SetAreaType(areaType);
            }
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x11B) {
        // 0x11B = SetCriticalPathObject (ACTION.IDS 283).  Binary case
        // 0x11b toggles bit 0x2000 of the target sprite's
        // m_baseStats.m_flags based on m_specificID (0 clears, nonzero
        // sets), then posts CMessageSpriteUpdate in SP or matching-host
        // mode.  We perform the bit toggle; the SpriteUpdate broadcast
        // is a TODO until the MP-host comparison helper is recovered.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
        if (pObj != NULL) {
            if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
                CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
                if (m_curAction.m_specificID == 0) {
                    pSprite->m_baseStats.m_flags &= ~0x2000u;
                } else {
                    pSprite->m_baseStats.m_flags |= 0x2000u;
                }
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x120) {
        // 0x120 = SetDoorFlag (ACTION.IDS 288).  Binary case 0x120 reads
        // the resolved door's flags, OR-merges (when specifics2!=0) or
        // AND-NOT-clears (when 0) bit-mask specifics1, then writes back
        // and posts CMessageDoorStatus.
        CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_DOOR);
        if (pObj != NULL) {
            if (pObj->GetObjectType() == CGameObject::TYPE_DOOR) {
                CGameDoor* pDoor = static_cast<CGameDoor*>(pObj);
                DWORD flags = pDoor->GetFlags();
                DWORD mask = static_cast<DWORD>(m_curAction.GetSpecifics());
                if (m_curAction.GetSpecifics2() != 0) {
                    flags |= mask;
                } else {
                    flags &= ~mask;
                }
                pDoor->SetFlags(flags);
                CMessage* msg = new CMessageDoorStatus(pDoor, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            }
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x129) {
        // 0x129 = ScreenShake (ACTION.IDS 297).  Binary case 0x129 posts
        // CMessageScreenShake(duration=specifics1, magX=specifics2,
        // magY=specifics3, bOverride=TRUE) on self.
        CMessage* msg = new CMessageScreenShake(
            static_cast<WORD>(m_curAction.GetSpecifics()),
            static_cast<CHAR>(m_curAction.GetSpecifics2()),
            static_cast<CHAR>(m_curAction.GetSpecifics3()),
            TRUE,
            m_id,
            m_id);
        g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x111
        || m_curAction.m_actionID == 0x112) {
        // 0x111 = HideGUI (ACTION.IDS 273), 0x112 = UnhideGUI (274).
        // In SP the binary calls CScreenWorld::HideInterface / UnhideInterface
        // directly; in MP it posts CMessageToggleInterface.  We take the MP
        // path always -- the message handler routes to the same screen calls
        // and avoids the SP-only auto-hide bookkeeping branch.
        BOOLEAN bHide = m_curAction.m_actionID == 0x111 ? TRUE : FALSE;
        CMessage* msg = new CMessageToggleInterface(bHide, m_id, m_id);
        g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x113) {
        // 0x113 = SaveGame(I:STRREF*) (ACTION.IDS 275).  Binary case 0x451282
        // only posts the message on SP or the MP host; clients no-op.
        if (!g_pChitin->cNetwork.GetSessionOpen()
            || g_pChitin->cNetwork.GetSessionHosting() == TRUE) {
            CMessage* msg = new CMessageSaveGame(
                static_cast<STRREF>(m_curAction.GetSpecifics()),
                m_id,
                m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x11E) {
        // 0x11E = AllowAreaResting (ACTION.IDS 286).  Binary case 0x11e
        // toggles the no-rest bit (0x2) in m_pArea->m_header.m_flags --
        // m_specificID == 0 sets it (disallow), nonzero clears it (allow).
        if (m_pArea != NULL) {
            if (m_curAction.m_specificID == 0) {
                m_pArea->m_header.m_flags |= 2;
            } else {
                m_pArea->m_header.m_flags &= ~2u;
            }
        }
        actionReturn = ACTION_DONE;
    } else if (m_curAction.m_actionID == 0x74
        || m_curAction.m_actionID == 0xBC
        || m_curAction.m_actionID == 0xC1
        || m_curAction.m_actionID == 0xCC) {
        // 0x74 = TakePartyItem (ACTION.IDS 116), 0xBC = TakePartyItemAll (188),
        // 0xC1 = TakePartyItemRange (193), 0xCC = TakePartyItemNum (204).  All
        // four share case 0x22 of the dispatch byte table at 0x4529AC.
        actionReturn = TakePartyItem();
    } else if (m_curAction.m_actionID == 0xDC
        || m_curAction.m_actionID == 0xE2) {
        // 0xDC = 220, 0xE2 = 226.  Both share case 0x5C of the dispatch byte
        // table at 0x4529AC and neither appears in IWD2's ACTION.IDS, which
        // has no entry between 205 and 228; 226 is the counted variant.
        actionReturn = TakePartyItemList();
    } else if (m_curAction.m_actionID == 0x79
        || m_curAction.m_actionID == 0x7A) {
        // 0x79 = StartCutSceneMode (ACTION.IDS 121),
        // 0x7A = EndCutSceneMode  (ACTION.IDS 122).  Binary posts
        // CMessageCutSceneModeStatus -- 0x79 with TRUE, 0x7A with FALSE.
        CMessage* msg = new CMessageCutSceneModeStatus(
            m_curAction.m_actionID == 0x79 ? TRUE : FALSE,
            m_id,
            m_id);
        g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
        actionReturn = ACTION_DONE;
    }

    SetLastActionReturn(actionReturn);
    return actionReturn;
}

// 0x45C300
void CGameAIBase::InsertResponse(CAIResponse& response, BOOL checkCurrentResponse, BOOL clearActions)
{
    CAIAction action;
    CAIObjectType actorType;

    if (checkCurrentResponse
        && m_curResponseSetNum >= 0
        && m_curScriptNum >= 0
        && m_curResponseSetNum == response.m_responseSetNum
        && m_curScriptNum == response.m_scriptNum) {
        return;
    }

    if (clearActions) {
        ClearActions(FALSE);
    }

    m_curResponseNum = response.m_responseNum;
    m_curResponseSetNum = response.m_responseSetNum;
    m_curScriptNum = response.m_scriptNum;
    m_interrupt = TRUE;

    POSITION pos = response.m_actionList.GetHeadPosition();
    while (pos != NULL) {
        CAIAction* node = response.m_actionList.GetNext(pos);
        action = *node;

        CAIAction* newNode = new CAIAction();
        *newNode = action;
        m_queuedActions.AddTail(newNode);
    }
}

// 0x45CA10
void CGameAIBase::ProcessAI()
{
    // TODO: 0x57 action sentinel meaning not yet identified.

    if (m_inCutScene) {
        return;
    }
    if (m_nLastActionReturn == 0) {
        return;
    }

    // TODO INCOMPLETE: the binary drives CGameAIArea queued actions outside the
    // recovered ProcessAI body. Until that caller is recovered, drain the active
    // area response before allowing a new script response to interrupt it.
    BOOL bAreaActionFallback = m_objectType == CGameObject::TYPE_AIBASE;
    if (bAreaActionFallback
        && (m_curAction.m_actionID != CAIAction::NO_ACTION || !m_queuedActions.IsEmpty())) {
        if (m_curAction.m_actionID == CAIAction::NO_ACTION) {
            CAIAction action;
            SetCurrAction(GetNextAction(action));
            ResetCurrResponse();
        }

        BOOL bCanDoAction = m_curAction.m_actionID != CAIAction::NO_ACTION;
        if (bCanDoAction && !g_pBaldurChitin->GetObjectGame()->GetWorldTimer()->m_active) {
            bCanDoAction = g_pBaldurChitin->GetObjectGame()
                               ->GetRuleTables()
                               .m_lInstantActions.Find(m_curAction.GetActionID()) != NULL;
        }

        if (bCanDoAction) {
            DoAction();
        }
        return;
    }

    CAIResponse localResponse;

    ApplyTriggers();

    CAIResponse* found = NULL;
    if (m_overrideScript != NULL) {
        found = m_overrideScript->Find(m_pendingTriggers, this);
    }

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    BOOL bPlayerControl = pGame->m_bPartyAI == FALSE
        && pGame->GetCharacterPortraitNum(m_id) != -1;
    SHORT curID = m_curAction.m_actionID;
    BOOL bBypassChain = bPlayerControl
        || curID == 0x1B
        || curID == 0x57
        || (curID == CAIAction::MOVETOPOINT && m_curAction.m_specificID3 == 1);

    if (bBypassChain) {
        if (found != NULL) {
            if (!found->m_actionList.IsEmpty()) {
                found->m_scriptNum = 0;
                InsertResponse(*found, TRUE, TRUE);

                POSITION pos = m_pendingTriggers.GetHeadPosition();
                while (pos != NULL) {
                    CAITrigger* pTrigger = m_pendingTriggers.GetNext(pos);
                    if (pTrigger != NULL) {
                        delete pTrigger;
                    }
                }
                m_pendingTriggers.RemoveAll();
            }
            delete found;
            return;
        }
    }

    BOOL bTryNextScript = TRUE;
    if (found != NULL) {
        localResponse.Add(*found);
        BOOL hasContinue = localResponse.InListEnd(CAIAction::CONTINUE);
        bTryNextScript = found->m_actionList.IsEmpty() || hasContinue;
    }
    if (found != NULL) {
        delete found;
        found = NULL;
    }

    CAIScript* chain[6] = {
        m_special1Script,
        m_teamScript,
        m_special2Script,
        m_combatScript,
        m_special3Script,
        m_movementScript,
    };

    SHORT scriptLevel = 0;
    for (int i = 0; i < 6 && bTryNextScript; ++i) {
        scriptLevel = (SHORT)(i + 1);
        if (chain[i] == NULL || chain[i]->IsEmpty()) {
            continue;
        }
        found = chain[i]->Find(m_pendingTriggers, this);
        if (found == NULL) {
            continue;
        }
        localResponse.Add(*found);
        BOOL hasContinue = localResponse.InListEnd(CAIAction::CONTINUE);
        if (!found->m_actionList.IsEmpty() && !hasContinue) {
            bTryNextScript = FALSE;
        }
        delete found;
        found = NULL;
    }

    if (!localResponse.m_actionList.IsEmpty()) {
        localResponse.m_scriptNum = scriptLevel;
        InsertResponse(localResponse, TRUE, TRUE);

        POSITION pos = m_pendingTriggers.GetHeadPosition();
        while (pos != NULL) {
            CAITrigger* pTrigger = m_pendingTriggers.GetNext(pos);
            if (pTrigger != NULL) {
                delete pTrigger;
            }
        }
        m_pendingTriggers.RemoveAll();
    }
    if (found != NULL) {
        delete found;
    }

    if (bAreaActionFallback && m_curAction.m_actionID == CAIAction::NO_ACTION && !m_queuedActions.IsEmpty()) {
        CAIAction action;
        SetCurrAction(GetNextAction(action));
        ResetCurrResponse();
    }

    BOOL bCanDoAreaAction = bAreaActionFallback && m_curAction.m_actionID != CAIAction::NO_ACTION;
    if (bCanDoAreaAction && !g_pBaldurChitin->GetObjectGame()->GetWorldTimer()->m_active) {
        bCanDoAreaAction = g_pBaldurChitin->GetObjectGame()
                               ->GetRuleTables()
                               .m_lInstantActions.Find(m_curAction.GetActionID()) != NULL;
    }

    if (bCanDoAreaAction) {
        DoAction();
    }
}

// 0x45D130
void CGameAIBase::RemoveFromArea()
{
    CGameObject::RemoveFromArea();

    BYTE rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(m_id,
        CGameObjectArray::THREAD_ASYNCH,
        NULL,
        INFINITE);
    if (rc != CGameObjectArray::SUCCESS) {
        // __FILE__: C:\Projects\Icewind2\src\Baldur\CGameAIBase.cpp
        // __LINE__: 6886
        UTIL_ASSERT(FALSE);
    }

    delete this;
}

// 0x44CC70
void CGameAIBase::AddAction(const CAIAction& action)
{
    CAIAction* copy = new CAIAction();
    copy->m_actionID = action.m_actionID;
    copy->m_specificID = action.m_specificID;
    copy->m_actorID.Set(action.m_actorID);
    copy->m_acteeID.Set(action.m_acteeID);
    copy->m_acteeID2.Set(action.m_acteeID2);
    copy->m_dest = action.m_dest;
    copy->m_specificID2 = action.m_specificID2;
    copy->m_specificID3 = action.m_specificID3;
    copy->m_string1 = action.m_string1;
    copy->m_string2 = action.m_string2;
    copy->m_internalFlags = action.m_internalFlags;
    m_queuedActions.AddHead(copy);
}

// 0x44CE20
void CGameAIBase::AddEffect(CGameEffect* pEffect, BYTE list, BOOL noSave, BOOL immediateApply)
{
    // The base handler for the non-sprite CGameAIBase leaves -- doors, containers,
    // triggers, regions, the AI-area/-game singletons (the binary COMDAT-folds the
    // eight identical overrides into one body). Those objects cannot hold effects,
    // so an effect delivered to them is simply discarded. CGameSprite overrides this
    // virtual (slot 0x78, 0x733050) with the real timed/equipped list management, so
    // a sprite victim never reaches here. The earlier stopgap wrongly ran the sprite
    // list path in this base, static_cast-ing a non-sprite to CGameSprite and
    // AddTail-ing onto a garbage list -- the Fireball-on-non-sprite heap fault
    // (GatherTargets(ANYONE) strikes any blast object; the original discards here).
    if (pEffect != NULL) {
        delete pEffect;
    }
}

// 0x44CE40
void CGameAIBase::ClearAI(BOOLEAN a1)
{
    ClearActions(FALSE);
    ApplyTriggers();

    POSITION pos = m_pendingTriggers.GetHeadPosition();
    while (pos != NULL) {
        CAITrigger* pTrigger = m_pendingTriggers.GetNext(pos);
        if (pTrigger != NULL) {
            delete pTrigger;
        }
    }
    m_pendingTriggers.RemoveAll();

    SetCurrAction(CAIAction::NULL_ACTION);

    m_curAction.m_actionID = CAIAction::NULL_ACTION.m_actionID;
    m_curAction.m_specificID = CAIAction::NULL_ACTION.m_specificID;
    m_curAction.m_actorID.Set(CAIAction::NULL_ACTION.m_actorID);
    m_curAction.m_acteeID.Set(CAIAction::NULL_ACTION.m_acteeID);
    m_curAction.m_acteeID2.Set(CAIAction::NULL_ACTION.m_acteeID2);
    m_curAction.m_dest = CAIAction::NULL_ACTION.m_dest;
    m_curAction.m_specificID2 = CAIAction::NULL_ACTION.m_specificID2;
    m_curAction.m_specificID3 = CAIAction::NULL_ACTION.m_specificID3;
    m_curAction.m_string1 = CAIAction::NULL_ACTION.m_string1;
    m_curAction.m_string2 = CAIAction::NULL_ACTION.m_string2;
    m_curAction.m_internalFlags = CAIAction::NULL_ACTION.m_internalFlags;
}

// 0x44CF50
void CGameAIBase::InsertAction(const CAIAction& action)
{
    CAIAction* copy = new CAIAction();
    copy->m_actionID = action.m_actionID;
    copy->m_specificID = action.m_specificID;
    copy->m_actorID.Set(action.m_actorID);
    copy->m_acteeID.Set(action.m_acteeID);
    copy->m_acteeID2.Set(action.m_acteeID2);
    copy->m_dest = action.m_dest;
    copy->m_specificID2 = action.m_specificID2;
    copy->m_specificID3 = action.m_specificID3;
    copy->m_string1 = action.m_string1;
    copy->m_string2 = action.m_string2;
    copy->m_internalFlags = action.m_internalFlags;
    m_queuedActions.AddTail(copy);
}

// 0x45D190
void CGameAIBase::SetCurrAction(const CAIAction& action)
{
    m_actionCount = 0;
    m_interrupt = FALSE;
    m_curAction.m_actionID = action.m_actionID;
    m_curAction.m_specificID = action.m_specificID;
    m_curAction.m_actorID.Set(action.m_actorID);
    m_curAction.m_acteeID.Set(action.m_acteeID);
    m_curAction.m_acteeID2.Set(action.m_acteeID2);
    m_curAction.m_dest = action.m_dest;
    m_curAction.m_specificID2 = action.m_specificID2;
    m_curAction.m_specificID3 = action.m_specificID3;
    m_curAction.m_string1 = action.m_string1;
    m_curAction.m_string2 = action.m_string2;
    m_curAction.m_internalFlags = action.m_internalFlags;
    if (action.m_actionID != CAIAction::NO_ACTION
        && g_pBaldurChitin->GetObjectGame()->GetRuleTables().m_lNoDecodeList.Find(action.m_actionID) == NULL) {
        m_curAction.Decode(this);
    }
}

// 0x45D280
void CGameAIBase::SetScript(SHORT level, CAIScript* script)
{
    switch (level) {
    case 0:
        if (m_overrideScript != NULL) {
            delete m_overrideScript;
        }
        m_overrideScript = script;
        break;
    case 1:
        if (m_special1Script != NULL) {
            delete m_special1Script;
        }
        m_special1Script = script;
        break;
    case 2:
        if (m_teamScript != NULL) {
            delete m_teamScript;
        }
        m_teamScript = script;
        break;
    case 3:
        if (m_special2Script != NULL) {
            delete m_special2Script;
        }
        m_special2Script = script;
        break;
    case 4:
        if (m_combatScript != NULL) {
            delete m_combatScript;
        }
        m_combatScript = script;
        break;
    case 5:
        if (m_special3Script != NULL) {
            delete m_special3Script;
        }
        m_special3Script = script;
        break;
    case 6:
        if (m_movementScript != NULL) {
            delete m_movementScript;
        }
        m_movementScript = script;
        break;
    }
}

// 0x45D6A0
void CGameAIBase::ApplyTriggers()
{
    CMessage* message;

    if ((g_pBaldurChitin->GetObjectGame()->GetWorldTimer()->m_gameTime / 4) % 900 == 0) {
        message = new CMessageUpdateReaction(11, m_id, m_id);
        g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
    }

    POSITION pos = m_pendingTriggers.GetHeadPosition();
    while (pos != NULL) {
        CAITrigger* pTrigger = m_pendingTriggers.GetNext(pos);
        if ((pTrigger->m_flags & 0x4) == 0) {
            pTrigger->m_flags |= 0x4;

            switch (pTrigger->m_triggerID) {
            case CAITRIGGER_ATTACKEDBY:
                AutoPause(2);
                if (!m_lAttacker.Equal(pTrigger->GetCause())
                    && (g_pBaldurChitin->GetObjectGame()->GetCharacterPortraitNum(m_id) != -1
                        || g_pBaldurChitin->GetObjectGame()->GetCharacterPortraitNum(pTrigger->GetCause().GetInstance()) != -1)) {
                    m_lAttacker.Set(pTrigger->GetCause());
                    message = new CMessageSetLastObject(pTrigger->GetCause(),
                        CAITRIGGER_ATTACKEDBY,
                        m_id,
                        m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
                }
                break;
            case CAITRIGGER_HELP:
                if (!m_lHelp.Equal(pTrigger->GetCause())) {
                    m_lHelp.Set(pTrigger->GetCause());
                    message = new CMessageSetLastObject(pTrigger->GetCause(),
                        CAITRIGGER_HELP,
                        m_id,
                        m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
                }
                break;
            case CAITRIGGER_RECEIVEDORDER:
                if (!m_lOrderedBy.Equal(pTrigger->GetCause())) {
                    m_lOrderedBy.Set(pTrigger->GetCause());
                    message = new CMessageSetLastObject(pTrigger->GetCause(), 6, m_id, m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
                }
                break;
            case CAITRIGGER_SAID:
                if (!m_lTalkedTo.Equal(pTrigger->GetCause())) {
                    m_lTalkedTo.Set(pTrigger->GetCause());
                    message = new CMessageSetLastObject(pTrigger->GetCause(),
                        CAITRIGGER_SAID,
                        m_id,
                        m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
                }
                break;
            case CAITRIGGER_HITBY:
                m_lAttackStyle = pTrigger->m_specificID;
                if (!m_lHitter.Equal(pTrigger->GetCause())) {
                    m_lHitter.Set(pTrigger->GetCause());
                    message = new CMessageSetLastObject(pTrigger->GetCause(),
                        CAITRIGGER_HITBY,
                        m_id,
                        m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
                }
                break;
            case CAITRIGGER_HEARD:
                if (!m_lHeard.Equal(pTrigger->GetCause())) {
                    m_lHeard.Set(pTrigger->GetCause());
                    message = new CMessageSetLastObject(pTrigger->GetCause(),
                        CAITRIGGER_HEARD,
                        m_id,
                        m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
                }
                break;
            }

            if (g_pBaldurChitin->GetObjectGame()->m_saveObjectList.Find(pTrigger->m_triggerID) != NULL
                && !m_lTrigger.Equal(pTrigger->GetCause())) {
                m_lTrigger.Set(pTrigger->GetCause());
                message = new CMessageSetLastObject(pTrigger->GetCause(), 0, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
            }
        }
    }
}

// 0x45DED0
BOOL CGameAIBase::CheckAppropriateTarget(CSpell* pSpell, CGameAIBase* pTarget)
{

    pSpell->Demand();
    if (pTarget->GetObjectType() != TYPE_SPRITE) {
        pSpell->Release();
        return FALSE;
    }

    SPELL_ABILITY* ability = pSpell->GetAbility(0);
    if (ability != NULL
        && (ability->actionType != 1
            || (static_cast<CGameSprite*>(pTarget)->GetDerivedStats()->m_generalState & 0x800) == 0
            || pSpell->GetGenericName() == 12117
            || pSpell->GetGenericName() == 25765
            || pSpell->GetGenericName() == 32393)) {
        pSpell->Release();
        return TRUE;
    }

    pSpell->Release();
    return FALSE;
}

// 0x45DF70
BOOL CGameAIBase::PartyHasItem(const CResRef& resRef)
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    for (SHORT nPortrait = 0; nPortrait < pGame->GetNumCharacters(); nPortrait++) {
        LONG nCharacterId = pGame->GetCharacterId(nPortrait);

        CGameSprite* pSprite;
        BYTE rc = pGame->GetObjectArray()->GetShare(nCharacterId,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSprite),
            INFINITE);
        if (rc != CGameObjectArray::SUCCESS) {
            break;
        }

        CString sName;
        resRef.CopyToString(sName);

        if (pSprite->FindItemPersonal(sName, 0, FALSE) != -1) {
            pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
            return TRUE;
        }

        if (pSprite->FindItemBags(sName, 0, FALSE) != -1) {
            pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
            return TRUE;
        }

        pGame->GetObjectArray()->ReleaseShare(nCharacterId,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }
    return FALSE;
}

// 0x45E100
void CGameAIBase::ApplyEffectToParty(CGameEffect* pEffect)
{
    CMessage* message;
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    for (SHORT nPortrait = 0; nPortrait < pGame->GetNumCharacters(); nPortrait++) {
        LONG nCharacterId = pGame->GetCharacterId(nPortrait);
        if (pGame->GetGameSave()->m_bArenaMode) {
            CGameSprite* pSprite;
            BYTE rc = pGame->GetObjectArray()->GetShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
            if (rc == CGameObjectArray::SUCCESS) {
                if (pSprite->InControl()) {
                    pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                    message = new CMessageAddEffect(pEffect, m_id, nCharacterId);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
                } else {
                    pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                }
            }
        } else {
            message = new CMessageAddEffect(pEffect, m_id, nCharacterId);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
        }
    }
}

// 0x4C83F0
SHORT CGameAIBase::GiveItemCreate(CGameSprite* pTarget)
{
    // Unrecovered.  Transfers this object's items to pTarget: when this is a
    // container (TYPE_CONTAINER) it drains the container's contents, and when
    // this is a sprite (TYPE_SPRITE) it hands over the relevant inventory,
    // delivering each item with a CMessageContainerAddItem (target container)
    // or the give-to-sprite message.  The GiveItemCreate action (ExecuteAction)
    // builds the items into a temporary container, then calls this to deliver
    // them to the action's target.  Body left unimplemented.
    return ACTION_DONE;
}

// 0x799E60
void CGameAIBase::AutoPause(DWORD type)
{
}

// 0x49FC40
BOOL CGameAIBase::GetCanSeeInvisible()
{
    return TRUE;
}

// 0x45F2A0
SHORT CGameAIBase::MoveView(CPoint dest, int speed)
{
    CGameArea* pVisibleArea = g_pBaldurChitin->GetObjectGame()->GetVisibleArea();

    INT x;
    INT y;
    pVisibleArea->GetInfinity()->GetViewPosition(x, y);

    if (dest.x < 0) {
        dest.x = 0;
    }

    if (dest.y < 0) {
        dest.y = 0;
    }

    int maxX = pVisibleArea->GetInfinity()->rViewPort.left
        - pVisibleArea->GetInfinity()->rViewPort.right
        + pVisibleArea->GetInfinity()->nAreaX;
    if (dest.x > maxX) {
        dest.x = maxX;
    }

    int maxY = pVisibleArea->GetInfinity()->rViewPort.top
        - pVisibleArea->GetInfinity()->rViewPort.bottom
        + pVisibleArea->GetInfinity()->nAreaY;
    if (dest.y > maxY) {
        dest.y = maxY;
    }

    if (m_curAction.m_actionID == CAIACTION_MOVEVIEWPOINTUNTILDONE) {
        if (!field_594) {
            CMessageStartScroll* pMessage = new CMessageStartScroll(pVisibleArea,
                CPoint(x, y),
                dest,
                static_cast<BYTE>(speed),
                m_id,
                m_id);

            g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

            field_594 = TRUE;
        }

        if (x != dest.x || y != dest.y) {
            return ACTION_NORMAL;
        }

        field_594 = FALSE;
    } else {
        CMessageStartScroll* pMessage = new CMessageStartScroll(pVisibleArea,
            CPoint(x, y),
            dest,
            static_cast<BYTE>(speed),
            m_id,
            m_id);

        g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
    }

    return ACTION_DONE;
}

// 0x45F5D0
SHORT CGameAIBase::MoveViewPoint()
{
    CPoint dest(m_curAction.m_dest);

    CRect viewPort(GetArea()->GetInfinity()->rViewPort);
    dest.x -= viewPort.Width() / 2;
    dest.y -= viewPort.Height() / 2;

    if (dest.x < 0) {
        dest.x = 0;
    }

    if (dest.x > GetArea()->GetInfinity()->nAreaX) {
        dest.x = GetArea()->GetInfinity()->nAreaX;
    }

    if (dest.y < 0) {
        dest.y = 0;
    }

    if (dest.y > GetArea()->GetInfinity()->nAreaY) {
        dest.y = GetArea()->GetInfinity()->nAreaY;
    }

    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    return MoveView(dest, speed);
}

// 0x45F660
SHORT CGameAIBase::MoveViewObject(CGameObject* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    CPoint dest(target->GetPos());

    CRect viewPort(GetArea()->GetInfinity()->rViewPort);
    dest.x -= viewPort.Width() / 2;
    dest.y -= viewPort.Height() / 2;

    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    return MoveView(dest, speed);
}

// 0x45F6D0
SHORT CGameAIBase::MoveCursor(CPoint dest, SHORT speed)
{
    CInfinity* pInfinity = GetArea()->GetInfinity();

    INT x;
    INT y;
    pInfinity->GetViewPosition(x, y);

    CPoint pt;
    GetCursorPos(&pt);

    const SHORT nCursorStep = 4;
    const SHORT nDiagonalStep = static_cast<SHORT>(3 * nCursorStep / 4);
    const SHORT nDiagonalTolerance = static_cast<SHORT>(nDiagonalStep * speed);

    if (m_curAction.m_specificID == 0) {
        pInfinity->SetViewPosition(dest.x, dest.y, TRUE);
        return ACTION_DONE;
    }

    const SHORT nStraightStep = static_cast<SHORT>(nCursorStep * speed);
    const SHORT nHalfStep = static_cast<SHORT>(nCursorStep / 2 * speed);
    const INT dx = dest.x - pt.x;
    const INT dy = dest.y - pt.y;

    if (dx <= nStraightStep) {
        if (dx >= -nStraightStep
            && dy <= nDiagonalTolerance
            && dy >= -nDiagonalTolerance) {
            return ACTION_DONE;
        }

        if (dx >= -nStraightStep) {
            if (dy > 0) {
                pt.y += nDiagonalStep * speed;
            } else if (dy < 0) {
                pt.y -= nDiagonalStep * speed;
            }

            pInfinity->SetViewPosition(pt.x, pt.y, TRUE);
            return ACTION_INTERRUPTABLE;
        }
    }

    if (dx > 0 && dy <= 6 && dy >= -nDiagonalTolerance) {
        pt.x += nCursorStep * speed;
    } else if (dx < 0 && dy <= 6 && dy >= -nDiagonalTolerance) {
        pt.x -= nCursorStep * speed;
    } else if (dx > 0 && dy > 0) {
        pt.x += nDiagonalStep * speed;
        pt.y += nHalfStep;
    } else if (dx < 0 && dy > 0) {
        pt.x -= nDiagonalStep * speed;
        pt.y += nHalfStep;
    } else if (dx > 0 && dy < 0) {
        pt.x += nDiagonalStep * speed;
        pt.y -= nHalfStep;
    } else if (dx < 0 && dy < 0) {
        pt.x -= nDiagonalStep * speed;
        pt.y -= nHalfStep;
    }

    pInfinity->SetViewPosition(pt.x, pt.y, TRUE);

    return ACTION_INTERRUPTABLE;
}

// 0x45F900
SHORT CGameAIBase::MoveCursorPoint()
{
    CPoint dest = m_curAction.m_dest;
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    return MoveCursor(dest, speed);
}

// NOTE: Inlined.
SHORT CGameAIBase::ClickLButton(CPoint pt)
{
    g_pBaldurChitin->m_pEngineWorld->OnLButtonDown(pt);
    g_pBaldurChitin->m_pEngineWorld->OnLButtonUp(pt);
    return ACTION_DONE;
}

// 0x45F920
SHORT CGameAIBase::ClickLButtonPoint()
{
    CPoint dest = m_curAction.m_dest;
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    SHORT moveReturn = MoveCursor(dest, speed);
    if (moveReturn != ACTION_DONE) {
        return moveReturn;
    }

    // NOTE: Uninline.
    return ClickLButton(dest);
}

// 0x45F980
SHORT CGameAIBase::ClickLButtonObject(CGameObject* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    CPoint dest = target->GetPos();
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    SHORT moveReturn = MoveCursor(dest, speed);
    if (moveReturn != ACTION_DONE) {
        return moveReturn;
    }

    // NOTE: Uninline.
    return ClickLButton(dest);
}

// NOTE: Inlined.
SHORT CGameAIBase::DoubleClickLButton(CPoint pt)
{
    g_pBaldurChitin->m_pEngineWorld->OnLButtonDown(pt);
    g_pBaldurChitin->m_pEngineWorld->OnLButtonUp(pt);
    g_pBaldurChitin->m_pEngineWorld->OnLButtonDblClk(pt);
    g_pBaldurChitin->m_pEngineWorld->OnLButtonUp(pt);
    return ACTION_DONE;
}

// 0x45F9F0
SHORT CGameAIBase::DoubleClickLButtonPoint()
{
    CPoint dest = m_curAction.m_dest;
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    SHORT moveReturn = MoveCursor(dest, speed);
    if (moveReturn != ACTION_DONE) {
        return moveReturn;
    }

    // NOTE: Uninline.
    return DoubleClickLButton(dest);
}

// 0x45FA70
SHORT CGameAIBase::DoubleClickLButtonObject(CGameObject* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    CPoint dest = target->GetPos();
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    SHORT moveReturn = MoveCursor(dest, speed);
    if (moveReturn != ACTION_DONE) {
        return moveReturn;
    }

    // NOTE: Uninline.
    return DoubleClickLButton(dest);
}

// NOTE: Inlined.
SHORT CGameAIBase::ClickRButton(CPoint pt)
{
    g_pBaldurChitin->m_pEngineWorld->OnRButtonDown(pt);
    g_pBaldurChitin->m_pEngineWorld->OnRButtonUp(pt);
    return ACTION_DONE;
}

// 0x45FB10
SHORT CGameAIBase::ClickRButtonPoint()
{
    CPoint dest = m_curAction.m_dest;
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    SHORT moveReturn = MoveCursor(dest, speed);
    if (moveReturn != ACTION_DONE) {
        return moveReturn;
    }

    // NOTE: Uninline.
    return ClickRButton(dest);
}

// 0x45FB70
SHORT CGameAIBase::ClickRButtonObject(CGameObject* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    CPoint dest = target->GetPos();
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    SHORT moveReturn = MoveCursor(dest, speed);
    if (moveReturn != ACTION_DONE) {
        return moveReturn;
    }

    // NOTE: Uninline.
    return ClickRButton(dest);
}

// NOTE: Inlined.
SHORT CGameAIBase::DoubleClickRButton(CPoint pt)
{
    g_pBaldurChitin->m_pEngineWorld->OnRButtonDown(pt);
    g_pBaldurChitin->m_pEngineWorld->OnRButtonUp(pt);
    g_pBaldurChitin->m_pEngineWorld->OnRButtonDblClk(pt);
    g_pBaldurChitin->m_pEngineWorld->OnRButtonUp(pt);
    return ACTION_DONE;
}

// 0x45FBF0
SHORT CGameAIBase::DoubleClickRButtonPoint()
{
    CPoint dest = m_curAction.m_dest;
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    SHORT moveReturn = MoveCursor(dest, speed);
    if (moveReturn != ACTION_DONE) {
        return moveReturn;
    }

    // NOTE: Uninline.
    return DoubleClickRButton(dest);
}

// 0x45FC80
SHORT CGameAIBase::DoubleClickRButtonObject(CGameObject* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    CPoint dest = target->GetPos();
    SHORT speed = static_cast<SHORT>(m_curAction.m_specificID);

    SHORT moveReturn = MoveCursor(dest, speed);
    if (moveReturn != ACTION_DONE) {
        return moveReturn;
    }

    // NOTE: Uninline.
    return DoubleClickRButton(dest);
}

// 0x45FD20
SHORT CGameAIBase::ChangeAIScript()
{
    CAIScript* script = new CAIScript(CResRef(m_curAction.GetString1()));
    SetScript(static_cast<SHORT>(m_curAction.m_specificID), script);

    if (GetObjectType() == TYPE_CONTAINER) {
        static_cast<CGameContainer*>(this)->SetScriptRes(m_curAction.GetString1());
    }

    return ACTION_DONE;
}

// 0x45FED0
SHORT CGameAIBase::StartTimer()
{
    BYTE id = static_cast<BYTE>(m_curAction.m_specificID);
    LONG time = m_curAction.m_specificID2;

    POSITION pos = m_timers.GetHeadPosition();
    while (pos != NULL) {
        CGameTimer* pTimer = m_timers.GetNext(pos);
        if (pTimer->m_id == id) {
            pTimer->m_time = time;
            return ACTION_DONE;
        }
    }

    CGameTimer* pTimer = new CGameTimer();
    pTimer->m_time = time;
    pTimer->m_id = id;
    m_timers.AddTail(pTimer);

    return ACTION_DONE;
}

// 0x45FF40
SHORT CGameAIBase::SendTrigger(CGameAIBase* sprite)
{
    if (sprite == NULL) {
        return ACTION_ERROR;
    }

    CAITrigger trigger(CAITrigger::TRIGGER, m_typeAI, m_curAction.m_specificID);

    CMessageSetTrigger* pMessage = new CMessageSetTrigger(trigger,
        m_id,
        sprite->GetId());

    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

    return ACTION_DONE;
}

// 0x4600B0
SHORT CGameAIBase::Wait()
{
    if (m_actionCount == 0) {
        m_curAction.m_specificID *= 15;
    }

    m_curAction.m_specificID--;

    if (m_curAction.m_specificID > 0) {
        return ACTION_INTERRUPTABLE;
    }

    return ACTION_DONE;
}

// 0x4600F0
SHORT CGameAIBase::SmallWait()
{
    m_curAction.m_specificID--;

    if (m_curAction.m_specificID > 0) {
        return ACTION_INTERRUPTABLE;
    }

    return ACTION_DONE;
}

// 0x460110
SHORT CGameAIBase::Shout()
{
    CTypedPtrList<CPtrList, LONG*> targets;

    SHORT range = m_curAction.m_actionID != CAIACTION_212
        ? GetVisualRange()
        : SHORT_MAX;

    m_pArea->GetAllInRange(m_pos,
        CAIObjectType::ANYONE,
        range,
        GetTerrainTable(),
        targets,
        FALSE,
        FALSE);

    CAITrigger trigger(CAITrigger::HEARD, m_typeAI, m_curAction.m_specificID);

    POSITION pos = targets.GetHeadPosition();
    while (pos != NULL) {
        LONG nId = reinterpret_cast<LONG>(targets.GetNext(pos));

        CMessage* message = new CMessageSetTrigger(trigger, m_id, nId);
        g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
    }

    return ACTION_DONE;
}

// 0x463310
void CGameAIBase::PutItemGround(CItem* pItem)
{
    LONG nContainerId = g_pBaldurChitin->GetObjectGame()->GetGroundPile(m_id);
    if (nContainerId != CGameObjectArray::INVALID_INDEX) {
        CGameContainer* pContainer;

        BYTE rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(nContainerId,
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pContainer),
            INFINITE);
        if (rc == CGameObjectArray::SUCCESS) {
            pContainer->PlaceItemInBlankSlot(pItem, TRUE, SHORT_MAX);
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(nContainerId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
    }
}

// 0x463740
SHORT CGameAIBase::ClearActions(CGameObject* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    CMessage* message = new CMessageClearActions(m_id, target->GetId());
    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);

    return ACTION_DONE;
}

// 0x45EDE0 - handles SetGlobal (0x1E) and SetGlobalRandom (0x132).
// TODO: Multiplayer broadcast (CMessage at vtable PTR_FUN_0084882c) skipped.
SHORT CGameAIBase::SetGlobal()
{
    CString sScope;
    CString sName;
    SplitScriptVariableName(m_curAction.GetString1(), sScope, sName);
    LONG nValue = m_curAction.m_specificID;

    if (m_curAction.m_actionID == 0x132) {
        LONG lo = m_curAction.m_specificID;
        LONG hi = m_curAction.m_specificID2;
        if (lo < hi) {
            nValue = rand() % (hi - lo + 1) + lo;
        } else if (lo > hi) {
            nValue = rand() % (lo - hi + 1) + hi;
        } else {
            nValue = lo;
        }
        m_curAction.m_specificID = nValue;
    }

    if (sScope == CString("GLOBAL")) {
        CVariableHash* pHash = g_pBaldurChitin->GetObjectGame()->GetVariables();
        CVariable* pVar = pHash->FindKey(sName);
        if (pVar != NULL) {
            pVar->m_intValue = nValue;
        } else {
            CVariable v;
            v.SetName(sName);
            v.m_intValue = nValue;
            pHash->AddKey(v);
        }
        return ACTION_DONE;
    }

    if (sScope == CString("LOCALS")) {
        if ((GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
            CGameSprite* pSprite = static_cast<CGameSprite*>(this);
            CVariableHash* pHash = pSprite->GetLocalVariables();
            CVariable* pVar = pHash->FindKey(sName);
            if (pVar != NULL) {
                pVar->m_intValue = nValue;
            } else {
                CVariable v;
                v.SetName(sName);
                v.m_intValue = nValue;
                pHash->AddKey(v);
            }
        }
        return ACTION_DONE;
    }

    CString sAreaName = sScope;
    if (sScope == CString("MYAREA") && m_pArea != NULL) {
        sAreaName = CResRef(reinterpret_cast<BYTE*>(m_pArea->m_header.m_areaName)).GetResRefStr();
    }

    CGameArea* pArea = g_pBaldurChitin->GetObjectGame()->GetArea(sAreaName);
    if (pArea != NULL) {
        CVariableHash* pHash = pArea->GetVariables();
        CVariable* pVar = pHash->FindKey(sName);
        if (pVar != NULL) {
            pVar->m_intValue = nValue;
        } else {
            CVariable v;
            v.SetName(sName);
            v.m_intValue = nValue;
            pHash->AddKey(v);
        }
    }

    return ACTION_DONE;
}

// 0x45D3E0 - decodes a numeric spell id into a resref string of the form
// "SP" + family + "%03d" where family is one of:
//   1xxx -> SPPR (priest)
//   2xxx -> SPWI (wizard / arcane)
//   3xxx -> SPIN (innate)
// When spellId is 0 the helper falls back to field_588 (caster's pending
// spell id); if both are zero the output is the empty string.  Invalid
// family ids fall through to an empty prefix and the binary logs an
// "Invalid or missing spell ID" assert -- skipped here.
void CGameAIBase::SpellIdToResRef(int spellId, CString& outResRef)
{
    if (spellId == 0 && field_588 == 0) {
        outResRef = "";
        return;
    }
    if (spellId == 0) {
        spellId = field_588;
    }

    outResRef = "SP";
    int family = spellId / 1000;
    if (family == 1) {
        outResRef += "PR";
    } else if (family == 2) {
        outResRef += "WI";
    } else if (family == 3) {
        outResRef += "IN";
    }

    CString suffix;
    suffix.Format("%03d", spellId % 1000);
    outResRef += suffix;
}

// 0x461190 - ForceSpell (0x71) and ReallyForceSpell (0xB5) action handler.
// This step wires the cast-animation state machine -- the binary's three
// visual stages now drive CMessageSetSequence on the caster so the sprite
// raises hands during the wind-up and fires the cast-burst pose on the
// release tick.  ApplyCastingEffect moves from the fire branch to the
// first cast tick to match the binary (pre-cast SPL feature blocks run
// once at cast start, not at completion).
//
// Recovered field offsets on caster (param_1 in the decomp):
//   +0x25  -> m_typeAI.m_nGeneral (binary compares against DAT_00847C48,
//             the G_DEAD marker written by 0x4ACFB2 / 0x70D4F6 when
//             m_baseStats.m_generalState has STATE_DEAD bit 0x800)
//   +0x33  -> m_typeAI.m_nSpecific (used as effect.casterParty in case 6)
//   +0x11D -> m_actionCount (cast-frame counter; CGameAIBase 0x474)
//   +0x14D2-> m_nSequence (CGameSprite animation sequence; 0x5348)
//   +0x283 -> m_derivedStats.m_spellStates dword0 (bit 0x100000 == lost
//             concentration; CGameSprite 0xA0C)
//   +0x476 -> m_curAction.m_actionID (only the 0xB5 fast-path check uses it)
//   +0x966 -> m_derivedStats.m_nLevel (CGameSprite 0x920+0x46)
//
// Sequence ids match the DAT_0085BBB2/DAT_0085BBB3 byte constants:
//   SEQ_CAST    = 2 (DAT_0085BBB2, cast-burst pose)
//   SEQ_CONJURE = 3 (DAT_0085BBB3, raise-hands wind-up)
//
// Helpers still pending (see memory/project_forcespell_recovery.md):
//   FUN_0051EAF0 - projectile/visual-effect factory (~5400 lines).  Until
//                  recovered the binary's projectile launch path
//                  (CMessageFireProjectile + CProjectile::Launch vfn at
//                  +0x6c) cannot fire.  targetType 2 effects are applied
//                  directly to explicit object targets as a gameplay
//                  fallback, but the visible bolt is absent.
//   FUN_00727720 - per-class memorization lookup; supplies the byte the
//                  binary parks at projectile +0x186 (caster-class index
//                  used by saving-throw + damage scaling).
//   FUN_00727B80 - silence/spell-state filter consulted on the first cast
//                  tick before queueing the concentration / dispel
//                  ITEM_EFFECT broadcasts.
SHORT CGameAIBase::ForceSpellAction(CGameObject* target)
{
    // Dead casters short-circuit (binary 0x4611BA).
    if (m_typeAI.GetGeneral() == CAIObjectType::G_DEAD) {
        return ACTION_DONE;
    }
    if (target == NULL) {
        return ACTION_INTERRUPTABLE;
    }

    // The caster move-to-range and orient gates run AFTER the spell + ability
    // are resolved (the chosen ability's range drives the approach distance) --
    // see the combined gate below, just before the cast-time machine.

    // Resolve resref + cast level.  Script can pass either a CString in
    // m_string1 (with cast level in m_specificID) or a numeric spell id in
    // m_specificID alone (cast level falls back to caster's m_nLevel).
    SHORT specificLevel;
    CResRef resRef;
    CString sStr1 = m_curAction.GetString1();
    if (sStr1.IsEmpty()) {
        CString sFromId;
        SpellIdToResRef(m_curAction.m_specificID, sFromId);
        if (sFromId.IsEmpty()) {
            return ACTION_INTERRUPTABLE;
        }
        resRef = sFromId;
        if (GetObjectType() == CGameObject::TYPE_SPRITE) {
            specificLevel = static_cast<CGameSprite*>(this)
                                ->GetDerivedStats()
                                ->m_nLevel;
        } else {
            specificLevel = 1;
        }
    } else {
        resRef = sStr1;
        // Spell (0x1F) / SpellPoint (0x5F) cast from the UI carry the resref in
        // String1 with specificID == 0; their caster level is the sprite's own
        // level. The real Spell handlers (ExecuteAction case 0x9e -> the cast
        // state machine) are unrecovered, so this stopgap -- which routes Spell
        // through the ForceSpell handler -- derives the level here. ForceSpell
        // (0x71) / ForceSpellPoint (0x72) and their Really/NoDec variants keep
        // the script-supplied level in specificID.
        if (m_curAction.m_actionID == CAIAction::FORCESPELL
            || m_curAction.m_actionID == CAIAction::REALLYFORCESPELL
            || m_curAction.m_actionID == CAIAction::FORCESPELLPOINT) {
            specificLevel = static_cast<SHORT>(m_curAction.m_specificID);
        } else if (GetObjectType() == CGameObject::TYPE_SPRITE) {
            specificLevel = static_cast<CGameSprite*>(this)
                                ->GetDerivedStats()
                                ->m_nLevel;
        } else {
            specificLevel = 1;
        }
    }
    if (specificLevel < 2) {
        specificLevel = 1;
    }

    // Binary heap-allocates a 16-byte CResHelper<CResSpell,1006> (== CSpell)
    // with SetResRef + auto-request, then Demand() to make sure the .SPL is
    // resident.
    CSpell* pSpell = new CSpell();
    if (pSpell == NULL) {
        return ACTION_INTERRUPTABLE;
    }
    pSpell->SetResRef(resRef, TRUE, TRUE);
    if (!pSpell->Demand()) {
        delete pSpell;
        return ACTION_INTERRUPTABLE;
    }

    // 0xB5 (ReallyForceSpell) skips the target-appropriate gate.
    if (m_curAction.m_actionID != 0xB5
        && (target->GetObjectType() & CGameObject::TYPE_AIBASE) != 0
        && !CheckAppropriateTarget(pSpell, static_cast<CGameAIBase*>(target))) {
        pSpell->Release();
        delete pSpell;
        return ACTION_INTERRUPTABLE;
    }

    // Pick the ability whose minCasterLevel <= specificLevel.  Binary scans
    // forward, counts qualifying entries, then re-fetches at (count - 1).
    LONG abilityCount = pSpell->GetAbilityCount();
    SHORT nAbilityIndex = -1;
    for (LONG i = 0; i < abilityCount; ++i) {
        SPELL_ABILITY* pCheck = pSpell->GetAbility(i);
        if (pCheck == NULL
            || pCheck->minCasterLevel > static_cast<WORD>(specificLevel)) {
            break;
        }
        nAbilityIndex++;
    }
    SPELL_ABILITY* pAbility = pSpell->GetAbility(nAbilityIndex);
    if (pAbility == NULL) {
        pSpell->Release();
        delete pSpell;
        return ACTION_INTERRUPTABLE;
    }

    // Cast-time gate.  speedFactor is the per-ability cast time in 1/10ths
    // of an AI tick; the binary computes castTime = speedFactor*100/10 ==
    // *10 and walks the caster through three visual stages:
    //   m_actionCount == 0           : first tick -- queue the SPL pre-cast
    //                                  feature blocks (ApplyCastingEffect)
    //                                  and emit concentration/dispel
    //                                  ITEM_EFFECT broadcasts if the caster
    //                                  is silenced or has lost concentration
    //   m_actionCount < castTime - 4 : raise-hands (SEQ_CONJURE)
    //   m_actionCount < castTime     : cast-burst (SEQ_CAST) +
    //                                  CGameSprite::ApplyCastingEffectPost
    //   otherwise                    : fire and complete.
    // The stage-1/2 branches return ACTION_INTERRUPTABLE so the action
    // executor re-enters this case next tick.  ReallyForceSpell (0xB5)
    // and non-sprite callers skip the cast-time machine entirely and
    // resolve in a single tick (binary 0x46139A check on +0x476).
    BOOL isSprite = (GetObjectType() & CGameObject::TYPE_SPRITE) != 0;
    BOOL bInstantCast = !isSprite || m_curAction.m_actionID == 0xB5;
    CGameSprite* pSprite = isSprite ? static_cast<CGameSprite*>(this) : NULL;
    CPoint targetPos = target->GetPos();

    // --- Move-to-range + caster orient gate (binary FUN_00740270). ----------
    // Only the normal UI casts (Spell 0x1F / SpellNoDec 0xBF) walk and turn;
    // the force actions (ForceSpell 0x71 / ReallyForceSpell 0xB5 /
    // ForceSpellPoint 0x72) deliberately fire in place.  Both gates run here --
    // after the ability is chosen, before the cast-time machine -- in approach-
    // then-face order: the walk takes priority so the orient gate never preempts
    // the path (a turn-vs-walk fight that would never make progress).  The
    // original runs face-then-range with a free-running cast counter; this
    // stopgap pins the counter (see below), so the order is swapped to keep the
    // walk alive.
    if (isSprite
        && m_curAction.m_actionID != CAIAction::FORCESPELL
        && m_curAction.m_actionID != CAIAction::REALLYFORCESPELL
        && m_curAction.m_actionID != CAIAction::FORCESPELLPOINT) {
        // (1) Move-to-range + line-of-sight.  FUN_00740270 fires only when the
        // target is BOTH within the chosen ability's range
        // (CGameSprite::CheckCastingRange, 0x7408B6) AND in line of sight
        // (CGameArea::CheckLOS, 0x7408DC); a spell
        // whose header flag 0x800 is set bypasses the LOS half (0x7408E5).  When
        // neither holds it walks the caster toward the target via FUN_0073EDD0
        // (== CGameSprite::MoveToObject, 0x740A37) and re-enters next tick.
        // CheckLOS fails not only on blocking terrain but also when the target is
        // beyond the caster's sight range (its leading distance gate,
        // CGameArea.cpp:498) -- so a target sitting inside a spell's generous range
        // but past sight is still approached first (the runtime-traced gap: a
        // creature inside Magic Missile's 50-square range yet past sight still
        // made the original walk while our range-only gate cast in place).  range
        // == 0xFFFF (-1) is unbounded (matches CheckCastingRange's -1 short test); the
        // grid-square distance carries a +2 slack; self casts never walk.  The
        // whole decision is latched to m_actionCount <= 0 -- the binary's +0x54EA
        // "casting begun" flag, set once the cast machine runs -- so a target that
        // steps out of sight mid-cast cannot re-trigger the walk and restart the
        // cast.
        if (m_actionCount <= 0
            && target != static_cast<CGameObject*>(this)) {
            CPoint selfPos = pSprite->GetPos();
            LONG dx = selfPos.x / CPathSearch::GRID_SQUARE_SIZEX
                - targetPos.x / CPathSearch::GRID_SQUARE_SIZEX;
            LONG dy = selfPos.y / CPathSearch::GRID_SQUARE_SIZEY
                - targetPos.y / CPathSearch::GRID_SQUARE_SIZEY;
            LONG slack = pAbility->range + 2;
            BOOL bInRange = pAbility->range == 0xFFFF
                || dx * dx + dy * dy <= slack * slack;
            BOOL bCanCast = bInRange
                && (m_pArea->CheckLOS(m_pos, targetPos, GetTerrainTable(), FALSE)
                    || (pSpell->GetItemFlags() & 0x800) != 0);
            if (!bCanCast) {
                pSpell->Release();
                delete pSpell;
                // Pin the action counter (DoAction post-increments -1 -> 0) so
                // the cast-time machine still sees m_actionCount == 0 on its
                // first tick once the approach finishes -- a counter inflated by
                // the walk would skip ApplyCastingEffect and fizzle the cast.
                m_actionCount = -1;
                return pSprite->MoveToObject(target);
            }
            // In range and in sight: drop the approach path that MoveToObject
            // submitted (it paths all the way to personal space, CGameSprite.cpp
            // :14060) so the caster does not coast into melee after the spell
            // fires.  The binary posts the same CMessageDropPath right before it
            // casts (0x7408ED, vtable 0x84C44C, just ahead of LAB_00740922).
            CMessage* pDropPath = new CMessageDropPath(m_id, m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pDropPath, FALSE);
        }

        // (2) Orient.  The SEQ_CAST executor compares m_nDirection against
        // GetDirection(the target's live GetPos()) and, while they differ, posts
        // a gradual CMessageSetDirection (-> CGameSprite::SetDirection) and
        // re-enters WITHOUT advancing the cast, so the casting glow is born
        // already facing the target.  The m_actionCount <= 0 guard disables the
        // gate once the cast is counting: otherwise a target that moves mid-cast
        // re-fires the turn and re-pins the counter every tick, restarting the
        // cast forever (the un-guarded 0d1347bf loop).
        if (m_actionCount <= 0
            && pSprite->m_nDirection != pSprite->GetDirection(targetPos)) {
            pSpell->Release();
            delete pSpell;
            pSprite->SetDirection(targetPos);
            m_actionCount = -1;
            return ACTION_INTERRUPTABLE;
        }
    }

    if (!bInstantCast) {
        WORD castTime = static_cast<WORD>(pAbility->speedFactor) * 10;
        SHORT currentSeq = pSprite->m_nSequence;


        // First-tick pre-cast hook.  Binary 0x46139A: queue the SPL's
        // pre-cast feature blocks (visuals, chant, projectile-spawn) before
        // any animation runs so they overlap with the cast-time wind-up.
        if (m_actionCount == 0) {
            pSprite->ApplyCastingEffect(pSpell, pAbility, targetPos);
            // TODO (multi-session): FUN_00727B80 silence/spell-state gate ->
            // concentration ITEM_EFFECT (opcode 0x88) broadcast against
            // target sprite, and m_spellStates bit 0x100000 -> dispel
            // ITEM_EFFECT (opcode 0xA0) on self.  Both queue a
            // CMessageAddEffect via PTR_FUN_008487CC.  No gameplay impact
            // while missing -- the caster simply doesn't visibly lose
            // concentration when struck mid-cast.
        }

        if (m_actionCount < static_cast<SHORT>(castTime - 4)) {
            // Stage 1: raise hands.  Self-queued CMessageSetSequence so the
            // animation system picks up SEQ_CONJURE on the next render
            // (binary 0x461795 path, PTR_FUN_008488C4 vtable -> CMessageSetSequence).
            if (currentSeq != CGameSprite::SEQ_CONJURE) {
                CMessage* msg = new CMessageSetSequence(
                    CGameSprite::SEQ_CONJURE, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            }
            pSpell->Release();
            delete pSpell;
            return ACTION_INTERRUPTABLE;
        }

        if (m_actionCount < static_cast<SHORT>(castTime)) {
            // Stage 2: cast burst.  On entry transition send SEQ_CAST and
            // play the burst sound cue (ApplyCastingEffectPost).
            if (currentSeq != CGameSprite::SEQ_CAST) {
                CMessage* msg = new CMessageSetSequence(
                    CGameSprite::SEQ_CAST, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
                pSprite->ApplyCastingEffectPost(pSpell, pAbility);
            }
            pSpell->Release();
            delete pSpell;
            return ACTION_INTERRUPTABLE;
        }

        // Stage 3 entry: cast-time exhausted.  If the cast was instant
        // enough to skip stage 2 (speedFactor*10 <= 4 + first tick), the
        // burst sequence + sound still need to fire here.
        if (currentSeq != CGameSprite::SEQ_CAST) {
            CMessage* msg = new CMessageSetSequence(
                CGameSprite::SEQ_CAST, m_id, m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            pSprite->ApplyCastingEffectPost(pSpell, pAbility);
        }
    } else if (isSprite) {
        // ReallyForceSpell single-tick path: still queue the SPL pre-cast
        // feature blocks so visuals and the gameplay 8-way dispatch see a
        // consistent state, but skip every sequence-change message.
        pSprite->ApplyCastingEffect(pSpell, pAbility, targetPos);
    }

    // Fire path -- ability's gameplay-payload effects.  The 8-way targetType
    // dispatch matches binary FUN_00461190 lines 262-339.  Non-sprite
    // callers fall back to the legacy stub (FireSpell) since they don't
    // have an area pointer for the AoE / party cases.
    if (isSprite) {
        BYTE nClass = static_cast<BYTE>(m_curAction.m_specificID2 & 0xFF);
        DWORD nSpec = pSprite->m_baseStats.m_specialization;
        BYTE nLevel = static_cast<BYTE>(specificLevel);

        // Pre-build the projectile (binary FUN_00461190 @0x46137c: DecodeProjectile
        // with the ability's missileType) before the effect dispatch, so the
        // targetType-2 gameplay effects can ride the missile (CProjectile::AddEffect)
        // and deliver on arrival instead of at cast.  Fired directly after the loop.
        CProjectile* pProj = CProjectile::DecodeProjectile(pAbility->missileType, this, 0);

        for (LONG e = 0; e < static_cast<LONG>(pAbility->effectCount); ++e) {
            CGameEffect* pEffect = pSpell->BuildAbilityEffect(
                nAbilityIndex, e, this, nClass, nSpec, nLevel);
            if (pEffect == NULL) {
                continue;
            }
            pEffect->m_source = pSprite->GetPos();
            pEffect->m_sourceID = m_id;
            pEffect->m_target = targetPos;
            switch (pEffect->m_targetType) {
            case 1: {
                CMessage* msg = new CMessageAddEffect(pEffect, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
                continue;
            }
            case 2:
                // targetType 2 (preset target): the effect rides the missile and
                // delivers on arrival (binary case 2 @0x461953: CProjectile::AddEffect
                // on the pre-built projectile).  If the projectile type is unrecovered
                // (pProj NULL), fall back to a direct cast-time application so the
                // spell still has gameplay impact even without the missile.
                if (pProj != NULL) {
                    pProj->AddEffect(pEffect);
                    continue;
                }
                if (target != NULL) {
                    CMessage* msg = new CMessageAddEffect(pEffect, m_id, target->m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
                    continue;
                }
                break;
            case 3:
                pSprite->ApplyEffectToParty(pEffect);
                break;
            case 4:
                pSprite->m_pArea->ApplyEffect(pEffect, FALSE, FALSE, 0, NULL);
                break;
            case 5:
                pSprite->m_pArea->ApplyEffect(pEffect, TRUE, FALSE, 0, NULL);
                break;
            case 6:
                pSprite->m_pArea->ApplyEffect(
                    pEffect, FALSE, TRUE, m_typeAI.m_nSpecific, NULL);
                break;
            case 7:
                pSprite->m_pArea->ApplyEffect(
                    pEffect, FALSE, TRUE, GetAIType().m_nSpecific, NULL);
                break;
            case 8:
                pSprite->m_pArea->ApplyEffect(pEffect, FALSE, FALSE, 0, pSprite);
                break;
            default:
                break;
            }
            delete pEffect;
        }

        // Cast-success feedback line -- "<caster> casts <SpellName>".
        // Binary 0x4619E5: only sprite casters emit FEEDBACK_SPELL.
        STRREF strSpellName = pSpell->GetGenericName();
        pSprite->FeedBack(CGameSprite::FEEDBACK_SPELL, 0, 0, 0,
            static_cast<LONG>(strSpellName), 0, 0);

        // Projectile launch (binary FUN_00461190 @0x46146d): fire the pre-built
        // projectile directly via CProjectile::Fire (vtable slot 0x6c, height 0x1e),
        // carrying the targetType-2 effects attached above so they deliver on
        // arrival rather than at cast.  The original ALSO queues a broadcast
        // CMessageFireProjectile here, but its Run() skips on the casting owner and
        // only fires the visual on remote machines -- that MP replication is deferred
        // with the rest of MP.  The caster-class stamp (proj +0x186 via FUN_00727720)
        // for damage scaling is likewise still deferred.  Fire ignores nHeight.
        if (pProj != NULL) {
            LONG nMissileTarget = (target != NULL)
                ? target->m_id : CGameObjectArray::INVALID_INDEX;
            pProj->Fire(pSprite->m_pArea, m_id, nMissileTarget, targetPos, 0x1E, 0);
        }
    } else {
        FireSpell(resRef, target);
    }

    // Consume the memorized slot.  Binary path lives in the
    // FUN_00740270 / FUN_00742840 quickspell wrappers (still TODO) -- here
    // we try the three slot kinds the sprite can own (per-class memorized,
    // cleric/paladin domain, innate) and stop at the first match.
    if (isSprite) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(this);
        SHORT nSpellLevel = pSpell->GetLevel();
        if (nSpellLevel >= 1) {
            UINT nLvl = static_cast<UINT>(nSpellLevel - 1);
            BOOLEAN consumed = FALSE;
            for (UINT nIdx = 0; nIdx < CSPELLLIST_NUM_CLASSES && !consumed; ++nIdx) {
                BYTE nCasterClass = g_pBaldurChitin->GetObjectGame()->GetSpellcasterClass(nIdx);
                if (nCasterClass == 0) { continue; }
                if (pSprite->SubtractFromSpellCount(nCasterClass, nLvl, resRef, 1, 0)) {
                    consumed = TRUE;
                }
            }
            if (!consumed && pSprite->SubtractFromDomainSpellCount(nLvl, resRef, 1, 0)) {
                consumed = TRUE;
            }
            if (!consumed) {
                pSprite->SubtractFromInnateSpellCount(resRef, 1, 0);
            }
        }
    }

    pSpell->Release();
    delete pSpell;
    return ACTION_DONE;
}

// 0x461B80
SHORT CGameAIBase::ForceSpellPointAction()
{
    if (m_typeAI.GetGeneral() == CAIObjectType::G_DEAD) {
        return ACTION_DONE;
    }

    SHORT specificLevel;
    CResRef resRef;
    CString sStr1 = m_curAction.GetString1();
    if (sStr1.IsEmpty()) {
        CString sFromId;
        SpellIdToResRef(m_curAction.m_specificID, sFromId);
        if (sFromId.IsEmpty()) {
            return ACTION_INTERRUPTABLE;
        }
        resRef = sFromId;
        if (GetObjectType() == CGameObject::TYPE_SPRITE) {
            specificLevel = static_cast<CGameSprite*>(this)
                                ->GetDerivedStats()
                                ->m_nLevel;
        } else {
            specificLevel = 1;
        }
    } else {
        resRef = sStr1;
        // Spell (0x1F) / SpellPoint (0x5F) cast from the UI carry the resref in
        // String1 with specificID == 0; their caster level is the sprite's own
        // level. The real Spell handlers (ExecuteAction case 0x9e -> the cast
        // state machine) are unrecovered, so this stopgap -- which routes Spell
        // through the ForceSpell handler -- derives the level here. ForceSpell
        // (0x71) / ForceSpellPoint (0x72) and their Really/NoDec variants keep
        // the script-supplied level in specificID.
        if (m_curAction.m_actionID == CAIAction::FORCESPELL
            || m_curAction.m_actionID == CAIAction::REALLYFORCESPELL
            || m_curAction.m_actionID == CAIAction::FORCESPELLPOINT) {
            specificLevel = static_cast<SHORT>(m_curAction.m_specificID);
        } else if (GetObjectType() == CGameObject::TYPE_SPRITE) {
            specificLevel = static_cast<CGameSprite*>(this)
                                ->GetDerivedStats()
                                ->m_nLevel;
        } else {
            specificLevel = 1;
        }
    }
    if (specificLevel < 2) {
        specificLevel = 1;
    }

    CSpell* pSpell = new CSpell();
    if (pSpell == NULL) {
        return ACTION_INTERRUPTABLE;
    }
    pSpell->SetResRef(resRef, TRUE, TRUE);
    if (!pSpell->Demand()) {
        delete pSpell;
        return ACTION_INTERRUPTABLE;
    }

    LONG abilityCount = pSpell->GetAbilityCount();
    SHORT nAbilityIndex = -1;
    for (LONG i = 0; i < abilityCount; ++i) {
        SPELL_ABILITY* pCheck = pSpell->GetAbility(i);
        if (pCheck == NULL
            || pCheck->minCasterLevel > static_cast<WORD>(specificLevel)) {
            break;
        }
        nAbilityIndex++;
    }
    SPELL_ABILITY* pAbility = pSpell->GetAbility(nAbilityIndex);
    if (pAbility == NULL) {
        pSpell->Release();
        delete pSpell;
        return ACTION_INTERRUPTABLE;
    }

    BOOL isSprite = (GetObjectType() & CGameObject::TYPE_SPRITE) != 0;
    BOOL bInstantCast = !isSprite;
    CGameSprite* pSprite = isSprite ? static_cast<CGameSprite*>(this) : NULL;
    CPoint targetPos = m_curAction.m_dest;

    if (!bInstantCast) {
        WORD castTime = static_cast<WORD>(pAbility->speedFactor) * 10;
        SHORT currentSeq = pSprite->m_nSequence;


        if (m_actionCount == 0) {
            pSprite->ApplyCastingEffect(pSpell, pAbility, targetPos);
        }

        if (m_actionCount < static_cast<SHORT>(castTime - 4)) {
            if (currentSeq != CGameSprite::SEQ_CONJURE) {
                CMessage* msg = new CMessageSetSequence(
                    CGameSprite::SEQ_CONJURE, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            }
            pSpell->Release();
            delete pSpell;
            return ACTION_INTERRUPTABLE;
        }

        if (m_actionCount < static_cast<SHORT>(castTime)) {
            if (currentSeq != CGameSprite::SEQ_CAST) {
                CMessage* msg = new CMessageSetSequence(
                    CGameSprite::SEQ_CAST, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
                pSprite->ApplyCastingEffectPost(pSpell, pAbility);
            }
            pSpell->Release();
            delete pSpell;
            return ACTION_INTERRUPTABLE;
        }

        if (currentSeq != CGameSprite::SEQ_CAST) {
            CMessage* msg = new CMessageSetSequence(
                CGameSprite::SEQ_CAST, m_id, m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
            pSprite->ApplyCastingEffectPost(pSpell, pAbility);
        }
    }

    if (isSprite) {
        BYTE nClass = static_cast<BYTE>(m_curAction.m_specificID2 & 0xFF);
        DWORD nSpec = pSprite->m_baseStats.m_specialization;
        BYTE nLevel = static_cast<BYTE>(specificLevel);

        // Pre-build the projectile (binary 0x462102: DecodeProjectile with the
        // ability's missileType) before the effect dispatch so the targetType-2
        // gameplay effects can ride the missile (CProjectile::AddEffect) and
        // deliver on arrival instead of at cast.  Fired directly after the loop.
        // This mirrors the object-target twin ForceSpellAction (0x461190); the
        // earlier stopgap here dropped case 2 (no projectile) so projectile
        // spells -- Fireball: damage targetType-2 + missile 38 -- never fired.
        CProjectile* pProj = CProjectile::DecodeProjectile(pAbility->missileType, this, 0);

        for (LONG e = 0; e < static_cast<LONG>(pAbility->effectCount); ++e) {
            CGameEffect* pEffect = pSpell->BuildAbilityEffect(
                nAbilityIndex, e, this, nClass, nSpec, nLevel);
            if (pEffect == NULL) {
                continue;
            }
            pEffect->m_source = pSprite->GetPos();
            pEffect->m_sourceID = m_id;
            pEffect->m_target = targetPos;
            switch (pEffect->m_targetType) {
            case 1: {
                CMessage* msg = new CMessageAddEffect(pEffect, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);
                continue;
            }
            case 2:
                // targetType 2 (preset target): the effect rides the missile and
                // delivers on arrival (binary case 2 @0x461953: CProjectile::AddEffect).
                // A point cast has no object to address, so when the projectile type
                // is unrecovered (pProj NULL) the effect is dropped rather than fired
                // at cast.
                if (pProj != NULL) {
                    pProj->AddEffect(pEffect);
                    continue;
                }
                break;
            case 3:
                pSprite->ApplyEffectToParty(pEffect);
                break;
            case 4:
                pSprite->m_pArea->ApplyEffect(pEffect, FALSE, FALSE, 0, NULL);
                break;
            case 5:
                pSprite->m_pArea->ApplyEffect(pEffect, TRUE, FALSE, 0, NULL);
                break;
            case 6:
                pSprite->m_pArea->ApplyEffect(
                    pEffect, FALSE, TRUE, m_typeAI.m_nSpecific, NULL);
                break;
            case 8:
                pSprite->m_pArea->ApplyEffect(pEffect, FALSE, FALSE, 0, pSprite);
                break;
            case 7:
            default:
                // targetType 7 (binary @0x46213b): no application, just release.
                break;
            }
            delete pEffect;
        }

        STRREF strSpellName = pSpell->GetGenericName();
        pSprite->FeedBack(CGameSprite::FEEDBACK_SPELL, 0, 0, 0,
            static_cast<LONG>(strSpellName), 0, 0);

        // Projectile launch (binary 0x46146d region): fire the pre-built
        // projectile via CProjectile::Fire (vtable slot 0x6c, height 0x1e),
        // carrying the targetType-2 effects attached above.  A point cast has no
        // target object, so the missile homes on the destination point with an
        // invalid target id.  The original ALSO queues a broadcast
        // CMessageFireProjectile (its Run() only fires the visual on remote
        // machines) and stamps the caster-class scaling byte (proj +0x186 via
        // FUN_00727720); both are deferred with the rest of MP.  Fire ignores
        // nHeight.
        if (pProj != NULL) {
            pProj->Fire(pSprite->m_pArea, m_id, CGameObjectArray::INVALID_INDEX,
                targetPos, 0x1E, 0);
        }
    } else {
        FireSpellPoint(resRef, targetPos);
    }

    if (isSprite) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(this);
        SHORT nSpellLevel = pSpell->GetLevel();
        if (nSpellLevel >= 1) {
            UINT nLvl = static_cast<UINT>(nSpellLevel - 1);
            BOOLEAN consumed = FALSE;
            for (UINT nIdx = 0; nIdx < CSPELLLIST_NUM_CLASSES && !consumed; ++nIdx) {
                BYTE nCasterClass = g_pBaldurChitin->GetObjectGame()->GetSpellcasterClass(nIdx);
                if (nCasterClass == 0) { continue; }
                if (pSprite->SubtractFromSpellCount(nCasterClass, nLvl, resRef, 1, 0)) {
                    consumed = TRUE;
                }
            }
            if (!consumed && pSprite->SubtractFromDomainSpellCount(nLvl, resRef, 1, 0)) {
                consumed = TRUE;
            }
            if (!consumed) {
                pSprite->SubtractFromInnateSpellCount(resRef, 1, 0);
            }
        }
    }

    pSpell->Release();
    delete pSpell;
    return ACTION_DONE;
}

// 0x45C290 - resolves m_acteeID, dropping sprites whose immunity list names
// this object's CAIObjectType (e.g. spell-immune creature scripted against
// by a matching caster type), and publishes the result through UpdateTarget.
// Returns the target with an ACTIVE GetObjectArray share -- caller must
// ReleaseShare on the returned id.  The dispatcher inlines this body in
// several CGameAIBase::ExecuteAction cases (0x32-0x3A, 0x3E, 0x7D, 0x7E,
// 0x85); src calls the helper there instead.
CGameObject* CGameAIBase::ResolveActionTarget()
{
    CGameObject* pObj = m_curAction.m_acteeID.GetObject(this, FALSE);
    if (pObj != NULL
        && pObj->GetObjectType() == CGameObject::TYPE_SPRITE
        && static_cast<CGameSprite*>(pObj)->GetDerivedStats()->m_cImmunitiesAIType.OnList(m_typeAI)) {
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
            pObj->m_id,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        pObj = NULL;
    }

    UpdateTarget(pObj);

    return pObj;
}

// 0x45BDD0 - typed resolver used by most dispatcher cases: same resolve +
// immunity drop as the untyped overload, then filters by nObjectType
// (TYPE_NONE accepts anything, TYPE_AIBASE requires only the AIBASE bit,
// any other value must equal GetObjectType exactly) and caps the targeting
// range of blinded non-party sprites at GetPersonalSpace() / 2 + 4 grid
// squares.  Rejected targets are released and re-published as NULL.
CGameObject* CGameAIBase::ResolveActionTarget(BYTE nObjectType)
{
    CGameObject* pObj = m_curAction.m_acteeID.GetObject(this, FALSE);
    if (pObj != NULL
        && pObj->GetObjectType() == CGameObject::TYPE_SPRITE
        && static_cast<CGameSprite*>(pObj)->GetDerivedStats()->m_cImmunitiesAIType.OnList(m_typeAI)) {
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
            pObj->m_id,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        pObj = NULL;
    }

    UpdateTarget(pObj);

    if (pObj != NULL) {
        if (pObj->GetObjectType() != nObjectType
            && nObjectType != CGameObject::TYPE_AIBASE
            && nObjectType != CGameObject::TYPE_NONE) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
            UpdateTarget(NULL);
            return NULL;
        }

        if (nObjectType == CGameObject::TYPE_AIBASE
            && (pObj->GetObjectType() & CGameObject::TYPE_AIBASE) == 0) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
            UpdateTarget(NULL);
            return NULL;
        }

        if (GetObjectType() == CGameObject::TYPE_SPRITE
            && g_pBaldurChitin->GetObjectGame()->GetCharacterPortraitNum(m_id) == -1
            && (static_cast<CGameSprite*>(this)->GetDerivedStats()->m_generalState & STATE_BLIND) != 0
            && pObj->GetObjectType() == CGameObject::TYPE_SPRITE) {
            const CPoint& targetPos = pObj->GetPos();
            CPoint ptTarget(
                targetPos.x / CPathSearch::GRID_SQUARE_SIZEX,
                targetPos.y / CPathSearch::GRID_SQUARE_SIZEY);
            CPoint ptThis(
                m_pos.x / CPathSearch::GRID_SQUARE_SIZEX,
                m_pos.y / CPathSearch::GRID_SQUARE_SIZEY);
            LONG nSquares = CAIUtil::CountSquares(ptThis, ptTarget);

            if (nSquares > (static_cast<CGameSprite*>(this)->m_animation.GetPersonalSpace() >> 1) + 4) {
                g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                    pObj->m_id,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
                UpdateTarget(NULL);
                return NULL;
            }
        }
    }

    UpdateTarget(pObj);

    return pObj;
}

// 0x45C030 - explicit-filter resolver: identical to the nObjectType overload
// above, except the object comes from a caller-supplied CAIObjectType instead
// of m_curAction.m_acteeID.  Used by the dispatcher cases that synthesise their
// own target filter (e.g. GroupAttack's "enemy of me") rather than acting on
// the actee the action carries.  The binary emits both bodies in full rather
// than delegating.
CGameObject* CGameAIBase::ResolveActionTarget(const CAIObjectType& type, BYTE nObjectType)
{
    CGameObject* pObj = type.GetObject(this, FALSE);
    if (pObj != NULL
        && pObj->GetObjectType() == CGameObject::TYPE_SPRITE
        && static_cast<CGameSprite*>(pObj)->GetDerivedStats()->m_cImmunitiesAIType.OnList(m_typeAI)) {
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
            pObj->m_id,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
        pObj = NULL;
    }

    UpdateTarget(pObj);

    if (pObj != NULL) {
        if (pObj->GetObjectType() != nObjectType
            && nObjectType != CGameObject::TYPE_AIBASE
            && nObjectType != CGameObject::TYPE_NONE) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
            UpdateTarget(NULL);
            return NULL;
        }

        if (nObjectType == CGameObject::TYPE_AIBASE
            && (pObj->GetObjectType() & CGameObject::TYPE_AIBASE) == 0) {
            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObj->m_id,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
            UpdateTarget(NULL);
            return NULL;
        }

        if (GetObjectType() == CGameObject::TYPE_SPRITE
            && g_pBaldurChitin->GetObjectGame()->GetCharacterPortraitNum(m_id) == -1
            && (static_cast<CGameSprite*>(this)->GetDerivedStats()->m_generalState & STATE_BLIND) != 0
            && pObj->GetObjectType() == CGameObject::TYPE_SPRITE) {
            const CPoint& targetPos = pObj->GetPos();
            CPoint ptTarget(
                targetPos.x / CPathSearch::GRID_SQUARE_SIZEX,
                targetPos.y / CPathSearch::GRID_SQUARE_SIZEY);
            CPoint ptThis(
                m_pos.x / CPathSearch::GRID_SQUARE_SIZEX,
                m_pos.y / CPathSearch::GRID_SQUARE_SIZEY);
            LONG nSquares = CAIUtil::CountSquares(ptThis, ptTarget);

            if (nSquares > (static_cast<CGameSprite*>(this)->m_animation.GetPersonalSpace() >> 1) + 4) {
                g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                    pObj->m_id,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
                UpdateTarget(NULL);
                return NULL;
            }
        }
    }

    UpdateTarget(pObj);

    return pObj;
}

// 0x44DC10 case 0xA1 - IncrementChapter(S:Chapter*).
SHORT CGameAIBase::IncrementChapter()
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();

    CString sChapter = m_curAction.GetString1();
    if (sChapter.IsEmpty()) {
        sChapter = "CHAPTERS";
    }

    BYTE resRef[RESREF_SIZE];
    CResRef(sChapter).GetResRef(resRef);

    // TODO: Incomplete. The MP client request path is not recovered here yet.
    // The binary SP/host path opens the chapter screen through
    // CScreenChapter::StartChapterMultiplayerHost.
    if (!g_pChitin->cNetwork.GetSessionOpen()
        || g_pChitin->cNetwork.GetSessionHosting() == TRUE) {
        BYTE nChapter = static_cast<BYTE>(pGame->GetCurrentChapter() + 1);

        g_pBaldurChitin->m_pEngineChapter->StartChapterMultiplayerHost(
            nChapter,
            resRef);

        CList<STRREF, STRREF>* pTextList = pGame->GetRuleTables().GetChapterText(CResRef(resRef), nChapter);
        if (pTextList != NULL) {
            if (pTextList->GetCount() > 1) {
                POSITION pos = pTextList->GetHeadPosition();
                pTextList->GetNext(pos);
                pGame->GetJournal()->AddEntry(pTextList->GetNext(pos), 0);
            }

            delete pTextList;
        }
    }

    return ACTION_DONE;
}

// 0x44DC10 case 0x78 - StartCutScene(S:CutScene*).
SHORT CGameAIBase::StartCutScene()
{
    CString sScript = m_curAction.GetString1();
    if (sScript.IsEmpty()) {
        return ACTION_ERROR;
    }

    CAIScript script((CResRef(sScript)));
    LONG queuedCount = 0;

    POSITION pos = script.m_caList.GetHeadPosition();
    while (pos != NULL) {
        CAIConditionResponse* pConditionResponse = script.m_caList.GetNext(pos);
        if (pConditionResponse == NULL) {
            continue;
        }

        POSITION responsePos = pConditionResponse->m_responseSet.m_responseList.GetHeadPosition();
        if (responsePos == NULL) {
            continue;
        }

        CAIResponse* pResponse = pConditionResponse->m_responseSet.m_responseList.GetNext(responsePos);
        if (pResponse == NULL || pResponse->m_actionList.GetCount() == 0) {
            continue;
        }

        POSITION actionPos = pResponse->m_actionList.GetHeadPosition();
        CAIAction* pActorAction = pResponse->m_actionList.GetNext(actionPos);
        if (pActorAction == NULL) {
            continue;
        }

        CAIAction actorAction(*pActorAction);
        actorAction.Decode(this);

        CGameObject* pObject = actorAction.m_acteeID.GetObjectWithType(this,
            CGameObject::TYPE_AIBASE,
            FALSE);
        if (pObject == NULL) {
            continue;
        }

        CAIResponse response;
        response.m_weight = pResponse->m_weight;
        response.m_responseNum = pResponse->m_responseNum;
        response.m_responseSetNum = pResponse->m_responseSetNum;
        response.m_scriptNum = pResponse->m_scriptNum;

        while (actionPos != NULL) {
            CAIAction* pAction = pResponse->m_actionList.GetNext(actionPos);
            if (pAction != NULL) {
                response.Add(*pAction);
                queuedCount++;
            }
        }

        CMessage* responseMsg = new CMessageInsertResponse(response,
            FALSE,
            FALSE,
            FALSE,
            m_id,
            pObject->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(responseMsg, FALSE);

        if ((pObject->GetObjectType() & CGameObject::TYPE_AIBASE) != 0) {
            CMessage* cutSceneMsg = new CMessageSetInCutScene(TRUE,
                m_id,
                pObject->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(cutSceneMsg, FALSE);
        }

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
                pObject->GetId(),
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
    }


    return ACTION_DONE;
}

// Case body for FloatMessage (0xF1), at 0x4507E0 inside ExecuteAction --
// not a function of its own, so no address marker.
// Resolves m_acteeID, queues a CMessageFloatText so the strref (m_specificID)
// is displayed above the target sprite.  Binary takes an SP fast path
// calling FUN_004C80E0 directly; routing through CMessageFloatText::Run
// reaches the same display routine on both SP and MP.
SHORT CGameAIBase::FloatMessage()
{
    CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
    if (pObj == NULL) {
        return ACTION_DONE;
    }

    LONG targetId = pObj->m_id;
    CMessage* msg = new CMessageFloatText(m_id,
        targetId,
        m_curAction.m_specificID,
        FALSE);
    g_pBaldurChitin->GetMessageHandler()->AddMessage(msg, FALSE);

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
        targetId,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);
    return ACTION_DONE;
}

// Case body for HideCreature (0xE9), at 0x452020 inside ExecuteAction --
// not a function of its own, so no address marker.
// Resolves target via m_acteeID and calls SetStealthState(m_specificID).
// TODO: Skips the FUN_0045BDD0 immunity/distance filter and the MP
// broadcast (CMessage90) -- SP semantics are preserved.
SHORT CGameAIBase::HideCreature()
{
    CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
    if (pObj == NULL) {
        return ACTION_DONE;
    }

    if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
        pSprite->SetStealthState(m_curAction.m_specificID);
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
        pObj->m_id,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);
    return ACTION_DONE;
}

// 0x4525CC - case body for WaitAnimation (0x143).
// Returns ACTION_INTERRUPTABLE while the resolved target's current
// animation sequence matches m_specificID, ACTION_DONE otherwise.
// TODO: Binary uses FUN_0045BDD0 which adds an immunity check and a
// PC-distance cutoff (param_1[0x248] & 0x40000U).  Skipped here -- both
// are filters; the core semantics match.
SHORT CGameAIBase::WaitAnimation()
{
    CGameObject* pObj = ResolveActionTarget(CGameObject::TYPE_SPRITE);
    if (pObj == NULL) {
        return ACTION_DONE;
    }

    SHORT result = ACTION_DONE;
    if ((pObj->GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(pObj);
        if (pSprite->GetSequence() == m_curAction.m_specificID) {
            result = ACTION_INTERRUPTABLE;
        }
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
        pObj->m_id,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);
    return result;
}

// 0x460300 - handles IncrementGlobal (0x6D).  Like SetGlobal but adds
// m_specificID to existing value; if variable absent, creates it with
// value = m_specificID.
// TODO: Multiplayer broadcast (same CMessage as SetGlobal) skipped.
SHORT CGameAIBase::IncrementGlobal()
{
    CString sScope;
    CString sName;
    SplitScriptVariableName(m_curAction.GetString1(), sScope, sName);
    LONG nDelta = m_curAction.m_specificID;

    if (sScope == CString("GLOBAL")) {
        CVariableHash* pHash = g_pBaldurChitin->GetObjectGame()->GetVariables();
        CVariable* pVar = pHash->FindKey(sName);
        if (pVar != NULL) {
            pVar->m_intValue += nDelta;
        } else {
            CVariable v;
            v.SetName(sName);
            v.m_intValue = nDelta;
            pHash->AddKey(v);
        }
        return ACTION_DONE;
    }

    if (sScope == CString("LOCALS")) {
        if ((GetObjectType() & CGameObject::TYPE_SPRITE) != 0) {
            CGameSprite* pSprite = static_cast<CGameSprite*>(this);
            CVariableHash* pHash = pSprite->GetLocalVariables();
            CVariable* pVar = pHash->FindKey(sName);
            if (pVar != NULL) {
                pVar->m_intValue += nDelta;
            } else {
                CVariable v;
                v.SetName(sName);
                v.m_intValue = nDelta;
                pHash->AddKey(v);
            }
        }
        return ACTION_DONE;
    }

    CString sAreaName = sScope;
    if (sScope == CString("MYAREA") && m_pArea != NULL) {
        sAreaName = CResRef(reinterpret_cast<BYTE*>(m_pArea->m_header.m_areaName)).GetResRefStr();
    }

    CGameArea* pArea = g_pBaldurChitin->GetObjectGame()->GetArea(sAreaName);
    if (pArea != NULL) {
        CVariableHash* pHash = pArea->GetVariables();
        CVariable* pVar = pHash->FindKey(sName);
        if (pVar != NULL) {
            pVar->m_intValue += nDelta;
        } else {
            CVariable v;
            v.SetName(sName);
            v.m_intValue = nDelta;
            pHash->AddKey(v);
        }
    }

    return ACTION_DONE;
}

// 0x464950
BOOL CGameAIBase::PlaceItem(CItem* pItem, BOOL haveDeny, BOOL dropUnplaced, DWORD num, BOOL feedback)
{
    if (pItem == NULL) {
        return TRUE;
    }

    // A stack of `num` is delivered one unit at a time: split a fresh copy off
    // for the remainder and recurse, then place `pItem` itself below. The copy
    // is made before the flag bit below is set, and the recursion sets it on the
    // copy in turn.
    if (num > 1) {
        PlaceItem(new CItem(*pItem), haveDeny, dropUnplaced, num - 1, TRUE);
    }

    pItem->m_flags |= 2;

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();

    if (pGame->IsFamiliar(m_id)) {
        // A familiar carries nothing of its own -- the item is messaged to the
        // protagonist instead.
        CMessage* pMessage = new CMessageAddItem(*pItem, m_id, pGame->GetProtagonist());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
        return TRUE;
    }

    if (pGame->GetCharacterPortraitNum(m_id) != -1) {
        // Party member: scan the personal (backpack) slots UPWARD from 18, the
        // same base `CGameSpriteEquipment::GetUsedSlotsCount` walks.
        CGameSprite* pSprite = static_cast<CGameSprite*>(this);

        INT nIndex;
        for (nIndex = 0; nIndex < CScreenInventory::PERSONAL_INVENTORY_SIZE; nIndex++) {
            if (pSprite->m_equipment.m_items[18 + nIndex] == NULL) {
                break;
            }
        }

        if (nIndex < CScreenInventory::PERSONAL_INVENTORY_SIZE) {
            if (haveDeny) {
                // The caller already owns the lock, so neither the deny pair nor
                // the multiplayer broadcast below runs.
                pSprite->m_equipment.m_items[18 + nIndex] = pItem;
                return TRUE;
            }

            CGameObject* pObjectTemp;

            BYTE rc;
            do {
                rc = pGame->GetObjectArray()->GetDeny(m_id,
                    CGameObjectArray::THREAD_ASYNCH,
                    &pObjectTemp,
                    INFINITE);
            } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

            if (rc == CGameObjectArray::SUCCESS) {
                pSprite->m_equipment.m_items[18 + nIndex] = pItem;
                pGame->GetObjectArray()->ReleaseDeny(m_id,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }

            // Broadcast even when the lock was not acquired and nothing was
            // stored -- the original does not gate this on `rc`.
            if (g_pChitin->cNetwork.GetServiceProvider() != CNetwork::SERV_PROV_NULL
                && g_pChitin->cNetwork.m_idLocalPlayer != m_remotePlayerID) {
                CMessage* pMessage = new CMessageAddItem(*pItem, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
            }

            return TRUE;
        }

        // Pack full: the item drops on the ground pile instead.
        if (!dropUnplaced) {
            return FALSE;
        }

        LONG nContainerId = pGame->GetGroundPile(m_id);
        if (nContainerId == CGameObjectArray::INVALID_INDEX) {
            return FALSE;
        }

        CGameObject* pObjectTemp;

        BYTE rc = pGame->GetObjectArray()->GetDeny(nContainerId,
            CGameObjectArray::THREAD_ASYNCH,
            &pObjectTemp,
            INFINITE);
        if (rc == CGameObjectArray::SUCCESS) {
            static_cast<CGameContainer*>(pObjectTemp)->PlaceItemInBlankSlot(pItem, TRUE, SHORT_MAX);
            pGame->GetObjectArray()->ReleaseDeny(nContainerId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }

        return FALSE;
    }

    if (m_objectType == TYPE_SPRITE) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(this);

        if (feedback) {
            pGame->FeedBack(CInfGame::FEEDBACK_ITEMLOST, 0, TRUE);
        }

        // Non-party sprites scan DOWNWARD, and over a different range than the
        // party scan above: m_items[42] down to m_items[15]. Both the direction
        // and the bounds differ -- this is not the same loop.
        INT nIndex;
        for (nIndex = 27; nIndex >= 0; nIndex--) {
            if (pSprite->m_equipment.m_items[15 + nIndex] == NULL) {
                break;
            }
        }

        if (nIndex >= 0) {
            if (haveDeny) {
                pSprite->m_equipment.m_items[15 + nIndex] = pItem;
                return TRUE;
            }

            CGameObject* pObjectTemp;

            BYTE rc;
            do {
                rc = pGame->GetObjectArray()->GetDeny(m_id,
                    CGameObjectArray::THREAD_ASYNCH,
                    &pObjectTemp,
                    INFINITE);
            } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

            if (rc == CGameObjectArray::SUCCESS) {
                // NOTE: base 18, not the 15 the scan above found the slot with
                // -- verified byte-exact at 0x464D7B (`89 9C BE 20 4B 00 00`)
                // against its sibling store at 0x464D27 (`... 14 4B 00 00`).
                // The original writes three slots past the free one it located;
                // reproduced as-is rather than "corrected".
                pSprite->m_equipment.m_items[18 + nIndex] = pItem;
                pGame->GetObjectArray()->ReleaseDeny(m_id,
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }

            if (g_pChitin->cNetwork.GetServiceProvider() != CNetwork::SERV_PROV_NULL
                && g_pChitin->cNetwork.m_idLocalPlayer != m_remotePlayerID) {
                CMessage* pMessage = new CMessageAddItem(*pItem, m_id, m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
            }

            return TRUE;
        }

        if (!dropUnplaced) {
            return FALSE;
        }

        LONG nContainerId = pGame->GetGroundPile(m_id);
        if (nContainerId == CGameObjectArray::INVALID_INDEX) {
            return FALSE;
        }

        CGameObject* pObjectTemp;

        BYTE rc = pGame->GetObjectArray()->GetDeny(nContainerId,
            CGameObjectArray::THREAD_ASYNCH,
            &pObjectTemp,
            INFINITE);
        if (rc == CGameObjectArray::SUCCESS) {
            static_cast<CGameContainer*>(pObjectTemp)->PlaceItemInBlankSlot(pItem, TRUE, SHORT_MAX);
            pGame->GetObjectArray()->ReleaseDeny(nContainerId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }

        return FALSE;
    }

    if (m_objectType == TYPE_CONTAINER) {
        static_cast<CGameContainer*>(this)->PlaceItemInBlankSlot(pItem, TRUE, SHORT_MAX);
        return TRUE;
    }

    if (dropUnplaced) {
        delete pItem;
    }

    return FALSE;
}

// 0x463B30
SHORT CGameAIBase::TakePartyItem()
{
    // Serves four ACTION.IDS entries, all routed to this case by the dispatch
    // byte table at 0x4529AC: 116 TakePartyItem(S:Item*), 188
    // TakePartyItemAll(S:Item*), 193 TakePartyItemRange(S:Item*,I:Range*) and
    // 204 TakePartyItemNum(S:Item*,I:Num).  The two variants that carry a
    // second script parameter both read it out of m_specificID, so which
    // meaning it has is decided here, up front.
    CString name = m_curAction.GetString1();

    CItem* pItem = NULL;
    LONG nCount = 1;
    BOOL bNum = FALSE;
    LONG nRange = 0;
    BOOL bRange = FALSE;

    if (m_curAction.m_actionID == 204) {
        bNum = TRUE;
        nCount = m_curAction.m_specificID;
    }

    if (m_curAction.m_actionID == 193) {
        bRange = TRUE;
        nRange = m_curAction.m_specificID;
    }

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();

    for (SHORT nPortrait = 0; nPortrait < pGame->GetNumCharacters(); nPortrait++) {
        int nTaken = 0;
        LONG nCharacterId = pGame->GetCharacterId(nPortrait);

        CGameSprite* sprite;
        BYTE rc;
        do {
            rc = pGame->GetObjectArray()->GetShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&sprite),
                INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

        if (rc != CGameObjectArray::SUCCESS) {
            return 0;
        }

        // TakePartyItemRange only reaches members standing in the same area and
        // within the script's range, measured in search-map squares -- each
        // coordinate is divided down BEFORE the subtraction, so this is not the
        // usual EXACT_SCALE dx^2 + 16dy^2/9 form used elsewhere.
        BOOL bTake = !bRange;
        if (bRange && sprite->m_pArea == m_pArea) {
            const CPoint& ptSprite = sprite->GetPos();

            int nDeltaY = m_pos.y / CPathSearch::GRID_SQUARE_SIZEY
                - ptSprite.y / CPathSearch::GRID_SQUARE_SIZEY;
            int nDeltaX = m_pos.x / CPathSearch::GRID_SQUARE_SIZEX
                - ptSprite.x / CPathSearch::GRID_SQUARE_SIZEX;

            bTake = nDeltaX * nDeltaX + nDeltaY * nDeltaY <= nRange;
        }

        if (bTake) {
            SHORT nSlot = sprite->FindItemPersonal(name, 0, FALSE);
            while (nSlot != -1) {
                // The slot index is into the whole equipment array, so the
                // copy is taken from m_items[0] -- not the +18 personal window
                // PlaceItem scans.
                pItem = new CItem(*sprite->m_equipment.m_items[nSlot]);

                CMessage* pMessage = new CMessageRemoveItem(nSlot, m_id, sprite->m_id);
                g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

                CAbilityId abId;
                abId.m_itemType = 2;
                abId.m_itemNum = nSlot;
                abId.m_abilityNum = 0;
                sprite->UpdateQuickButtons(abId, 0, TRUE, FALSE);
                abId.m_abilityNum = 1;
                sprite->UpdateQuickButtons(abId, 0, TRUE, FALSE);
                abId.m_abilityNum = 2;
                sprite->UpdateQuickButtons(abId, 0, TRUE, FALSE);

                if (bNum) {
                    if (pItem->GetMaxStackable() > 1) {
                        if (pItem->GetUsageCount(0) > nCount) {
                            // Only part of the stack is wanted: shrink the
                            // taken copy and message the remainder back.
                            pItem->SetUsageCount(0,
                                static_cast<WORD>(pItem->GetUsageCount(0) - nCount));
                            nCount = 0;

                            CMessage* pRemainder = new CMessageAddItem(*pItem, m_id, sprite->m_id);
                            g_pBaldurChitin->GetMessageHandler()->AddMessage(pRemainder, FALSE);
                        } else {
                            nCount -= pItem->GetUsageCount(0);
                        }
                    } else {
                        nCount--;
                    }
                }

                PlaceItem(pItem, TRUE, TRUE, 1, TRUE);

                if (m_curAction.m_actionID != 188 && m_curAction.m_actionID != 204) {
                    // Plain TakePartyItem / TakePartyItemRange take exactly one.
                    pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                    return ACTION_DONE;
                }

                nTaken++;

                if (m_curAction.m_actionID == 204 && nCount < 1) {
                    pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                    return ACTION_DONE;
                }

                // Skip the copies already taken off this member so the search
                // advances instead of finding the same slot again.
                nSlot = sprite->FindItemPersonal(name, nTaken, FALSE);
            }

            if (pItem == NULL
                || m_curAction.m_actionID == 188
                || (m_curAction.m_actionID == 204 && nCount > 0)) {
                SHORT nBags = sprite->TakeItemBags(name, nCount, -1);
                if (nBags > 0) {
                    nCount -= nBags;

                    // Nothing came back from the bags but a count, so the
                    // replacement is built from the resref alone.
                    PlaceItem(new CItem(CResRef(name), 1, 0, 0, 0, 0),
                        TRUE,
                        TRUE,
                        nBags,
                        TRUE);
                }

                if (m_curAction.m_actionID == 204 && nCount < 1) {
                    pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                        CGameObjectArray::THREAD_ASYNCH,
                        INFINITE);
                    return ACTION_DONE;
                }
            }
        }

        pGame->GetObjectArray()->ReleaseShare(nCharacterId,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }

    if (pItem != NULL) {
        return ACTION_DONE;
    }

    return ACTION_ERROR;
}

// Serves the two action ids the dispatch byte table at 0x4529AC routes to case
// 0x5C: 220 and 226.  A sweep of all 326 entries found no third.  Neither id is
// listed in IWD2's own ACTION.IDS -- that file has no entry at all between 205
// and 228 -- so both are engine-internal leftovers here; in BG2 they are
// TakeItemListParty and TakeItemListPartyNum /*#guess*/, which matches what the
// code does: 226 is the one that carries a count, read out of m_specificID.
//
// The binary compares m_actionID against the immediate 226, not against a
// CAIAction constant loaded from the ordinal table, so the literal is faithful.
//
// 0x464200
SHORT CGameAIBase::TakePartyItemList()
{
    C2DArray itemList;
    CString name;

    itemList.Load(CResRef(m_curAction.GetString1()));

    LONG nCount = -1;
    if (m_curAction.m_actionID == 226) {
        nCount = m_curAction.m_specificID;
    }

    for (int nRow = 0; nRow < itemList.GetHeight(); nRow++) {
        // Only column 0 of each row is read -- the rest of the 2DA is ignored.
        name = itemList.GetAt(CPoint(0, nRow));

        CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
        BOOL found = FALSE;

        for (SHORT nPortrait = 0; nPortrait < pGame->GetNumCharacters(); nPortrait++) {
            int nTaken = 0;
            LONG nCharacterId = pGame->GetCharacterId(nPortrait);

            CGameSprite* sprite;
            BYTE rc;
            do {
                rc = pGame->GetObjectArray()->GetShare(nCharacterId,
                    CGameObjectArray::THREAD_ASYNCH,
                    reinterpret_cast<CGameObject**>(&sprite),
                    INFINITE);
            } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

            if (rc != CGameObjectArray::SUCCESS) {
                return 0;
            }

            SHORT nSlot = sprite->FindItemPersonal(name, 0, FALSE);
            if (nSlot != -1) {
                found = TRUE;

                do {
                    // The slot index is into the whole equipment array, so the
                    // copy is taken from m_items[0] -- not the +18 personal
                    // window PlaceItem scans.
                    CItem* pItem = new CItem(*sprite->m_equipment.m_items[nSlot]);

                    CMessage* pMessage = new CMessageRemoveItem(nSlot, m_id, sprite->m_id);
                    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

                    CAbilityId abId;
                    abId.m_itemType = 2;
                    abId.m_itemNum = nSlot;
                    abId.m_abilityNum = 0;
                    sprite->UpdateQuickButtons(abId, 0, TRUE, FALSE);
                    abId.m_abilityNum = 1;
                    sprite->UpdateQuickButtons(abId, 0, TRUE, FALSE);
                    abId.m_abilityNum = 2;
                    sprite->UpdateQuickButtons(abId, 0, TRUE, FALSE);

                    PlaceItem(pItem, TRUE, TRUE, 1, TRUE);

                    nTaken++;

                    // Skip the copies already taken off this member so the
                    // search advances instead of finding the same slot again.
                    nSlot = sprite->FindItemPersonal(name, nTaken, FALSE);
                } while (nSlot != -1);
            }

            // The bags are emptied of this item unconditionally, and of every
            // copy in them -- unlike TakePartyItem this pass has no per-member
            // limit, so the count that comes back is not subtracted from
            // nCount, and the replacement is placed without feedback.
            SHORT nBags = sprite->TakeItemBags(name, SHORT_MAX, -1);
            if (nBags > 0) {
                found = TRUE;

                // Nothing came back from the bags but a count, so the
                // replacement is built from the resref alone.
                PlaceItem(new CItem(CResRef(name), 1, 0, 0, 0, 0),
                    TRUE,
                    TRUE,
                    nBags,
                    FALSE);
            }

            pGame->GetObjectArray()->ReleaseShare(nCharacterId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }

        if (found && m_curAction.m_actionID == 226 && --nCount < 1) {
            return ACTION_DONE;
        }
    }

    // nCount still holding its starting value means nothing was ever taken, in
    // which case the counted variant hands the actor the list's default item
    // instead -- but only if that resref really is an item in the key table.
    if (nCount == m_curAction.m_specificID && m_curAction.m_actionID == 226) {
        CString newItem = itemList.GetDefault();
        newItem.MakeUpper();

        if (g_pBaldurChitin->cDimm.m_cKeyTable.FindKey(CResRef(newItem), 1005, TRUE) != NULL) {
            PlaceItem(new CItem(CResRef(newItem), 0, 0, 0, 0, 0),
                TRUE,
                TRUE,
                1,
                TRUE);
        }
    }

    return ACTION_DONE;
}

// 0x465110
SHORT CGameAIBase::TakePartyGold()
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();

    DWORD gold = m_curAction.m_specificID;
    if (gold > pGame->GetGameSave()->m_nPartyGold) {
        gold = pGame->GetGameSave()->m_nPartyGold;
    }

    if (m_objectType == TYPE_SPRITE) {
        static_cast<CGameSprite*>(this)->GetBaseStats()->m_gold += gold;
        static_cast<CGameSprite*>(this)->GetDerivedStats()->m_nGold += gold;
    }

    CMessagePartyGold* pMessage = new CMessagePartyGold(TRUE,
        TRUE,
        -static_cast<LONG>(gold),
        m_id,
        m_id);

    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

    return ACTION_DONE;
}

// 0x4651A0
SHORT CGameAIBase::GivePartyGold()
{
    DWORD gold = m_curAction.m_specificID;

    if (m_objectType == TYPE_SPRITE && m_curAction.m_actionID == CAIAction::GIVEPARTYGOLD) {
        if (gold > static_cast<CGameSprite*>(this)->GetDerivedStats()->m_nGold) {
            gold = static_cast<CGameSprite*>(this)->GetDerivedStats()->m_nGold;
        }

        static_cast<CGameSprite*>(this)->GetBaseStats()->m_gold -= gold;
        static_cast<CGameSprite*>(this)->GetDerivedStats()->m_nGold -= gold;
    }

    CMessagePartyGold* pMessage = new CMessagePartyGold(TRUE,
        TRUE,
        gold,
        m_id,
        m_id);

    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

    return ACTION_DONE;
}

// 0x4654C0
SHORT CGameAIBase::GiveOrder(CGameAIBase* sprite)
{
    if (sprite == NULL) {
        return ACTION_ERROR;
    }

    CAITrigger trigger(CAITrigger::RECEIVEDORDER, m_typeAI, m_curAction.m_specificID);

    CMessageSetTrigger* pMessage = new CMessageSetTrigger(trigger,
        m_id,
        sprite->GetId());
    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

    return ACTION_DONE;
}

// 0x465630
SHORT CGameAIBase::DisplayString(CGameAIBase* sprite)
{
    STR_RES strRes;
    COLORREF rgbColor = RGB(0, 0, 0);
    STRREF name;

    if (sprite == NULL) {
        return ACTION_ERROR;
    }

    if (sprite->GetObjectType() == TYPE_SPRITE) {
        rgbColor = CVidPalette::RANGE_COLORS[static_cast<CGameSprite*>(sprite)->GetBaseStats()->m_colors[2]];
        name = static_cast<CGameSprite*>(sprite)->GetNameRef();
    } else {
        name = -1;
    }

    g_pBaldurChitin->GetTlkTable().Fetch(m_curAction.m_specificID, strRes);
    strRes.szText.TrimLeft();

    if (sprite->m_typeAI.GetEnemyAlly() == CAIObjectType::EA_PC) {
        if (strRes.cSound.m_nLooping == 0) {
            strRes.cSound.SetFireForget(TRUE);
        }

        if (sprite->GetObjectType() == TYPE_SPRITE) {
            strRes.cSound.SetChannel(static_cast<CGameSprite*>(sprite)->GetChannel(),
                reinterpret_cast<DWORD>(m_pArea));
        } else {
            strRes.cSound.SetChannel(14, reinterpret_cast<DWORD>(m_pArea));
        }

        strRes.cSound.Play(m_pos.x, m_pos.y, 0, FALSE);

        if (strRes.szText != "") {
            g_pBaldurChitin->GetBaldurMessage()->DisplayTextRef(name,
                m_curAction.m_specificID,
                rgbColor,
                RGB(160, 200, 215),
                -1,
                m_id,
                sprite->GetId());
        }
    } else {
        if (strRes.cSound.m_nLooping == 0) {
            strRes.cSound.SetFireForget(TRUE);
        }

        if (sprite->GetObjectType() == TYPE_SPRITE) {
            strRes.cSound.SetChannel(static_cast<CGameSprite*>(sprite)->GetChannel(),
                reinterpret_cast<DWORD>(m_pArea));
        } else {
            strRes.cSound.SetChannel(14, reinterpret_cast<DWORD>(m_pArea));
        }

        strRes.cSound.Play(m_pos.x, m_pos.y, 0, FALSE);

        if (strRes.szText != "") {
            g_pBaldurChitin->GetBaldurMessage()->DisplayTextRef(name,
                m_curAction.m_specificID,
                rgbColor,
                RGB(215, 215, 190),
                -1,
                m_id,
                sprite->GetId());
        }
    }

    return ACTION_DONE;
}

// 0x465F30
SHORT CGameAIBase::StartMovie()
{
    CString sMovieFileName;

    CBaldurProjector* pProjector = g_pBaldurChitin->m_pEngineProjector;
    if (!pProjector->ResolveMovieFileName(CResRef(m_curAction.GetString1()), sMovieFileName)) {
        return ACTION_ERROR;
    }

    g_pBaldurChitin->m_pEngineWorld->ReadyMovie(CResRef(m_curAction.GetString1()), FALSE);

    return ACTION_DONE;
}

// 0x466030
SHORT CGameAIBase::RevealAreaOnMap()
{
    CString sArea = m_curAction.GetString1();

    CWorldMap* pWorldMap = g_pBaldurChitin->GetObjectGame()->GetWorldMap(sArea);
    pWorldMap->EnableArea(pWorldMap->GetCurrentMapIndex(),
        CResRef(sArea),
        TRUE);

    g_pBaldurChitin->GetObjectGame()->FeedBack(CInfGame::FEEDBACK_WORLDMAP_UPDATE, 0, TRUE);

    g_pBaldurChitin->GetBaldurMessage()->SendMapWorldRevealArea(m_curAction.GetString1());

    return ACTION_DONE;
}

// 0x466120
SHORT CGameAIBase::ChangeTileState(CGameTiledObject* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    if (m_curAction.m_specificID != 0) {
        if ((target->m_dwFlags & 0x1) != 0) {
            target->ToggleState();
        }
    } else {
        if ((target->m_dwFlags & 0x1) == 0) {
            target->ToggleState();
        }
    }

    return ACTION_DONE;
}

// 0x466170
SHORT CGameAIBase::TriggerActivation(CGameTrigger* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    if (m_curAction.m_specificID != 0) {
        target->m_dwFlags &= ~0x100;
    } else {
        target->m_dwFlags |= 0x100;
    }

    CMessageTriggerStatus* pMessage = new CMessageTriggerStatus(target->m_dwFlags,
        target->m_trapActivated,
        target->m_trapDetected,
        m_id,
        target->GetId());
    g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

    return ACTION_DONE;
}

// 0x4668B0
SHORT CGameAIBase::StartMusic()
{
    if (m_pArea == NULL) {
        return ACTION_ERROR;
    }

    // TODO: Check cast.
    m_pArea->PlaySong(static_cast<SHORT>(m_curAction.m_specificID), m_curAction.m_specificID2);

    return ACTION_DONE;
}

// 0x4668E0
SHORT CGameAIBase::SetMusic()
{
    if (m_pArea == NULL) {
        return ACTION_ERROR;
    }

    if (!m_pArea->SetSong(static_cast<SHORT>(m_curAction.m_specificID), static_cast<BYTE>(m_curAction.m_specificID2))) {
        return ACTION_ERROR;
    }

    return ACTION_DONE;
}

// 0x466A00
SHORT CGameAIBase::FinalSave()
{
    if (g_pChitin->cNetwork.GetSessionOpen()
        && g_pChitin->cNetwork.GetSessionHosting() != TRUE) {
        return ACTION_DONE;
    }

    if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(m_id, CGameObjectArray::THREAD_ASYNCH, INFINITE) != CGameObjectArray::SUCCESS) {
        return ACTION_DONE;
    }

    // NOTE: Looks like inlining.
    if (1) {
        CString sSaveName("000000002-Final-Save");
        g_pBaldurChitin->GetObjectGame()->m_sSaveGame = sSaveName;
        CScreenCharacter::SAVE_NAME = sSaveName;
    }

    g_pBaldurChitin->GetObjectGame()->SaveGame(1, 0, 1);

    CGameObject* pObject;

    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(m_id,
            CGameObjectArray::THREAD_ASYNCH,
            &pObject,
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return ACTION_ERROR;
    }

    return ACTION_DONE;
}

// 0x466B30
SHORT CGameAIBase::Unlock(CGameAIBase* pObject)
{
    CMessage* message;

    if (pObject == NULL) {
        return ACTION_ERROR;
    }

    if (pObject->GetObjectType() != TYPE_DOOR
        && pObject->GetObjectType() != TYPE_CONTAINER) {
        return ACTION_ERROR;
    }

    message = new CMessageSetForceActionPick(TRUE, m_id, pObject->GetId());
    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);

    CAITrigger trigger(CAITrigger::NO_TRIGGER, 0);

    if (pObject->GetObjectType() == TYPE_DOOR
        && (static_cast<CGameDoor*>(pObject)->m_dwFlags & 0x2) != 0) {
        trigger = CAITrigger(CAITrigger::UNLOCKED, m_typeAI, 0);

        message = new CMessageSetTrigger(trigger, m_id, pObject->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);

        static_cast<CGameDoor*>(pObject)->m_dwFlags &= ~0x2;

        message = new CMessageUnlock(static_cast<CGameDoor*>(pObject)->m_dwFlags,
            m_id,
            pObject->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
    }

    if (pObject->GetObjectType() == TYPE_CONTAINER
        && (static_cast<CGameContainer*>(pObject)->m_dwFlags & 0x1) != 0) {
        trigger = CAITrigger(CAITrigger::UNLOCKED, m_typeAI, 0);

        message = new CMessageSetTrigger(trigger, m_id, pObject->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);

        message = new CMessageUnlock(static_cast<CGameContainer*>(pObject)->m_dwFlags & ~0x1,
            m_id,
            pObject->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
    }

    return ACTION_DONE;
}

// 0x466F90
SHORT CGameAIBase::MoveGlobal(CGameSprite* pSprite)
{
    if (pSprite == NULL) {
        return ACTION_ERROR;
    }

    CString sArea = m_curAction.GetString1();
    sArea.MakeUpper();

    CMessage* message = new CMessageMoveGlobal(sArea,
        m_curAction.m_dest,
        m_id,
        pSprite->GetId());
    g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);

    return ACTION_DONE;
}

// 0x467110
SHORT CGameAIBase::Lock(CGameAIBase* pObject)
{
    if (pObject == NULL) {
        return ACTION_ERROR;
    }

    if (pObject->GetObjectType() != TYPE_DOOR
        && pObject->GetObjectType() != TYPE_CONTAINER) {
        return ACTION_ERROR;
    }

    CMessageSetForceActionPick* pSetForceActionPick = new CMessageSetForceActionPick(TRUE,
        m_id,
        pObject->GetId());
    g_pBaldurChitin->GetMessageHandler()->AddMessage(pSetForceActionPick, FALSE);

    CAITrigger trigger(CAITrigger::NO_TRIGGER, 0);

    if (pObject->GetObjectType() == TYPE_DOOR) {
        CGameDoor* pDoor = static_cast<CGameDoor*>(pObject);
        if ((pDoor->m_dwFlags & 0x2) == 0) {
            trigger = CAITrigger(CAITrigger::UNLOCKED, m_typeAI, 0);

            CMessageSetTrigger* pSetTrigger = new CMessageSetTrigger(trigger,
                m_id,
                pDoor->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pSetTrigger, FALSE);

            pDoor->m_dwFlags |= 0x2;

            CMessageUnlock* pUnlock = new CMessageUnlock(pDoor->m_dwFlags,
                m_id,
                pDoor->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pUnlock, FALSE);
        }
    }

    if (pObject->GetObjectType() == TYPE_CONTAINER) {
        CGameContainer* pContainer = static_cast<CGameContainer*>(pObject);
        if ((pContainer->m_dwFlags & 0x1) == 0) {
            trigger = CAITrigger(CAITrigger::UNLOCKED, m_typeAI, 0);

            CMessageSetTrigger* pSetTrigger = new CMessageSetTrigger(trigger,
                m_id,
                pContainer->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pSetTrigger, FALSE);

            CMessageUnlock* pUnlock = new CMessageUnlock(pContainer->m_dwFlags | 0x1,
                m_id,
                pContainer->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pUnlock, FALSE);
        }
    }

    return ACTION_DONE;
}

// 0x467550
SHORT CGameAIBase::DestroyItem()
{
    if (GetObjectType() == TYPE_SPRITE) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(this);
        SHORT slotNum = pSprite->FindItemPersonal(m_curAction.GetString1(), 0, FALSE);
        if (slotNum != -1) {
            pSprite->Unequip(slotNum);
            g_pBaldurChitin->GetObjectGame()->AddDisposableItem(pSprite->GetEquipment()->m_items[slotNum]);
            pSprite->GetEquipment()->m_items[slotNum] = NULL;
            return ACTION_DONE;
        }

        if (pSprite->TakeItemBags(m_curAction.GetString1(), 1, -1) > 0) {
            return ACTION_DONE;
        }

        return ACTION_ERROR;
    }

    if (GetObjectType() == TYPE_CONTAINER) {
        CGameContainer* pContainer = static_cast<CGameContainer*>(this);
        SHORT slotNum = pContainer->FindItemSlot(CResRef(m_curAction.GetString1()));
        if (slotNum != -1) {
            pContainer->GetItem(slotNum);
            pContainer->SetItem(slotNum, NULL);
            pContainer->CompressContainer();
        }
        return ACTION_DONE;
    }

    return ACTION_ERROR;
}

// 0x467720
SHORT CGameAIBase::DetectSecretDoor(CGameDoor* target)
{
    if (target == NULL) {
        return ACTION_ERROR;
    }

    CGameDoor* pDoor;

    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(target->GetId(),
            CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pDoor),
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return ACTION_ERROR;
    }

    if ((pDoor->m_dwFlags & 0x80) != 0) {
        if ((pDoor->m_dwFlags & 0x100) == 0) {
            pDoor->SetDrawPoly(400);

            pDoor->m_dwFlags |= 0x100;

            CMessageDoorStatus* pDoorStatus = new CMessageDoorStatus(pDoor,
                m_id,
                target->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pDoorStatus, FALSE);
        } else {
            pDoor->SetDrawPoly(400);
        }
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(target->GetId(),
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return ACTION_DONE;
}

// 0x467880
SHORT CGameAIBase::FadeToColor()
{
    CMessageFadeColor* pFadeColor = new CMessageFadeColor(TRUE,
        static_cast<BYTE>(m_curAction.m_dest.x),
        static_cast<BYTE>(m_curAction.m_dest.y),
        static_cast<BYTE>(m_curAction.m_specificID),
        m_id,
        m_id);

    g_pBaldurChitin->GetMessageHandler()->AddMessage(pFadeColor, FALSE);

    return ACTION_DONE;
}

// 0x467900
SHORT CGameAIBase::FadeFromColor()
{
    CMessageFadeColor* pFadeColor = new CMessageFadeColor(FALSE,
        static_cast<BYTE>(m_curAction.m_dest.x),
        static_cast<BYTE>(m_curAction.m_dest.y),
        static_cast<BYTE>(m_curAction.m_specificID),
        m_id,
        m_id);

    g_pBaldurChitin->GetMessageHandler()->AddMessage(pFadeColor, FALSE);

    return ACTION_DONE;
}

// 0x467970
SHORT CGameAIBase::FadeColorActivate()
{
    CMessageFadeColor* pFadeColor = new CMessageFadeColor(255,
        static_cast<BYTE>(m_curAction.m_dest.x),
        static_cast<BYTE>(m_curAction.m_dest.y),
        static_cast<BYTE>(m_curAction.m_specificID),
        m_id,
        m_id);

    g_pBaldurChitin->GetMessageHandler()->AddMessage(pFadeColor, FALSE);

    return ACTION_DONE;
}

// 0x4679E0
SHORT CGameAIBase::SpawnPtActivate(CGameSpawning* target)
{
    if (target != NULL) {
        return ACTION_ERROR;
    }

    if (target->GetObjectType() != TYPE_SPAWNING) {
        return ACTION_ERROR;
    }

    if (!target->m_spawningObject.m_activated) {
        CMessageSpawnPtActivate* pMessage = new CMessageSpawnPtActivate(TRUE,
            m_id,
            target->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
    }

    return ACTION_DONE;
}

// 0x467A80
SHORT CGameAIBase::SpawnPtDeactivate(CGameSpawning* target)
{
    if (target != NULL) {
        return ACTION_ERROR;
    }

    if (target->GetObjectType() != TYPE_SPAWNING) {
        return ACTION_ERROR;
    }

    if (target->m_spawningObject.m_activated) {
        CMessageSpawnPtActivate* pMessage = new CMessageSpawnPtActivate(FALSE,
            m_id,
            target->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
    }

    return ACTION_DONE;
}

// 0x467B10
SHORT CGameAIBase::SpawnPtSpawn(CGameSpawning* target)
{
    if (target != NULL) {
        return ACTION_ERROR;
    }

    if (target->GetObjectType() != TYPE_SPAWNING) {
        return ACTION_ERROR;
    }

    if (target->m_spawningObject.m_activated) {
        CMessageSpawnPtSpawn* pMessage = new CMessageSpawnPtSpawn(m_pos,
            m_id,
            target->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);
    }

    return ACTION_DONE;
}

// 0x467BB0
SHORT CGameAIBase::StaticStart(CGameStatic* target, BOOL bStart)
{
    if (target != NULL) {
        return ACTION_ERROR;
    }

    if (target->GetObjectType() != TYPE_STATIC) {
        return ACTION_ERROR;
    }

    // TODO: Check, not sure if that's right.
    if (bStart == (target->m_header.m_dwFlags & 0x8)) {
        CMessageStaticStart* pMessage = new CMessageStaticStart(bStart,
            m_id,
            target->GetId());
    }

    return ACTION_DONE;
}

// 0x460D60
SCRIPTNAME& CGameAIBase::GetScriptName()
{
    return m_scriptName;
}

// 0x50A400
void CGameAIBase::SetTrigger(const CAITrigger& trigger)
{
    m_pendingTriggers.AddTail(new CAITrigger(trigger));
}

// NOTE: Inlined.
void CGameAIBase::SetDefaultScript(CAIScript* script)
{
    if (m_movementScript != NULL) {
        delete m_movementScript;
    }

    m_movementScript = script;
}

// 0x6F2C20
SHORT CGameAIBase::GetVisualRange()
{
    return m_pArea->m_visibility.m_nSearchRangeH * 32;
}

// 0x6F2C30
SHORT CGameAIBase::GetHelpRange()
{
    return m_pArea->m_visibility.m_nSearchRangeH * 48;
}

// NOTE: Inlined.
void CGameAIBase::ResetCurrResponse()
{
    if (m_curAction.m_actionID == CAIAction::NO_ACTION) {
        m_curResponseNum = -1;
        m_curResponseSetNum = -1;
        m_curScriptNum = -1;
    }
}

// 0x45B970
CAIAction& CGameAIBase::GetNextAction(CAIAction& action)
{
    while (!m_queuedActions.IsEmpty()) {
        CAIAction* node = m_queuedActions.RemoveHead();
        if (node->GetActionID() != CAIAction::NO_ACTION) {
            action = *node;

            CAIObjectType actorType(node->m_actorID);
            if (actorType.OfType(CAIObjectType::ANYONE, FALSE, FALSE)
                && actorType.GetName() == ""
                && actorType.m_SpecialCase[0] == 0) {
                delete node;
                return action;
            }

            actorType.Decode(this);
            CGameAIBase* actor = static_cast<CGameAIBase*>(actorType.GetObjectWithType(this, CGameObject::TYPE_AIBASE, FALSE));
            if (actor != NULL) {
                action.m_actorID = CAIObjectType::ANYONE;
                action.m_internalFlags |= 0x1;

                CMessage* message = new CMessageInsertAction(action, m_id, actor->GetId());
                g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);

                g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(actor->GetId(),
                    CGameObjectArray::THREAD_ASYNCH,
                    INFINITE);
            }
        }
        delete node;
    }

    action = CAIAction::NULL_ACTION;
    return action;
}

// 0x481890
void CGameAIBase::SplitRectIntoGrid(CRect* r, CArray<CRect*>& ary)
{
    const INT width = r->right - r->left;
    const INT height = r->bottom - r->top;

    INT xCount = 1;
    INT yCount = 1;
    if (width > 512) {
        xCount = width / 512 + 1;
    }
    if (height > 512) {
        yCount = height / 512 + 1;
    }

    if (width <= 512 && height <= 512) {
        ary.SetSize(0, -1);
        ary.Add(r);
        return;
    }

    ary.SetSize(0, 4);
    for (INT y = 0; y < yCount; y++) {
        const INT top = r->top + 512 * y;
        if (top > r->bottom) {
            return;
        }

        for (INT x = 0; x < xCount; x++) {
            const INT left = r->left + 512 * x;
            if (left > r->right) {
                break;
            }

            CRect* pRect = new CRect();
            pRect->left = left;
            pRect->top = top;
            pRect->right = left + min(r->right - left, 512);
            pRect->bottom = top + min(r->bottom - top, 512);
            ary.Add(pRect);
        }
    }
}

// 0x467C50
void CGameAIBase::FireSpell(const CResRef& resRef, CGameObject* target)
{
    // TODO: Incomplete.
}

// 0x4681E0
void CGameAIBase::FireSpellPoint(const CResRef& resRef, const CPoint& ptTarget)
{
    // TODO: Incomplete.
}

// 0x4686C0
CVariable* CGameAIBase::GetGlobalVariable(const CString& sScope, const CString& sName, int a3)
{
    CVariable* pVariable;

    if (sScope == CString("GLOBAL")) {
        pVariable = g_pBaldurChitin->GetObjectGame()->GetVariables()->FindKey(sName);
        if (pVariable != NULL) {
            return pVariable;
        }
    } else {
        CGameArea* pArea = g_pBaldurChitin->GetObjectGame()->GetArea(sScope);
        if (pArea != NULL) {
            pVariable = pArea->GetVariables()->FindKey(sName);
            if (pVariable != NULL) {
                return pVariable;
            }
        }
    }

    CVariable variable;
    variable.SetName(sName);
    g_pBaldurChitin->GetObjectGame()->GetVariables()->AddKey(variable);

    return g_pBaldurChitin->GetObjectGame()->GetVariables()->FindKey(sName);
}

// 0x4530F0
void CGameAIBase::SetAIType342(const CAIObjectType& type)
{
    field_342.Set(type);
}

// 0x453110
void CGameAIBase::SetAIType37E(const CAIObjectType& type)
{
    field_37E.Set(type);
}

// 0x453130
void CGameAIBase::SetLastActionReturn(SHORT returnValue)
{
    m_nLastActionReturn = returnValue;
}

// 0x45B6D0
int CGameAIBase::GetAICounter58C()
{
    return field_58C;
}

// 0x45B6E0
void CGameAIBase::SetAIType3BA(const CAIObjectType& type)
{
    field_3BA.Set(type);
}

// -----------------------------------------------------------------------------

// 0x45E250
CGameAIArea::CGameAIArea()
{
    BYTE rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE);
    if (rc != CGameObjectArray::SUCCESS) {
        delete this;
    }
}

// 0x45E2D0
CGameAIArea::~CGameAIArea()
{
}

// 0x47C830
BOOLEAN CGameAIArea::CanSaveGame(STRREF& strError)
{
    strError = -1;
    return TRUE;
}

// 0x766660
BOOLEAN CGameAIArea::CompressTime(DWORD deltaTime)
{
    return TRUE;
}

// -----------------------------------------------------------------------------

// 0x45E2E0
CGameAIGame::CGameAIGame()
{
    BYTE rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE);
    if (rc != CGameObjectArray::SUCCESS) {
        delete this;
    }
}

// 0x45E2D0
CGameAIGame::~CGameAIGame()
{
}

// 0x47C830
BOOLEAN CGameAIGame::CanSaveGame(STRREF& strError)
{
    strError = -1;
    return TRUE;
}

// 0x766660
BOOLEAN CGameAIGame::CompressTime(DWORD deltaTime)
{
    return TRUE;
}

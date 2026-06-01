#include "CGameDialog.h"

#include "CAIResponse.h"
#include "CVidPalette.h"
#include "CAIScriptFile.h"
#include "CBaldurChitin.h"
#include "CGameArea.h"
#include "CGameJournal.h"
#include "CGameObjectArray.h"
#include "CGameSprite.h"
#include "CInfGame.h"
#include "CMessage.h"
#include "CResDLG.h"
#include "CScreenWorld.h"
#include "CUIControlButton.h"
#include "CUIControlTextDisplay.h"
#include "CUIManager.h"
#include "CUIPanel.h"
#include "CUtil.h"
#include "DebugLog.h"

// Splits a raw DLG script blob (one or more BIOC-style "Trigger(args)"
// expressions juxtaposed without separators) into the line-per-call shape
// CAIScriptFile::Parse{Conditional,Response}String expects. Binary at
// 0x483527..0x4836f0 keeps two CStrings -- a shrinking source and a growing
// accumulator -- and walks ')' to ')'.
static CString DLGNormalize(const char* pBytes, int nLen)
{
    CString sSource(pBytes, nLen);
    sSource.TrimLeft();
    sSource.TrimRight();

    CString sOut;
    while (sSource.GetLength() > 0) {
        INT pos = sSource.Find(')');
        if (pos < 0) {
            // Binary logs a "Error found while parsing dialog conditional"
            // assertion here; we let ParseConditionalString swallow the tail.
            sOut += sSource;
            break;
        }
        sOut += sSource.Left(pos + 1);
        sOut += '\n';
        sSource = sSource.Right(sSource.GetLength() - pos - 1);
        sSource.TrimLeft();
    }
    return sOut;
}

// NOTE: Inlined.
CGameDialogSprite::CGameDialogSprite()
{
    ClearMarshal();
}

// 0x67CB50
CGameDialogSprite::~CGameDialogSprite()
{
    ClearMarshal();
}

// 0x483970
void CGameDialogSprite::ClearMarshal()
{
    m_characterIndex = 0;
    m_talkerIndex = 0;
    m_dialogFreezeCounter = 6;
    m_dialogFreezeMultiplayer = 0;
    m_file = "";
    m_waitingForResponse = FALSE;
    m_currentEntryIndex = 0;
    m_responseMarker = -1;

    for (INT nIndex = 0; nIndex < m_dialogEntries.GetCount(); nIndex++) {
        CGameDialogEntry* pEntry = m_dialogEntries.GetAt(nIndex);
        if (pEntry != NULL) {
            delete pEntry;
        }
    }

    m_dialogEntries.RemoveAll();
    m_dialogEntriesOrdered.RemoveAll();
}

// 0x4839F0
BOOL CGameDialogSprite::StartDialog(CGameSprite* pSprite)
{
    g_pBaldurChitin->m_pEngineWorld->DisableKeyRepeat();

    m_nMusicThreadPriority = GetThreadPriority(g_pChitin->m_hMusicThread);
    if (GetPrivateProfileIntA("Program Options", "Volume Music", 0, g_pChitin->GetIniFileName())) {
        m_bMusicThreadPriorityChanged = SetThreadPriority(g_pChitin->m_hMusicThread, 15);
    }

    Iwd2DebugLog("CGameDialogSprite::StartDialog talkerId=%ld entryCount=%d",
        pSprite->GetId(),
        m_dialogEntriesOrdered.GetCount());

    for (INT nIndex = 0; nIndex < m_dialogEntriesOrdered.GetCount(); nIndex++) {
        CGameDialogEntry* pEntry = m_dialogEntriesOrdered.GetAt(nIndex);
        BOOL held = pEntry != NULL
            && pEntry->m_startCondition.Hold(CTypedPtrList<CPtrList, CAITrigger*>(), pSprite);
        Iwd2DebugLog("CGameDialogSprite::StartDialog entry=%d valid=%d held=%d dialogIndex=%d",
            nIndex, pEntry != NULL, held, pEntry ? pEntry->m_dialogIndex : -1);
        if (held) {
            // FIXME: Unused.
            LONG nCharacterId = g_pBaldurChitin->GetObjectGame()->GetProtagonist();

            CMessage* pMessage = new CMessageEnterDialog(pEntry->m_dialogIndex,
                TRUE,
                pSprite->GetId(),
                pSprite->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

            m_bDialogActive = 1;
            m_sScrollMarker = "";

            Iwd2DebugLog("CGameDialogSprite::StartDialog ENTERED talkerId=%ld dialogIndex=%d",
                pSprite->GetId(), pEntry->m_dialogIndex);
            return TRUE;
        }
    }

    Iwd2DebugLog("CGameDialogSprite::StartDialog NO MATCH talkerId=%ld", pSprite->GetId());
    return FALSE;
}

// 0x482210
void CGameDialogSprite::Initialize(CResRef file, LONG characterIndex, LONG talkerIndex)
{
    if (file == "") {
        ClearMarshal();
        return;
    }

    char szFile[16];
    file.CopyToString(szFile);

    if (file == m_file) {
        m_characterIndex = characterIndex;
        g_pBaldurChitin->GetObjectGame()->SetProtagonist(characterIndex);
        m_talkerIndex = talkerIndex;
        Iwd2DebugLog("CGameDialogSprite::Initialize REUSE file='%s' entries=%d",
            szFile, m_dialogEntriesOrdered.GetCount());
        return;
    }

    CResRef cNewFile;
    CResDLG* pRes = NULL;
    BOOL bRequested = FALSE;

    if (file != "") {
        pRes = static_cast<CResDLG*>(g_pChitin->cDimm.GetResObject(file, 0x3F3, TRUE));
        if (pRes == NULL) {
            Iwd2DebugLog("CGameDialogSprite::Initialize RESOURCE NOT FOUND file='%s'", szFile);
            cNewFile = "";
        } else {
            bRequested = TRUE;
            static_cast<CRes*>(pRes)->Request();
            cNewFile = file;
        }
    }

    BOOL bValid = FALSE;
    DWORD nProbeSize = 0;
    if (pRes != NULL && pRes->Demand() != NULL) {
        nProbeSize = pRes->m_nResSizeActual;
        static_cast<CRes*>(pRes)->Release();
        bValid = (nProbeSize != 0);
    }

    if (!bValid) {
        Iwd2DebugLog("CGameDialogSprite::Initialize INVALID file='%s' pRes=%p probeSize=%lu",
            szFile, pRes, nProbeSize);
        ClearMarshal();
    } else {
        void* pData = NULL;
        DWORD nSize = 0;
        if (pRes != NULL) {
            if (pRes->Demand() != NULL) {
                nSize = pRes->m_nResSizeActual;
                static_cast<CRes*>(pRes)->Release();
            }
            pData = pRes->Demand();
        }

        Iwd2DebugLog("CGameDialogSprite::Initialize LOADING file='%s' pData=%p nSize=%lu",
            szFile, pData, nSize);

        LoadEntries(pData, nSize, characterIndex, talkerIndex);

        m_file = file;

        Iwd2DebugLog("CGameDialogSprite::Initialize LOADED file='%s' entries=%d ordered=%d",
            szFile, m_dialogEntries.GetCount(), m_dialogEntriesOrdered.GetCount());

        if (pRes != NULL) {
            static_cast<CRes*>(pRes)->Release();
        }
    }

    if (pRes != NULL && cNewFile != "") {
        if (bRequested) {
            static_cast<CRes*>(pRes)->CancelRequest();
            bRequested = FALSE;
        }
        g_pChitin->cDimm.ReleaseResObject(pRes);
    }
}

// 0x482a40
void CGameDialogSprite::LoadEntries(void* pData, DWORD nSize, LONG characterIndex, LONG talkerIndex)
{
    // __FILE__: C:\Projects\Icewind2\src\Baldur\CGameDialog.cpp
    // __LINE__: 0x1ab
    UTIL_ASSERT(pData != NULL);

    if (pData == NULL) {
        return;
    }

    ClearMarshal();

    // __LINE__: 0x1c0
    UTIL_ASSERT(nSize > 0x30);

    // DLG V1.0 header layout (40 bytes starting at offset 8, after the "DLG V1.0" magic).
    struct DLGHeader {
        DWORD numStates;
        DWORD statesOffset;
        DWORD numTransitions;
        DWORD transitionsOffset;
        DWORD stateTriggersOffset;
        DWORD numStateTriggers;
        DWORD transitionTriggersOffset;
        DWORD numTransitionTriggers;
        DWORD actionsOffset;
        DWORD numActions;
    } hdr;
    memcpy(&hdr, static_cast<BYTE*>(pData) + 8, sizeof(hdr));

    // Raw record tables (allocated as flat byte buffers; freed at function exit).
    CPtrArray arrStates;
    CPtrArray arrTrans;
    CPtrArray arrSTrig;
    CPtrArray arrTTrig;
    CPtrArray arrActions;

    if (hdr.numStates != 0) {
        DWORD* src = reinterpret_cast<DWORD*>(static_cast<BYTE*>(pData) + hdr.statesOffset);
        for (DWORD i = 0; i < hdr.numStates; i++) {
            DWORD* rec = static_cast<DWORD*>(malloc(16));
            UTIL_ASSERT(rec != NULL);
            memset(rec, 0, 16);
            memcpy(rec, src, 16);
            arrStates.Add(rec);
            src += 4;
        }
    }

    if (hdr.numTransitions != 0) {
        DWORD* src = reinterpret_cast<DWORD*>(static_cast<BYTE*>(pData) + hdr.transitionsOffset);
        for (DWORD i = 0; i < hdr.numTransitions; i++) {
            DWORD* rec = static_cast<DWORD*>(malloc(32));
            UTIL_ASSERT(rec != NULL);
            memset(rec, 0, 32);
            memcpy(rec, src, 32);
            arrTrans.Add(rec);
            src += 8;
        }
    }

    if (hdr.numStateTriggers != 0) {
        DWORD* src = reinterpret_cast<DWORD*>(static_cast<BYTE*>(pData) + hdr.stateTriggersOffset);
        for (DWORD i = 0; i < hdr.numStateTriggers; i++) {
            DWORD* rec = static_cast<DWORD*>(malloc(8));
            UTIL_ASSERT(rec != NULL);
            memset(rec, 0, 8);
            memcpy(rec, src, 8);
            arrSTrig.Add(rec);
            src += 2;
        }
    }

    if (hdr.numTransitionTriggers != 0) {
        DWORD* src = reinterpret_cast<DWORD*>(static_cast<BYTE*>(pData) + hdr.transitionTriggersOffset);
        for (DWORD i = 0; i < hdr.numTransitionTriggers; i++) {
            DWORD* rec = static_cast<DWORD*>(malloc(8));
            UTIL_ASSERT(rec != NULL);
            memset(rec, 0, 8);
            memcpy(rec, src, 8);
            arrTTrig.Add(rec);
            src += 2;
        }
    }

    if (hdr.numActions != 0) {
        DWORD* src = reinterpret_cast<DWORD*>(static_cast<BYTE*>(pData) + hdr.actionsOffset);
        for (DWORD i = 0; i < hdr.numActions; i++) {
            DWORD* rec = static_cast<DWORD*>(malloc(8));
            UTIL_ASSERT(rec != NULL);
            memset(rec, 0, 8);
            memcpy(rec, src, 8);
            arrActions.Add(rec);
            src += 2;
        }
    }

    m_characterIndex = characterIndex;
    m_talkerIndex = talkerIndex;

    // Resolve the talker's portrait color + display name (best-effort: skipped
    // if the sprite isn't currently in the object array).
    CGameSprite* pSprite = NULL;
    BYTE rc;
    do {
        do {
            rc = g_pBaldurChitin->GetObjectGame()->m_cObjectArray.GetShare(characterIndex,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
        } while (rc == CGameObjectArray::TIMEOUT);
    } while (rc == CGameObjectArray::DELETED);
    if (rc == CGameObjectArray::SUCCESS) {
        m_playerColor = CVidPalette::RANGE_COLORS[pSprite->GetBaseStats()->m_colors[CVIDPALETTE_RANGE_MAIN_CLOTH]];
        m_playerName = pSprite->GetName();
        g_pBaldurChitin->GetObjectGame()->m_cObjectArray.ReleaseShare(characterIndex,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }

    // One CAIScriptFile drives all three parses; it gets reset in each
    // Parse{Conditional,Response}String call via Clear().
    CAIScriptFile aScript;

    for (DWORD stateIdx = 0; stateIdx < hdr.numStates; stateIdx++) {
        DWORD* state = static_cast<DWORD*>(arrStates[stateIdx]);

        CGameDialogEntry* pEntry = new CGameDialogEntry();
        UTIL_ASSERT(pEntry != NULL);

        pEntry->m_dialogText = static_cast<STRREF>(state[0]);

        for (DWORD j = 0; j < state[2]; j++) {
            DWORD* trans = static_cast<DWORD*>(arrTrans[state[1] + j]);

            CGameDialogReply* pReply = new CGameDialogReply();
            UTIL_ASSERT(pReply != NULL);
            pReply->m_flags = trans[0];
            pReply->m_replyText = (trans[0] & 1) ? static_cast<STRREF>(trans[1]) : static_cast<STRREF>(-1);
            pReply->m_journalEntry = (trans[0] & 0x10) ? static_cast<STRREF>(trans[2]) : static_cast<STRREF>(-1);

            // Per-reply trigger (flag 0x2). trans[3] = transition trigger
            // index, or -1 (no record assigned) which the binary swaps for
            // "False()\n" so the reply Hold()s false rather than firing
            // unconditionally.
            if ((trans[0] & 0x2) != 0) {
                CString sCondText;
                if (trans[3] == 0xFFFFFFFF) {
                    sCondText = "False()\n";
                } else {
                    DWORD* ttrig = static_cast<DWORD*>(arrTTrig[trans[3]]);
                    sCondText = DLGNormalize(static_cast<const char*>(pData) + ttrig[0],
                        static_cast<int>(ttrig[1]));
                    if (sCondText.IsEmpty()) {
                        sCondText = "False()\n";
                    }
                }
                aScript.ParseConditionalString(sCondText);
                pReply->m_condition.Set(*aScript.m_curCondition);
            }

            // Per-reply action (flag 0x4). trans[4] = action index, or -1
            // which the binary maps to "\n" -- ParseResponseString swallows
            // the empty line and m_curResponse stays the empty default.
            if ((trans[0] & 0x4) != 0) {
                CString sActionText;
                if (trans[4] == 0xFFFFFFFF) {
                    sActionText = "\n";
                } else {
                    DWORD* aRec = static_cast<DWORD*>(arrActions[trans[4]]);
                    sActionText = DLGNormalize(static_cast<const char*>(pData) + aRec[0],
                        static_cast<int>(aRec[1]));
                    if (sActionText.IsEmpty()) {
                        sActionText = "\n";
                    }
                }
                aScript.ParseResponseString(sActionText);
                pReply->m_response.Set(*aScript.m_curResponse);
            }

            pReply->m_nextDialog = reinterpret_cast<BYTE*>(&trans[5]);
            pReply->m_nextEntryIndex = trans[7];
            pReply->m_displayPosition = NULL;
            pReply->m_removeIfPicked = FALSE;
            pReply->m_displayListId = 0xFF;

            pEntry->Add(pReply);
        }

        // state[3] = state trigger index (or -1 -- binary substitutes
        // "False()\n" same as the reply path, so entries with no trigger
        // record are inert until something explicitly enters them by index).
        // m_conditionPriority drives the m_dialogEntriesOrdered ordering:
        // lower values first; -1 (0xFFFFFFFF unsigned) sinks unconditional
        // entries to the back.
        CString sStateCondText;
        if (state[3] == 0xFFFFFFFF) {
            sStateCondText = "False()\n";
        } else {
            DWORD* strig = static_cast<DWORD*>(arrSTrig[state[3]]);
            sStateCondText = DLGNormalize(static_cast<const char*>(pData) + strig[0],
                static_cast<int>(strig[1]));
            if (sStateCondText.IsEmpty()) {
                sStateCondText = "False()\n";
            }
        }
        aScript.ParseConditionalString(sStateCondText);
        pEntry->m_startCondition.Set(*aScript.m_curCondition);

        pEntry->m_conditionPriority = state[3];
        pEntry->m_dialogIndex = m_dialogEntries.GetCount();
        m_dialogEntries.Add(pEntry);

        BOOL bInserted = FALSE;
        for (INT k = 0; k < m_dialogEntriesOrdered.GetCount(); k++) {
            if (pEntry->m_conditionPriority < m_dialogEntriesOrdered.GetAt(k)->m_conditionPriority) {
                m_dialogEntriesOrdered.InsertAt(k, pEntry);
                bInserted = TRUE;
                break;
            }
        }
        if (!bInserted) {
            m_dialogEntriesOrdered.Add(pEntry);
        }
    }

    for (INT i = 0; i < arrStates.GetCount(); i++) {
        free(arrStates[i]);
    }
    for (INT i = 0; i < arrTrans.GetCount(); i++) {
        free(arrTrans[i]);
    }
    for (INT i = 0; i < arrSTrig.GetCount(); i++) {
        free(arrSTrig[i]);
    }
    for (INT i = 0; i < arrTTrig.GetCount(); i++) {
        free(arrTTrig[i]);
    }
    for (INT i = 0; i < arrActions.GetCount(); i++) {
        free(arrActions[i]);
    }
}

// 0x483B70
BOOL CGameDialogSprite::FetchRumor(const CResRef& file, CGameSprite* pSprite, LONG& nIndex, STR_RES& strRes)
{
    Initialize(file, pSprite->GetId(), pSprite->GetId());

    CPtrArray validEntries;
    for (INT i = 0; i < m_dialogEntriesOrdered.GetCount(); i++) {
        CGameDialogEntry* pEntry = m_dialogEntriesOrdered.GetAt(i);
        if (pEntry != NULL) {
            CTypedPtrList<CPtrList, CAITrigger*> triggerList(10);
            if (pEntry->m_startCondition.Hold(triggerList, pSprite)) {
                validEntries.Add(pEntry);
            }
        }
    }

    if (validEntries.GetCount() < 1) {
        return FALSE;
    }

    if (nIndex < 0) {
        nIndex = rand() % validEntries.GetCount();
    }
    if (validEntries.GetCount() <= nIndex) {
        nIndex %= validEntries.GetCount();
    }

    CGameDialogEntry* pEntry = static_cast<CGameDialogEntry*>(validEntries[nIndex]);
    g_pBaldurChitin->GetTlkTable().Fetch(pEntry->m_dialogText, strRes);

    if (pEntry->GetCount() > 0) {
        CGameDialogReply* pReply = pEntry->GetAt(0);
        if ((pReply->m_flags & 0x10) != 0) {
            g_pBaldurChitin->GetObjectGame()->m_cJournal.AddEntry(pReply->m_journalEntry, 0);
        }
    }

    nIndex++;
    return TRUE;
}

// 0x483CF0
void CGameDialogSprite::EndDialog()
{
    if (m_bMusicThreadPriorityChanged == TRUE) {
        SetThreadPriority(g_pChitin->m_hMusicThread, m_nMusicThreadPriority);
    }

    g_pBaldurChitin->m_pEngineWorld->EnableKeyRepeat();

    m_waitingForResponse = FALSE;
    m_responseMarker = -1;
    m_bDialogActive = 0;
    m_sScrollMarker = "";

    CResRef cResRef("SilentDH");

    CSound cSound;
    cSound.SetResRef(cResRef, TRUE, TRUE);
    if (cSound.m_nLooping == 0) {
        cSound.SetFireForget(TRUE);
    }
    cSound.SetChannel(6, reinterpret_cast<DWORD>(g_pBaldurChitin->GetObjectGame()->GetVisibleArea()));
    cSound.Play(FALSE);
}

// 0x483EB0
BOOL CGameDialogSprite::EnterDialog(DWORD index, CGameSprite* pSprite, int a3)
{
    CGameDialogEntry* pEntry = m_dialogEntries[index];
    if (pEntry == NULL) {
        m_currentEntryIndex = 0;
        return FALSE;
    }

    m_currentEntryIndex = index;
    m_waitingForResponse = TRUE;
    m_responseMarker = -1;
    pEntry->Handle(pSprite, m_playerColor, a3);
    return TRUE;
}

// 0x485750
CGameDialogContinuation* CGameDialogReply::Apply(CGameSprite* pSprite)
{
    if ((m_flags & 0x4) != 0) {
        if (m_response.m_actionList.GetCount() != 0) {
            // The reply carries an AI response; queue it on the talker so the
            // actions execute next AI tick, then pause the actor's command
            // queue while the dialog window stays open.
            CMessageInsertResponse* pInsertMsg = new CMessageInsertResponse(
                m_response,
                /*checkCurrentResponse*/ 0,
                /*clearActions*/ 1,
                /*field_38*/ 1,
                pSprite->GetId(),
                pSprite->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pInsertMsg, FALSE);

            CMessageSetCommandPause* pPauseMsg = new CMessageSetCommandPause(
                75,
                pSprite->GetId(),
                pSprite->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pPauseMsg, FALSE);
        }

        // CMessageSetDialogWait fires whether or not there were responses --
        // it nulls out the dialog-wait timer on the actor.
        CMessageSetDialogWait* pWaitMsg = new CMessageSetDialogWait(
            0,
            CGameObjectArray::INVALID_INDEX,
            pSprite->GetId(),
            pSprite->GetId());
        g_pBaldurChitin->GetMessageHandler()->AddMessage(pWaitMsg, FALSE);
    }

    if ((m_flags & 0x10) != 0) {
        if (g_pBaldurChitin->GetObjectGame()->m_cJournal.AddEntry(m_journalEntry, 0)) {
            // TODO: binary at 0x4858a0..0x485900 fetches STRREF 0x2c5f
            // ("Journal entry added") and plays it as a notification sound
            // via a stack-local CSound. Skipped: minor UI feature.
        }
    }

    if (m_displayPosition != NULL) {
        g_pBaldurChitin->m_pEngineWorld->SetItemMarker(m_displayPosition, -1);
    }

    if ((m_flags & 0x8) != 0) {
        // Reply ends the conversation; no next-state info to return.
        return NULL;
    }

    CGameDialogContinuation* pCont = new CGameDialogContinuation;
    pCont->m_nextDialog = m_nextDialog;
    pCont->m_nextEntryIndex = m_nextEntryIndex;
    return pCont;
}

// 0x4824F0
void CGameDialogSprite::SwitchTalker(const CResRef& nextDialog, CGameSprite* pCurrentTalker)
{
    LONG newTalkerIndex;

    // Case 1: next dialog matches the file we already have loaded -- keep
    // the current talker. Binary 0x482517: CResRef::Compare(this->m_file, &nextDialog).
    if (m_file == nextDialog) {
        newTalkerIndex = m_talkerIndex;
    } else if (pCurrentTalker->m_dialog == nextDialog
        || pCurrentTalker->field_56E4 == nextDialog) {
        // Case 2: current talker already owns the next dialog (either in
        // m_dialog or the alternate slot field_56E4); reuse them so the
        // file load happens but the talker doesn't change. Binary
        // 0x482528..0x482556.
        newTalkerIndex = pCurrentTalker->GetId();
    } else {
        // Case 3 (binary 0x482558..0x4825a4): walk the talker's m_area
        // looking for a sprite with matching m_dialog/field_56E4 via
        // FUN_0046c460. Returns -1 if not found, in which case the binary
        // spawns "Mo the understudy" off DIALOGMO.CRE (binary 0x4825aa
        // ..0x4829bd). Both paths skipped: we keep the current talker, so
        // branching from one creature's DLG to another's stays on the
        // wrong sprite. Rare in IWD2 dialog trees (most branches stay
        // within the same DLG) but breaks the few that don't.
        newTalkerIndex = m_talkerIndex;
    }

    m_talkerIndex = newTalkerIndex;
    g_pBaldurChitin->GetObjectGame()->SetProtagonist(m_characterIndex);

    // Binary 0x482a98..0x482ad0 also queues a CMessageSetProtagonist on the
    // protagonist when the network session is active so the other clients'
    // copies of CInfGame stay in sync. SP-only; deferred with MP recovery.
}

// 0x483F00
void CGameDialogSprite::AsynchronousUpdate()
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    DWORD currentMode = pGame->GetGameSave()->m_mode;

    if (!m_waitingForResponse) {
        // No reply pending. Tick the freeze counter and -- once it expires
        // -- post EndDialog. The counter prevents instantaneously ending the
        // dialog when the entry text/replies are still being processed.
        if (currentMode != 0x502) {
            return;
        }

        // CChitin field at offset 0x1032 is the game-pause flag. When the
        // game is unpaused we tick once per AU; when paused we slow down to
        // one tick per 10 AU bumps so dialog doesn't race.
        BYTE bPaused = *(reinterpret_cast<const BYTE*>(g_pChitin) + 0x1032);

        if (bPaused == 0) {
            LONG counterBefore = m_dialogFreezeCounter--;
            if (counterBefore > 0) {
                return;
            }
            m_dialogFreezeCounter = 6;
        } else {
            if (currentMode != 0x502) {
                return;
            }
            LONG multiBefore = m_dialogFreezeMultiplayer++;
            if (multiBefore < 10) {
                return;
            }
            m_dialogFreezeMultiplayer = 0;
            LONG counterBefore = m_dialogFreezeCounter--;
            if (counterBefore > 0) {
                return;
            }
            // Binary at 0x484340..0x48448d logs "Sprite trying to execute
            // dialog without sprite" / "Unknown sprite trying to execute"
            // via the TRACE channel here, after a GetShare on m_talkerIndex.
            // Skipped: we just reset the counter and proceed to EndDialog.
            m_dialogFreezeCounter = 6;
        }

        g_pBaldurChitin->m_pEngineWorld->EndDialog(0, 1);
        return;
    }

    // Waiting on player input.
    //
    // TODO: scroll-to-marker (binary 0x483f5a..0x483ff7). When m_sScrollMarker is
    // non-empty the binary walks the active dialog display list looking for
    // a line whose text matches m_sScrollMarker and snaps the scroll to it -- used
    // for "you said earlier..." quoteback. We just leave m_sScrollMarker alone.

    m_dialogFreezeCounter = 6;

    CGameDialogEntry* pEntry = m_dialogEntries[m_currentEntryIndex];

    // End-dialog button visibility (binary 0x484025..0x4840a2). Panel 9 /
    // control 0 is the auto-continue button: activated when the entry has no
    // live replies (pEntry->m_bDisplayButton) AND the engine is still in
    // dialog mode. The binary skips redundant SetActive calls when the state
    // already matches; we do the same to keep InvalidateRect noise down.
    CUIPanel* pPanel = g_pBaldurChitin->m_pEngineWorld->GetManager()->GetPanel(9);
    CUIControlBase* pButton = pPanel->GetControl(0);
    BOOLEAN bShouldActivate = (pEntry->m_bDisplayButton && currentMode == 0x502)
        ? TRUE
        : FALSE;
    if (pButton->m_bActive != bShouldActivate) {
        pButton->SetActive(bShouldActivate);
        g_pBaldurChitin->m_pEngineWorld->GetManager()->GetPanel(9)->InvalidateRect(NULL);
    }
    g_pBaldurChitin->m_pEngineWorld->GetManager()->GetPanel(9)->InvalidateRect(NULL);

    // Acquire a deny lock on the talker so we can mutate pEntry->m_picked
    // and dispatch a state-change message under the same critical section.
    CGameSprite* pSprite = NULL;
    BYTE rc;
    LONG talkerIndex = m_talkerIndex;
    do {
        do {
            rc = g_pBaldurChitin->GetObjectGame()->m_cObjectArray.GetDeny(
                talkerIndex,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pSprite),
                INFINITE);
        } while (rc == CGameObjectArray::TIMEOUT);
    } while (rc == CGameObjectArray::DELETED);

    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }

    LONG marker = m_responseMarker;
    CGameDialogContinuation* pCont = NULL;

    if (marker == -1) {
        pEntry->m_picked = FALSE;
    } else {
        pEntry->m_picked = TRUE;
        if (marker != -2) {
            // Real reply pick (-2 is the "auto-continue button" marker that
            // skips Apply and goes straight to Path A). Apply fires the AI
            // response on the talker and returns the next-dialog info, or
            // NULL when the reply ends the conversation (flag 0x8).
            pCont = pEntry->GetAt(marker)->Apply(pSprite);
        }
    }

    if (pEntry->m_picked) {
        if (pCont == NULL) {
            // Path A (binary 0x484281..0x4842f0): reply has no continuation.
            // Post CMessageRemoveReplies so every reply's m_displayPosition
            // gets removed + the picked reply's text echoes back as the
            // player line, then end dialog.
            CMessageRemoveReplies* pMsg = new CMessageRemoveReplies(
                static_cast<LONG>(m_currentEntryIndex),
                m_responseMarker,
                m_playerColor,
                m_playerName,
                pSprite->GetId(),
                pSprite->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pMsg, FALSE);
            g_pBaldurChitin->m_pEngineWorld->EndDialog(FALSE, TRUE);
        } else {
            SwitchTalker(pCont->m_nextDialog, pSprite);

            CString sNextDialog;
            pCont->m_nextDialog.CopyToString(sNextDialog);

            CMessageContinueDialog* pMsg = new CMessageContinueDialog(
                static_cast<LONG>(m_currentEntryIndex),
                m_responseMarker,
                m_playerColor,
                m_playerName,
                pSprite->GetId(),
                sNextDialog,
                m_talkerIndex,
                m_characterIndex,
                static_cast<LONG>(pCont->m_nextEntryIndex),
                /*flag*/ 0,
                m_talkerIndex,
                m_talkerIndex);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pMsg, FALSE);

            m_responseMarker = -1;
            delete pCont;
        }
    }

    g_pBaldurChitin->GetObjectGame()->m_cObjectArray.ReleaseDeny(
        talkerIndex,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);
}

// 0x4845C0
void CGameDialogSprite::UpdateDialogColors()
{
    CScreenWorld* pWorld = g_pBaldurChitin->m_pEngineWorld;

    // __FILE__: C:\Projects\Icewind2\src\Baldur\CGameDialog.cpp
    // __LINE__: 1295
    UTIL_ASSERT(pWorld != NULL);

    if (m_waitingForResponse && pWorld->m_pActiveDialogDisplay != NULL) {
        COLORREF rgb = RGB(215, 215, 190);
        if (pWorld->m_bInControlOfDialog) {
            rgb = RGB(255, 46, 33);
        }

        CGameDialogEntry* pEntry = m_dialogEntries[m_currentEntryIndex];

        for (INT nIndex = 0; nIndex < pEntry->GetCount(); nIndex++) {
            CGameDialogReply* pReply = pEntry->GetAt(nIndex);
            if (pReply->m_displayPosition != NULL) {
                pWorld->m_pActiveDialogDisplay->SetItemTextColor(pReply->m_displayPosition, rgb);
            }
        }
    }
}

// 0x484680
CGameDialogEntry::~CGameDialogEntry()
{
    for (INT nIndex = 0; nIndex < GetCount(); nIndex++) {
        CGameDialogReply* pReply = GetAt(nIndex);
        if (pReply != NULL) {
            delete pReply;
        }
    }
    RemoveAll();
}

// 0x484730
void CGameDialogEntry::RemoveReplies(LONG lMarker, COLORREF rgbNameColor, const CString& sName)
{
    STR_RES strRes;
    BOOLEAN bRemoveIfPicked = FALSE;
    STRREF strReplyText;

    for (INT nIndex = 0; nIndex < GetCount(); nIndex++) {
        CGameDialogReply* pReply = GetAt(nIndex);
        if ((pReply->m_flags & 0x20) == 0) {
            if (pReply->m_displayPosition != NULL) {
                if (nIndex == lMarker) {
                    bRemoveIfPicked = pReply->m_removeIfPicked;
                    strReplyText = pReply->m_replyText;
                }

                g_pBaldurChitin->m_pEngineWorld->RemoveText(pReply->m_displayPosition);
                pReply->m_displayPosition = NULL;
            }
        }
    }

    if (!bRemoveIfPicked) {
        if (lMarker >= 0 && lMarker < GetCount()) {
            strReplyText = GetAt(lMarker)->m_replyText;
        }

        g_pBaldurChitin->GetTlkTable().Fetch(strReplyText, strRes);

        g_pBaldurChitin->m_pEngineWorld->DisplayText(sName,
            strRes.szText,
            rgbNameColor,
            RGB(160, 200, 215),
            -1,
            FALSE);

        g_pBaldurChitin->m_pEngineWorld->DisplayText(CString(""),
            CString(""),
            -1,
            FALSE);
    }
}

// 0x484900
void CGameDialogEntry::Handle(CGameSprite* pSprite, COLORREF playerColor, int a3)
{
    m_bDisplayButton = FALSE;

    STR_RES strRes;
    g_pBaldurChitin->GetTlkTable().Fetch(m_dialogText, strRes);

    COLORREF rgbSpeaker = CVidPalette::RANGE_COLORS[pSprite->GetBaseStats()->m_colors[CVIDPALETTE_RANGE_MAIN_CLOTH]];

    strRes.cSound.SetChannel(6,
        reinterpret_cast<DWORD>(g_pBaldurChitin->GetObjectGame()->GetVisibleArea()));
    if (strRes.cSound.GetRes() != NULL) {
        if (strRes.cSound.m_nLooping == 0) {
            strRes.cSound.SetFireForget(TRUE);
        }
        SleepEx(10, 0);
        strRes.cSound.Play(FALSE);
    }

    g_pBaldurChitin->m_pEngineWorld->DisplayText(CString(""),
        CString(""),
        rgbSpeaker,
        RGB(215, 215, 190),
        -1,
        FALSE);
    g_pBaldurChitin->m_pEngineWorld->DisplayText(pSprite->GetName(),
        strRes.szText,
        rgbSpeaker,
        RGB(215, 215, 190),
        -1,
        FALSE);
    g_pBaldurChitin->m_pEngineWorld->DisplayText(CString(""),
        CString(""),
        rgbSpeaker,
        RGB(215, 215, 190),
        -1,
        FALSE);

    // Area-switch + scroll-to-speaker (binary 0x484a50..0x484c50). If the
    // speaker isn't in the currently visible area, swap the visible area
    // before scrolling so the dialog renders against the right backdrop.
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pSprite->m_pArea != pGame->GetVisibleArea()) {
        CGameArea* pOld = pGame->GetVisibleArea();
        if (pOld != NULL) {
            // Binary 0x484ad9..0x484afd: clear the old area's per-pick state
            // before deactivating so stale picks don't render on top of the
            // dialog window.
            pOld->m_bPicked = FALSE;
            pOld->m_iPicked = -1;
            pOld->m_nToolTip = 0;
            pOld->OnDeactivation();
        }
        pGame->m_visibleArea = pSprite->m_pArea->m_id;
        pSprite->m_pArea->OnActivation();
    }

    if (pSprite->m_pArea == pGame->GetVisibleArea()) {
        CInfinity* pInfinity = pSprite->m_pArea->GetInfinity();
        CRect rViewPort(pInfinity->rViewPort);
        CPoint ptReference(0, 0);
        CRect rFx;
        if (pSprite->m_animation.m_animation != NULL) {
            pSprite->m_animation.CalculateFxRect(rFx, ptReference, pSprite->m_posZ);
        }

        CPoint ptScroll(pSprite->GetPos());
        ptScroll.x -= rViewPort.Width() / 2;
        ptScroll.y -= ptReference.y / 2;
        ptScroll.y -= rViewPort.Height() / 2;

        INT nCurrentX = 0;
        INT nCurrentY = 0;
        pInfinity->GetViewPosition(nCurrentX, nCurrentY);
        LONG dx = nCurrentX - ptScroll.x;
        LONG dy = nCurrentY - ptScroll.y;
        const LONG DIALOG_JUMP_CUT_OFF = 0x77A11;
        const SHORT DIALOG_SCROLL_SPEED = 0x10;
        g_pBaldurChitin->m_pEngineWorld->StartScroll(ptScroll,
            dx * dx + dy * dy < DIALOG_JUMP_CUT_OFF ? DIALOG_SCROLL_SPEED : 0);
    }

    // TODO: pause-mode SetMessageScreen overlay (binary 0x484c00..0x484c50).

    strRes.cSound.SetChannel(6,
        reinterpret_cast<DWORD>(g_pBaldurChitin->GetObjectGame()->GetVisibleArea()));
    if (strRes.cSound.GetRes() != NULL) {
        if (strRes.cSound.m_nLooping == 0) {
            strRes.cSound.SetFireForget(TRUE);
        }
        SleepEx(10, 0);
        strRes.cSound.Play(FALSE);
    }

    // Binary 0x485070: reply text color flips to red when player controls dialog.
    BOOLEAN bInControl = g_pBaldurChitin->m_pEngineWorld->m_bInControlOfDialog;
    COLORREF replyTextColor = bInControl ? RGB(255, 46, 33) : RGB(215, 215, 190);

    INT nValid = 0;

    // First pass: inline-text replies (flag 0x20). Binary 0x484f10..0x4850b0.
    // These render directly under the speaker text rather than as numbered
    // buttons, but still increment the shared counter so m_displayListId stays
    // aligned with the press-1..N keymap.
    for (INT i = 0; i < GetCount(); i++) {
        CGameDialogReply* pReply = GetAt(i);
        if (pReply == NULL || (pReply->m_flags & 0x20) == 0) {
            continue;
        }

        if ((pReply->m_flags & 0x2) != 0) {
            CTypedPtrList<CPtrList, CAITrigger*> triggerList(10);
            if (!pReply->m_condition.Hold(triggerList, pSprite)) {
                pReply->m_displayListId = 0xFF;
                continue;
            }
        }

        nValid++;

        CString sInline;
        STR_RES rrep;
        if ((pReply->m_flags & 0x1) != 0) {
            g_pBaldurChitin->GetTlkTable().Fetch(pReply->m_replyText, rrep);
            sInline = rrep.szText;
            pReply->m_removeIfPicked = FALSE;
            pReply->m_displayListId = static_cast<BYTE>(nValid);

            rrep.cSound.SetChannel(6,
                reinterpret_cast<DWORD>(g_pBaldurChitin->GetObjectGame()->GetVisibleArea()));
            if (rrep.cSound.GetRes() != NULL) {
                if (rrep.cSound.m_nLooping == 0) {
                    rrep.cSound.SetFireForget(TRUE);
                }
                SleepEx(10, 0);
                rrep.cSound.Play(FALSE);
            }
        }

        pReply->m_displayPosition = g_pBaldurChitin->m_pEngineWorld->DisplayText(
            CString(""),
            sInline,
            playerColor,
            replyTextColor,
            i,
            FALSE);
    }

    // Second pass: numbered reply buttons. Binary 0x4850b0..0x4855c0.
    for (INT i = 0; i < GetCount(); i++) {
        CGameDialogReply* pReply = GetAt(i);
        if (pReply == NULL) {
            continue;
        }

        if ((pReply->m_flags & 0x20) != 0) {
            continue;
        }

        if ((pReply->m_flags & 0x2) != 0) {
            CTypedPtrList<CPtrList, CAITrigger*> triggerList(10);
            if (!pReply->m_condition.Hold(triggerList, pSprite)) {
                pReply->m_displayListId = 0xFF;
                continue;
            }
        }

        nValid++;

        if ((pReply->m_flags & 0x1) != 0) {
            // Has reply text: fetch, format as "    N:", display.
            STR_RES rrep;
            g_pBaldurChitin->GetTlkTable().Fetch(pReply->m_replyText, rrep);

            CString sLine;
            sLine.Format("    %d:", nValid);

            pReply->m_removeIfPicked = FALSE;
            pReply->m_displayListId = static_cast<BYTE>(nValid);

            rrep.cSound.SetChannel(6,
                reinterpret_cast<DWORD>(g_pBaldurChitin->GetObjectGame()->GetVisibleArea()));
            if (rrep.cSound.GetRes() != NULL) {
                if (rrep.cSound.m_nLooping == 0) {
                    rrep.cSound.SetFireForget(TRUE);
                }
                SleepEx(10, 0);
                rrep.cSound.Play(FALSE);
            }

            pReply->m_displayPosition = g_pBaldurChitin->m_pEngineWorld->DisplayText(
                sLine,
                rrep.szText,
                playerColor,
                replyTextColor,
                i,
                FALSE);
        } else {
            // No reply text: show Continue/End Dialog button on panel 9.
            // Binary 0x485247..0x485452.
            m_bDisplayButton = TRUE;

            DWORD strref = (pReply->m_flags & 0x8) ? 9371 : 9372;
            STR_RES btnRes;
            g_pBaldurChitin->GetTlkTable().Fetch(strref, btnRes);

            pReply->m_removeIfPicked = TRUE;

            CScreenWorld* pWorld = g_pBaldurChitin->m_pEngineWorld;
            pWorld->m_dialogReplyIndex = static_cast<SHORT>(i);
            pWorld->m_dialogContinueFlag = 0;

            CUIPanel* pPanel9 = pWorld->GetManager()->GetPanel(9);
            if (pPanel9 != NULL) {
                CUIControlButton* pBtn = static_cast<CUIControlButton*>(pPanel9->GetControl(0));
                if (pBtn != NULL) {
                    pBtn->SetText(btnRes.szText);
                }
            }

            btnRes.cSound.SetChannel(6,
                reinterpret_cast<DWORD>(g_pBaldurChitin->GetObjectGame()->GetVisibleArea()));
            if (btnRes.cSound.GetRes() != NULL) {
                if (btnRes.cSound.m_nLooping == 0) {
                    btnRes.cSound.SetFireForget(TRUE);
                }
                SleepEx(10, 0);
                btnRes.cSound.Play(FALSE);
            }

            // Binary 0x4854e2: display empty text so the reply gets a
            // non-NULL m_displayPosition. RemoveReplies needs this to
            // find removeIfPicked and suppress the PC name echo.
            pReply->m_displayPosition = g_pBaldurChitin->m_pEngineWorld->DisplayText(
                CString(""),
                CString(""),
                playerColor,
                replyTextColor,
                i,
                FALSE);
        }
    }

    if (nValid == 0) {
        m_bDisplayButton = TRUE;
    }

    (void)a3;
}

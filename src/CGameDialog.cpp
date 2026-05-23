#include "CGameDialog.h"

#include "CAIResponse.h"
#include "CAIScriptFile.h"
#include "CBaldurChitin.h"
#include "CGameArea.h"
#include "CGameJournal.h"
#include "CGameSprite.h"
#include "CInfGame.h"
#include "CResDLG.h"
#include "CScreenWorld.h"
#include "CUIControlTextDisplay.h"
#include "CUtil.h"

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

    for (INT nIndex = 0; nIndex < m_dialogEntriesOrdered.GetCount(); nIndex++) {
        CGameDialogEntry* pEntry = m_dialogEntriesOrdered.GetAt(nIndex);
        if (pEntry != NULL
            && pEntry->m_startCondition.Hold(CTypedPtrList<CPtrList, CAITrigger*>(), pSprite)) {
            // FIXME: Unused.
            LONG nCharacterId = g_pBaldurChitin->GetObjectGame()->GetProtagonist();

            CMessage* pMessage = new CMessageEnterDialog(pEntry->m_dialogIndex,
                TRUE,
                pSprite->GetId(),
                pSprite->GetId());
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pMessage, FALSE);

            m_bDialogActive = 1;
            field_56 = "";

            return TRUE;
        }
    }

    return FALSE;
}

// 0x482210
void CGameDialogSprite::Initialize(CResRef file, LONG characterIndex, LONG talkerIndex)
{
    if (file == "") {
        ClearMarshal();
        return;
    }

    if (file == m_file) {
        m_characterIndex = characterIndex;
        g_pBaldurChitin->GetObjectGame()->SetProtagonist(characterIndex);
        // Multiplayer-host sync path queues a CMessageSetProtagonist
        // (binary 0x482275..0x4822c8). Skipped in SP; restore with MP recovery.
        m_talkerIndex = talkerIndex;
        return;
    }

    CResRef cNewFile;
    CResDLG* pRes = NULL;
    BOOL bRequested = FALSE;

    if (file != "") {
        pRes = static_cast<CResDLG*>(g_pChitin->cDimm.GetResObject(file, 0x3F3, TRUE));
        if (pRes == NULL) {
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
        ClearMarshal();
    } else {
        m_file = file;

        void* pData = NULL;
        DWORD nSize = 0;
        if (pRes != NULL) {
            if (pRes->Demand() != NULL) {
                nSize = pRes->m_nResSizeActual;
                static_cast<CRes*>(pRes)->Release();
            }
            pData = pRes->Demand();
        }

        LoadEntries(pData, nSize, characterIndex, talkerIndex);

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
        // Player portrait color table lives at .rdata 0x85e8d8; index =
        // sprite byte at offset 0x5CA (party-slot color id).
        m_playerColor = (reinterpret_cast<const COLORREF*>(0x85e8d8))[
            *(reinterpret_cast<const BYTE*>(pSprite) + 0x5ca)];
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
    field_56 = "";

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

// 0x483F00
void CGameDialogSprite::AsynchronousUpdate()
{
    // TODO: Incomplete.
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

    // Speaker portrait color (party-slot table at .rdata 0x85e8d8, indexed by
    // sprite byte 0x5ca).
    COLORREF rgbSpeaker = (reinterpret_cast<const COLORREF*>(0x85e8d8))[
        *(reinterpret_cast<const BYTE*>(pSprite) + 0x5ca)];

    g_pBaldurChitin->m_pEngineWorld->DisplayText(CString(""),
        strRes.szText,
        rgbSpeaker,
        RGB(190, 215, 215),
        -1,
        FALSE);
    g_pBaldurChitin->m_pEngineWorld->DisplayText(pSprite->GetName(),
        CString(""),
        -1,
        FALSE);

    // TODO: area-switch / scroll-to-speaker / pause-mode branching / dialog
    // sound playback (binary 0x484a50..0x484ed0) deferred for later commits.

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
        if ((pReply->m_flags & 0x1) != 0) {
            STR_RES rrep;
            g_pBaldurChitin->GetTlkTable().Fetch(pReply->m_replyText, rrep);
            sInline = rrep.szText;
            pReply->m_removeIfPicked = FALSE;
            pReply->m_displayListId = static_cast<BYTE>(nValid);
        }

        // TODO: per-reply sound (CSound::SetChannel/Play on rrep.cSound).
        // Color also flips to RGB(255,46,33) when pause-mode is active; both
        // deferred until task #4.
        pReply->m_displayPosition = g_pBaldurChitin->m_pEngineWorld->DisplayText(
            CString(""),
            sInline,
            playerColor,
            RGB(190, 215, 215),
            -1,
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

        // Flag 0x2 = reply has a trigger; gate via m_condition.Hold(). With
        // an empty m_triggerList (no condition string parsed yet) Hold
        // returns TRUE, so the reply still shows. Once LoadEntries wires up
        // CAIScriptFile::ParseConditionalString the gate becomes active.
        if ((pReply->m_flags & 0x2) != 0) {
            CTypedPtrList<CPtrList, CAITrigger*> triggerList(10);
            if (!pReply->m_condition.Hold(triggerList, pSprite)) {
                pReply->m_displayListId = 0xFF;
                continue;
            }
        }

        nValid++;

        STR_RES rrep;
        g_pBaldurChitin->GetTlkTable().Fetch(pReply->m_replyText, rrep);

        CString sLine;
        sLine.Format("%d. %s", nValid, static_cast<LPCTSTR>(rrep.szText));

        pReply->m_displayPosition = g_pBaldurChitin->m_pEngineWorld->DisplayText(
            sLine,
            CString(""),
            -1,
            FALSE);
        pReply->m_displayListId = static_cast<BYTE>(nValid);
    }

    if (nValid == 0) {
        m_bDisplayButton = TRUE;
        // Binary also pushes a "End dialog" auto-continue button here; deferred.
    }

    (void)playerColor;
    (void)a3;
}

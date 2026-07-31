// TEST SCAFFOLDING -- not a mirror of IWD2.exe. See AutoUI.h.
//
// Drives the UI the way the engine drives it. CChitin::AsynchronousUpdate polls
// the mouse with GetAsyncKeyState and then calls pActiveEngine->OnLButtonDown /
// OnLButtonUp with a screen point (CChitin.cpp:1544-1567) -- IWD2 never handles
// a WM_LBUTTONDOWN. So a synthetic click is just those same two virtual calls
// with a point we computed instead of one the OS gave us: real hit-testing,
// real capture handshake, real click sound.
//
// That single primitive covers menus, the action bar (which is panel 1,
// controls 6..17 of CScreenWorld's manager -- CInfButtonArray.cpp:598,627),
// portraits and ground clicks. Only the addressing differs: a control is
// resolved to its centre, a ground click is given in screen coordinates.
//
// Script format -- one command per line, '#' starts a comment:
//
//   waitgo [ticks]               block until IWD2_RE_UI_GO exists -- the observer
//                                handshake. vm.sh smoke only attaches the crash
//                                guard after the save reports loaded, so a
//                                scenario that clicks before that is invisible
//                                to --hit. Put this before the first action.
//   wait <screen> [ticks]        block until that screen is active
//   dump                         whole UI tree of the active screen
//   screen                       active screen identity only
//   click <panel> <control>      resolve the control centre, then click it
//   clickxy <x> <y>              click a raw screen point (ground, world)
//   key <vk>                     pActiveEngine->OnKeyDown
//   goto <screen>                CBaldurEngine::OnLeftPanelButtonClick
//   expect screen <screen>
//   expect controls <panel> <min>
//   expect control <panel> <control>
//   sleep <ticks>
//
// Results are one JSON object per line to IWD2_RE_UI_RESULT, last line the
// verdict. A failed assertion stops the script.

#include "AutoUI.h"

#include "mfc.h"

#include "BalDataTypes.h"
#include "CBaldurChitin.h"
#include "CBaldurEngine.h"
#include "CUIControlBase.h"
#include "CUIManager.h"
#include "CUIPanel.h"
#include "CWarp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Iwd2AutoUI {

namespace {

const int MAX_STEPS = 128;
const int MAX_TOKEN = 48;
const int MAX_STRS = 2;
const int MAX_NUMS = 3;
// Ticks to let the engine settle after a click before the next step runs.
const int CLICK_SETTLE_TICKS = 2;
const int DEFAULT_WAIT_TICKS = 3000;

struct Step {
    char op[MAX_TOKEN];
    char s[MAX_STRS][MAX_TOKEN];
    int n[MAX_NUMS];
    int nCount;
};

bool s_loaded = false;
bool s_finished = false;
Step s_steps[MAX_STEPS];
int s_count = 0;
int s_pc = 0;
int s_waited = 0;
int s_failed = 0;
int s_clickPhase = 0;
CPoint s_clickPt(0, 0);
FILE* s_out = NULL;

DWORD GetEnv(const char* name, char* value, DWORD size)
{
    DWORD result = GetEnvironmentVariableA(name, value, size);
    if (result >= size && size > 0) {
        value[size - 1] = '\0';
    }
    return result;
}

void Emit(const char* fmt, ...)
{
    if (s_out == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(s_out, fmt, args);
    va_end(args);
    fputc('\n', s_out);
    // Flushed per line: if the game faults mid-scenario the lines already
    // written are the diagnosis.
    fflush(s_out);
}

int Num(const Step& step, int nIndex, int nDefault)
{
    return nIndex < step.nCount ? step.n[nIndex] : nDefault;
}

// Identity of the active screen, by pointer comparison against the slots
// CBaldurChitin holds (CBaldurChitin.h:163-185). Pointer identity beats any
// heuristic: there is exactly one instance of each screen.
const char* ScreenName(const CWarp* pEngine)
{
    if (pEngine == NULL || g_pBaldurChitin == NULL) {
        return "none";
    }

    const void* p = pEngine;
    if (p == g_pBaldurChitin->m_pEngineWorld) return "world";
    if (p == g_pBaldurChitin->m_pEngineSave) return "save";
    if (p == g_pBaldurChitin->m_pEngineLoad) return "load";
    if (p == g_pBaldurChitin->m_pEngineOptions) return "options";
    if (p == g_pBaldurChitin->m_pEngineConnection) return "connection";
    if (p == g_pBaldurChitin->m_pEngineSinglePlayer) return "singleplayer";
    if (p == g_pBaldurChitin->m_pEngineMultiPlayer) return "multiplayer";
    if (p == g_pBaldurChitin->m_pEngineChapter) return "chapter";
    if (p == g_pBaldurChitin->m_pEngineCharacter) return "character";
    if (p == g_pBaldurChitin->m_pEngineCreateChar) return "createchar";
    if (p == g_pBaldurChitin->m_pEngineInventory) return "inventory";
    if (p == g_pBaldurChitin->m_pEngineJournal) return "journal";
    if (p == g_pBaldurChitin->m_pEngineMap) return "map";
    if (p == g_pBaldurChitin->m_pEngineWorldMap) return "worldmap";
    if (p == g_pBaldurChitin->m_pEngineSpellbook) return "spellbook";
    if (p == g_pBaldurChitin->m_pEngineStore) return "store";
    if (p == g_pBaldurChitin->m_pEngineStart) return "start";
    if (p == g_pBaldurChitin->m_pEngineMovies) return "movies";
    if (p == g_pBaldurChitin->m_pEngineKeymaps) return "keymaps";
    if (p == g_pBaldurChitin->m_pEngineProjector) return "projector";
    if (p == g_pBaldurChitin->m_pEngineDM) return "dm";
    return "other";
}

// Every screen that owns a UI is a CBaldurEngine; GetManager returns
// this + 0x30 (CBaldurEngine.cpp:468).
CUIManager* ManagerOf(CWarp* pEngine)
{
    if (pEngine == NULL) {
        return NULL;
    }

    return static_cast<CBaldurEngine*>(pEngine)->GetManager();
}

CUIPanel* FindPanel(CWarp* pEngine, DWORD nPanelId)
{
    CUIManager* pManager = ManagerOf(pEngine);
    return pManager != NULL ? pManager->GetPanel(nPanelId) : NULL;
}

int CountControls(CUIPanel* pPanel)
{
    if (pPanel == NULL) {
        return -1;
    }

    int nControls = 0;
    POSITION pos = pPanel->m_lControls.GetHeadPosition();
    while (pos != NULL) {
        pPanel->m_lControls.GetNext(pos);
        nControls++;
    }
    return nControls;
}

// Dump the live tree. This is the point of the module: it answers "does this
// screen actually have controls, and are they active" without a human reading
// pixels.
void DumpTree(CWarp* pEngine, int nStep)
{
    CUIManager* pManager = ManagerOf(pEngine);
    if (pManager == NULL) {
        Emit("{\"step\":%d,\"op\":\"dump\",\"status\":\"error\",\"detail\":\"no manager\"}",
            nStep);
        return;
    }

    char szResRef[16];
    memset(szResRef, 0, sizeof(szResRef));
    pManager->m_cResRef.CopyToString(szResRef);

    Emit("{\"step\":%d,\"op\":\"dump\",\"screen\":\"%s\",\"chu\":\"%s\","
         "\"initialized\":%d,\"hidden\":%d}",
        nStep, ScreenName(pEngine), szResRef,
        pManager->m_bInitialized != 0 ? 1 : 0,
        pManager->m_bHidden != 0 ? 1 : 0);

    POSITION panelPos = pManager->m_lPanels.GetHeadPosition();
    while (panelPos != NULL) {
        CUIPanel* pPanel = pManager->m_lPanels.GetNext(panelPos);
        if (pPanel == NULL) {
            continue;
        }

        Emit("{\"step\":%d,\"op\":\"panel\",\"id\":%lu,\"x\":%ld,\"y\":%ld,"
             "\"cx\":%ld,\"cy\":%ld,\"active\":%d,\"enabled\":%d,\"hidden\":%d,"
             "\"controls\":%d}",
            nStep, pPanel->m_nID,
            pPanel->m_ptOrigin.x, pPanel->m_ptOrigin.y,
            pPanel->m_size.cx, pPanel->m_size.cy,
            pPanel->m_bActive != 0 ? 1 : 0,
            pPanel->m_bEnabled != 0 ? 1 : 0,
            pPanel->m_bHidden != 0 ? 1 : 0,
            CountControls(pPanel));

        POSITION ctrlPos = pPanel->m_lControls.GetHeadPosition();
        while (ctrlPos != NULL) {
            CUIControlBase* pControl = pPanel->m_lControls.GetNext(ctrlPos);
            if (pControl == NULL) {
                continue;
            }

            Emit("{\"step\":%d,\"op\":\"control\",\"panel\":%lu,\"id\":%lu,"
                 "\"x\":%ld,\"y\":%ld,\"cx\":%ld,\"cy\":%ld,"
                 "\"type\":%d,\"active\":%d,\"enabled\":%d}",
                nStep, pPanel->m_nID, pControl->m_nID,
                pControl->m_ptOrigin.x, pControl->m_ptOrigin.y,
                pControl->m_size.cx, pControl->m_size.cy,
                pControl->m_nControlType,
                pControl->m_bActive != 0 ? 1 : 0,
                pControl->m_bEnabled != 0 ? 1 : 0);
        }
    }
}

void Fail(int nStep, const char* op, const char* detail)
{
    Emit("{\"step\":%d,\"op\":\"%s\",\"status\":\"fail\",\"detail\":\"%s\"}",
        nStep, op, detail);
    s_failed++;
    s_pc = s_count;
}

// One line -> one Step. Tokens after the op are sorted into up to two string
// slots and up to three integer slots, so every command form below parses with
// the same code: `wait world 3000`, `click 0 3`, `expect controls 0 5`.
void ParseLine(char* p)
{
    Step& step = s_steps[s_count];
    memset(&step, 0, sizeof(step));

    int nStrs = 0;
    char* token = strtok(p, " \t\r\n");
    if (token == NULL) {
        return;
    }

    strncpy(step.op, token, MAX_TOKEN - 1);

    while ((token = strtok(NULL, " \t\r\n")) != NULL) {
        if ((token[0] >= '0' && token[0] <= '9') || token[0] == '-') {
            if (step.nCount < MAX_NUMS) {
                step.n[step.nCount++] = atoi(token);
            }
        } else if (nStrs < MAX_STRS) {
            strncpy(step.s[nStrs++], token, MAX_TOKEN - 1);
        }
    }

    s_count++;
}

void LoadScript()
{
    s_loaded = true;

    char szPath[MAX_PATH];
    if (GetEnv("IWD2_RE_UI_SCRIPT", szPath, sizeof(szPath)) == 0) {
        return;
    }

    char szResult[MAX_PATH];
    if (GetEnv("IWD2_RE_UI_RESULT", szResult, sizeof(szResult)) > 0) {
        s_out = fopen(szResult, "wb");
    }

    FILE* fp = fopen(szPath, "rb");
    if (fp == NULL) {
        Emit("{\"op\":\"load\",\"status\":\"error\",\"detail\":\"cannot open script\"}");
        return;
    }

    char szLine[256];
    while (s_count < MAX_STEPS && fgets(szLine, sizeof(szLine), fp) != NULL) {
        char* hash = strchr(szLine, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        ParseLine(szLine);
    }

    fclose(fp);
    Emit("{\"op\":\"load\",\"status\":\"ok\",\"steps\":%d}", s_count);
}

} // namespace

bool IsEnabled()
{
    char szPath[MAX_PATH];
    return GetEnv("IWD2_RE_UI_SCRIPT", szPath, sizeof(szPath)) > 0;
}

void Tick(CWarp* pActiveEngine)
{
    if (!s_loaded) {
        LoadScript();
    }

    if (s_finished || pActiveEngine == NULL) {
        return;
    }

    if (s_pc >= s_count) {
        s_finished = true;
        Emit("{\"op\":\"verdict\",\"verdict\":\"%s\",\"steps\":%d,\"failed\":%d}",
            s_failed != 0 ? "FAIL" : "PASS", s_count, s_failed);
        if (s_out != NULL) {
            fclose(s_out);
            s_out = NULL;
        }
        return;
    }

    // A click spans ticks: down, then up, then settle -- as the engine's own
    // edge detection would deliver it.
    if (s_clickPhase != 0) {
        if (s_clickPhase == 1) {
            pActiveEngine->OnLButtonUp(s_clickPt);
            s_clickPhase = 2;
            return;
        }
        if (s_clickPhase < CLICK_SETTLE_TICKS + 2) {
            s_clickPhase++;
            return;
        }
        s_clickPhase = 0;
        s_pc++;
        return;
    }

    const Step& step = s_steps[s_pc];
    const int nStep = s_pc + 1;

    if (strcmp(step.op, "dump") == 0) {
        DumpTree(pActiveEngine, nStep);
        s_pc++;
        return;
    }

    if (strcmp(step.op, "screen") == 0) {
        Emit("{\"step\":%d,\"op\":\"screen\",\"screen\":\"%s\"}",
            nStep, ScreenName(pActiveEngine));
        s_pc++;
        return;
    }

    if (strcmp(step.op, "waitgo") == 0) {
        char szGo[MAX_PATH];
        if (GetEnv("IWD2_RE_UI_GO", szGo, sizeof(szGo)) == 0) {
            Emit("{\"step\":%d,\"op\":\"waitgo\",\"status\":\"ok\",\"detail\":\"no gate set\"}",
                nStep);
            s_pc++;
            return;
        }
        FILE* fp = fopen(szGo, "rb");
        if (fp != NULL) {
            fclose(fp);
            Emit("{\"step\":%d,\"op\":\"waitgo\",\"status\":\"ok\",\"ticks\":%d}",
                nStep, s_waited);
            s_waited = 0;
            s_pc++;
            return;
        }
        if (s_waited++ > Num(step, 0, DEFAULT_WAIT_TICKS)) {
            Emit("{\"step\":%d,\"op\":\"waitgo\",\"status\":\"fail\","
                 "\"detail\":\"observer never signalled\"}", nStep);
            s_failed++;
            s_waited = 0;
            s_pc = s_count;
        }
        return;
    }

    if (strcmp(step.op, "sleep") == 0) {
        if (s_waited++ < Num(step, 0, 1)) {
            return;
        }
        s_waited = 0;
        s_pc++;
        return;
    }

    if (strcmp(step.op, "wait") == 0) {
        const char* szNow = ScreenName(pActiveEngine);
        if (strcmp(szNow, step.s[0]) == 0) {
            Emit("{\"step\":%d,\"op\":\"wait\",\"screen\":\"%s\",\"status\":\"ok\","
                 "\"ticks\":%d}", nStep, szNow, s_waited);
            s_waited = 0;
            s_pc++;
            return;
        }
        if (s_waited++ > Num(step, 0, DEFAULT_WAIT_TICKS)) {
            Emit("{\"step\":%d,\"op\":\"wait\",\"want\":\"%s\",\"got\":\"%s\","
                 "\"status\":\"fail\",\"detail\":\"timeout\"}",
                nStep, step.s[0], szNow);
            s_failed++;
            s_waited = 0;
            s_pc = s_count;
        }
        return;
    }

    if (strcmp(step.op, "goto") == 0) {
        // OnLeftPanelButtonClick is the engine's own screen switcher
        // (CBaldurEngine.cpp:768-833).
        DWORD dwButton = static_cast<DWORD>(Num(step, 0, 0));
        if (strcmp(step.s[0], "world") == 0) dwButton = 0;
        else if (strcmp(step.s[0], "spellbook") == 0) dwButton = 4;
        else if (strcmp(step.s[0], "inventory") == 0) dwButton = 5;
        else if (strcmp(step.s[0], "journal") == 0) dwButton = 6;
        else if (strcmp(step.s[0], "map") == 0) dwButton = 7;
        else if (strcmp(step.s[0], "character") == 0) dwButton = 8;
        else if (strcmp(step.s[0], "options") == 0) dwButton = 9;

        static_cast<CBaldurEngine*>(pActiveEngine)->OnLeftPanelButtonClick(dwButton);
        Emit("{\"step\":%d,\"op\":\"goto\",\"target\":\"%s\",\"button\":%lu,"
             "\"status\":\"ok\"}", nStep, step.s[0], dwButton);
        s_pc++;
        return;
    }

    if (strcmp(step.op, "key") == 0) {
        pActiveEngine->OnKeyDown(static_cast<SHORT>(Num(step, 0, 0)));
        Emit("{\"step\":%d,\"op\":\"key\",\"vk\":%d,\"status\":\"ok\"}",
            nStep, Num(step, 0, 0));
        s_pc++;
        return;
    }

    if (strcmp(step.op, "clickxy") == 0) {
        s_clickPt = CPoint(Num(step, 0, 0), Num(step, 1, 0));
        pActiveEngine->OnLButtonDown(s_clickPt);
        Emit("{\"step\":%d,\"op\":\"clickxy\",\"x\":%ld,\"y\":%ld,\"status\":\"ok\"}",
            nStep, s_clickPt.x, s_clickPt.y);
        s_clickPhase = 1;
        return;
    }

    if (strcmp(step.op, "click") == 0) {
        CUIPanel* pPanel = FindPanel(pActiveEngine, static_cast<DWORD>(Num(step, 0, 0)));
        if (pPanel == NULL) {
            Fail(nStep, "click", "panel not found");
            return;
        }

        CUIControlBase* pControl = pPanel->GetControl(static_cast<DWORD>(Num(step, 1, 0)));
        if (pControl == NULL) {
            Fail(nStep, "click", "control not found");
            return;
        }

        // Control origin is panel-relative -- CUIPanel::OnLButtonDown subtracts
        // the panel origin before hit-testing (CUIPanel.cpp:221) -- so add it
        // back to get the screen point the engine expects.
        s_clickPt = CPoint(
            pPanel->m_ptOrigin.x + pControl->m_ptOrigin.x + pControl->m_size.cx / 2,
            pPanel->m_ptOrigin.y + pControl->m_ptOrigin.y + pControl->m_size.cy / 2);

        pActiveEngine->OnLButtonDown(s_clickPt);
        Emit("{\"step\":%d,\"op\":\"click\",\"panel\":%d,\"control\":%d,"
             "\"x\":%ld,\"y\":%ld,\"active\":%d,\"status\":\"ok\"}",
            nStep, Num(step, 0, 0), Num(step, 1, 0),
            s_clickPt.x, s_clickPt.y, pControl->m_bActive != 0 ? 1 : 0);
        s_clickPhase = 1;
        return;
    }

    if (strcmp(step.op, "expect") == 0) {
        if (strcmp(step.s[0], "screen") == 0) {
            const char* szNow = ScreenName(pActiveEngine);
            if (strcmp(szNow, step.s[1]) != 0) {
                Emit("{\"step\":%d,\"op\":\"expect\",\"kind\":\"screen\",\"want\":\"%s\","
                     "\"got\":\"%s\",\"status\":\"fail\"}", nStep, step.s[1], szNow);
                s_failed++;
                s_pc = s_count;
                return;
            }
            Emit("{\"step\":%d,\"op\":\"expect\",\"kind\":\"screen\",\"got\":\"%s\","
                 "\"status\":\"ok\"}", nStep, szNow);
            s_pc++;
            return;
        }

        if (strcmp(step.s[0], "controls") == 0) {
            CUIPanel* pPanel = FindPanel(pActiveEngine, static_cast<DWORD>(Num(step, 0, 0)));
            int nControls = CountControls(pPanel);
            if (nControls < 0) {
                Fail(nStep, "expect", "panel not found");
                return;
            }
            const int nMin = Num(step, 1, 1);
            if (nControls < nMin) {
                Emit("{\"step\":%d,\"op\":\"expect\",\"kind\":\"controls\",\"panel\":%d,"
                     "\"min\":%d,\"got\":%d,\"status\":\"fail\"}",
                    nStep, Num(step, 0, 0), nMin, nControls);
                s_failed++;
                s_pc = s_count;
                return;
            }
            Emit("{\"step\":%d,\"op\":\"expect\",\"kind\":\"controls\",\"panel\":%d,"
                 "\"min\":%d,\"got\":%d,\"status\":\"ok\"}",
                nStep, Num(step, 0, 0), nMin, nControls);
            s_pc++;
            return;
        }

        if (strcmp(step.s[0], "control") == 0) {
            CUIPanel* pPanel = FindPanel(pActiveEngine, static_cast<DWORD>(Num(step, 0, 0)));
            if (pPanel == NULL) {
                Fail(nStep, "expect", "panel not found");
                return;
            }
            CUIControlBase* pControl = pPanel->GetControl(static_cast<DWORD>(Num(step, 1, 0)));
            if (pControl == NULL) {
                Emit("{\"step\":%d,\"op\":\"expect\",\"kind\":\"control\",\"panel\":%d,"
                     "\"control\":%d,\"status\":\"fail\",\"detail\":\"missing\"}",
                    nStep, Num(step, 0, 0), Num(step, 1, 0));
                s_failed++;
                s_pc = s_count;
                return;
            }
            Emit("{\"step\":%d,\"op\":\"expect\",\"kind\":\"control\",\"panel\":%d,"
                 "\"control\":%d,\"active\":%d,\"status\":\"ok\"}",
                nStep, Num(step, 0, 0), Num(step, 1, 0),
                pControl->m_bActive != 0 ? 1 : 0);
            s_pc++;
            return;
        }

        Fail(nStep, "expect", "unknown assertion");
        return;
    }

    Fail(nStep, step.op, "unknown op");
}

}

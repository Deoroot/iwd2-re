#include "CTimerWorld.h"

#include "CBaldurChitin.h"
#include "CGameArea.h"
#include "CInfGame.h"
#include "CScreenWorld.h"

// 0x8E078C
const CString CTimerWorld::TOKEN_MINUTE("MINUTE");

// 0x8E0774
const CString CTimerWorld::TOKEN_HOUR("HOUR");

// 0x8E0764
const CString CTimerWorld::TOKEN_DAY("DAY");

// 0x8E0754
const CString CTimerWorld::TOKEN_MONTH("MONTH");

// 0x8E0750
const CString CTimerWorld::TOKEN_YEAR("YEAR");

// 0x8E0788
const CString CTimerWorld::TOKEN_MONTHNAME("MONTHNAME");

// 0x8E034C
const CString CTimerWorld::TOKEN_DAYANDMONTH("DAYANDMONTH");

// 0x8E0770
const CString CTimerWorld::TOKEN_GAMEDAY("GAMEDAY");

// 0x8E076C
const CString CTimerWorld::TOKEN_GAMEDAYS("GAMEDAYS");

// 0x8E0784
const CString CTimerWorld::TOKEN_DURATION("DURATION");

// 0x8E0768
const CString CTimerWorld::TOKEN_DURATIONNOAND("DURATIONNOAND");

// 0x84EC0C
const BYTE CTimerWorld::TIMESCALE_MSEC_PER_SEC = 15;

// 0x84EC0D
const BYTE CTimerWorld::TIMESCALE_SEC_PER_MIN = 60;

// 0x84EC0E
const BYTE CTimerWorld::TIMESCALE_MIN_PER_HOUR = 5;

// 0x84EC0F
const BYTE CTimerWorld::TIMESCALE_HOUR_PER_DAY = 24;

// 0x84EC10
const UINT CTimerWorld::TIMESCALE_MSEC_PER_DAY = 108000;

// 0x84EC14
const UINT CTimerWorld::TIMESCALE_MSEC_PER_HOUR = 4500;

// 0x84EC18
const BYTE CTimerWorld::TIME_DAWN_HOUR = 6;

// 0x84EC1C
const UINT CTimerWorld::TIME_DAWN = 27000;

// 0x84EC20
const BYTE CTimerWorld::TIME_DAY_HOUR = 7;

// 0x84EC24
const UINT CTimerWorld::TIME_DAY = 31500;

// 0x84EC28
const BYTE CTimerWorld::TIME_DUSK_HOUR = 21;

// 0x84EC2C
const UINT CTimerWorld::TIME_DUSK = 94500;

// 0x84EC30
const BYTE CTimerWorld::TIME_NIGHT_HOUR = 22;

// 0x84EC34
const UINT CTimerWorld::TIME_NIGHT = 99000;

// 0x84EC38
const UINT CTimerWorld::TIME_APPROACHING_DELTA = 450;

// 0x84EC3C
const UINT CTimerWorld::TIME_APPROACHING_DAWN = 26550;

// 0x84EC40
const UINT CTimerWorld::TIME_APPROACHING_DUSK = 94050;

// 0x84EC44
const BYTE CTimerWorld::PERCENTAGE_RESET = 255;

// 0x84EC45
const BYTE CTimerWorld::MULTIPLAYER_TIME_SYNCH_DELTA = 5;

// 0x84EC48
const UINT CTimerWorld::MULTIPLAYER_TIME_SYNCH_INTERVAL = 450;

// 0x54EDD0
CTimerWorld::CTimerWorld()
{
    m_active = FALSE;
    m_gameTime = 0;
    m_nLastPercentage = 0;
}

// 0x54EDE0
void CTimerWorld::AdvanceCurrentTime(ULONG gameTime)
{
    DWORD deltaTime = gameTime - m_gameTime % TIMESCALE_MSEC_PER_DAY;

    if (gameTime < m_gameTime % TIMESCALE_MSEC_PER_DAY) {
        deltaTime += TIMESCALE_MSEC_PER_DAY;
    }

    if (deltaTime > 900) {
        if (g_pChitin->cNetwork.GetSessionHosting() || g_pChitin->cNetwork.GetSessionOpen() != TRUE) {
            m_gameTime += deltaTime;

            if (g_pChitin->cNetwork.GetSessionOpen() == TRUE && g_pChitin->cNetwork.GetSessionHosting() == TRUE) {
                g_pBaldurChitin->m_cBaldurMessage.TimeSynchBroadcast(m_gameTime, true);
                CheckForTriggerEventPast();
                g_pBaldurChitin->m_pEngineWorld->CompressTime(gameTime);
            }
        } else {
            g_pBaldurChitin->m_cBaldurMessage.TimeChangeToServer(deltaTime);
        }
    }
}

// 0x54EEA0
void CTimerWorld::AddCurrentTime(ULONG gameTime)
{
    if (gameTime > 900) {
        if (g_pChitin->cNetwork.GetSessionHosting() || g_pChitin->cNetwork.GetSessionOpen() != TRUE) {
            m_gameTime += gameTime;

            if (g_pChitin->cNetwork.GetSessionOpen() == TRUE && g_pChitin->cNetwork.GetSessionHosting() == TRUE) {
                g_pBaldurChitin->m_cBaldurMessage.TimeSynchBroadcast(m_gameTime, true);
                CheckForTriggerEventPast();
                g_pBaldurChitin->m_pEngineWorld->CompressTime(gameTime);
            }
        } else {
            g_pBaldurChitin->m_cBaldurMessage.TimeChangeToServer(gameTime);
        }
    }
}

// 0x54EF30
void CTimerWorld::UpdateTime(BOOLEAN forceUpdate)
{
    if (m_active || forceUpdate) {
        if ((g_pBaldurChitin->nAUCounter & 1) != 1) {
            m_gameTime++;
            CheckForTriggerEventAbsolute();
        }
    }
}

// 0x54EF60
void CTimerWorld::CheckForTriggerEventAbsolute()
{
    ULONG time = m_gameTime % TIMESCALE_MSEC_PER_DAY;
    CGameArea* pActiveArea = g_pBaldurChitin->GetObjectGame()->GetVisibleArea();
    if (pActiveArea != NULL && pActiveArea->m_bAreaLoaded) {
        switch (time) {
        case TIME_DAY:
            pActiveArea->SetDay();
            m_nLastPercentage = -1;
            return;
        case TIME_NIGHT:
            pActiveArea->SetNight();
            m_nLastPercentage = -1;
            return;
        case TIME_APPROACHING_DAWN:
            pActiveArea->SetApproachingDawn();
            return;
        case TIME_APPROACHING_DUSK:
            pActiveArea->SetApproachingDusk();
            return;
        }

        if (time >= TIME_DUSK && time < TIME_NIGHT) {
            BYTE nNewPercentage = -1 - ((time + 16682716) << 8) / TIMESCALE_MSEC_PER_HOUR;
            if (nNewPercentage != m_nLastPercentage) {
                pActiveArea->SetDusk(nNewPercentage, TRUE);
                m_nLastPercentage = nNewPercentage;
            }
        } else if (time >= TIME_DAWN && time < TIME_DAY) {
            BYTE nNewPercentage = ((time + 16750216) << 8) / TIMESCALE_MSEC_PER_HOUR;
            if (nNewPercentage != m_nLastPercentage) {
                pActiveArea->SetDawn(nNewPercentage, TRUE);
                m_nLastPercentage = nNewPercentage;
            }
        }
    }
}

// 0x54F080
void CTimerWorld::CheckForTriggerEventPast()
{
    ULONG time = m_gameTime % TIMESCALE_MSEC_PER_DAY;
    CGameArea* pActiveArea = g_pBaldurChitin->GetObjectGame()->GetVisibleArea();
    if (pActiveArea != NULL && pActiveArea->m_bAreaLoaded) {
        if ((pActiveArea->GetInfinity()->m_areaType & 0x40) != 0) {
            pActiveArea->SetNight();
            m_nLastPercentage = -1;
        } else {
            if (time >= TIME_DAY && time < TIME_DUSK) {
                pActiveArea->SetDay();
                m_nLastPercentage = -1;

                if (time >= TIME_APPROACHING_DUSK) {
                    pActiveArea->SetApproachingDusk();
                }
            } else if (time >= TIME_NIGHT && time < TIME_DAWN) {
                pActiveArea->SetNight();
                m_nLastPercentage = -1;

                if (time >= TIME_APPROACHING_DAWN) {
                    pActiveArea->SetApproachingDawn();
                }
            } else if (time >= TIME_DAY) {
                // TODO: Figure out constants, math should be simpler.
                BYTE nNewPercentage = -1 - ((time + 16682716) << 8) / TIMESCALE_MSEC_PER_HOUR;
                if (nNewPercentage != m_nLastPercentage) {
                    pActiveArea->SetDusk(nNewPercentage, TRUE);
                    m_nLastPercentage = nNewPercentage;
                }
            } else {
                // TODO: Figure out constants, math should be simpler.
                BYTE nNewPercentage = -1 - ((time + 16750216) << 8) / TIMESCALE_MSEC_PER_HOUR;
                if (nNewPercentage != m_nLastPercentage) {
                    pActiveArea->SetDawn(nNewPercentage, TRUE);
                    m_nLastPercentage = nNewPercentage;
                }
            }
        }
    }
}

// 0x54F1D0
void CTimerWorld::GetCurrentTimeString(ULONG nFromTime, STRREF strTimeFormat, CString& sTime)
{
    // Based on Ghidra decomp 0x54F8F0
    // Constants from decomp: 0x1A5E0=108000 ticks/day, 0x1194=4500 ticks/hour, 0x900=900 ticks/game-minute

    const CRuleTables& rule = g_pBaldurChitin->GetObjectGame()->GetRuleTables();
    const C2DArray& tMonths = rule.m_tMonths;  // DAT_008de98c
    const C2DArray& tYears = rule.m_tYears;     // DAT_008df2c0

    // Count months/year and days/year from MONTHS.2DA (comp: the while loop)
    DWORD dwMonthIdx = 0;
    DWORD dwDaysPerYear = 0;
    DWORD dwMonthsPerYear = 0;
    while (TRUE) {
        CString sKey;
        sKey.Format("%d", dwMonthIdx);  // FUN_007faee8 + DAT_008a64c4 ("%d")
        LONG nDays = atol(tMonths.GetAt(CString("DAYS"), sKey));  // FUN_007e7b03 + C2DArray__GetAt
        if (nDays == 0) break;
        dwDaysPerYear += nDays;
        dwMonthsPerYear++;
        dwMonthIdx++;
    }

    if (dwMonthsPerYear == 0 || dwDaysPerYear == 0) return;

    // Read YEARS.2DA values (comp: C2DArray__GetAt calls with DAT_008dee64/DAT_008df0f8/etc)
    LONG nStartTime = atol(tYears.GetAt(CString("VALUE"), CString("STARTTIME")));
    LONG nStartYear = atol(tYears.GetAt(CString("VALUE"), CString("STARTYEAR")));
    LONG nNormalMonthFmt = atol(tYears.GetAt(CString("VALUE"), CString("NORMALDAYMONTHFORMAT")));
    LONG nSpecialMonthFmt = atol(tYears.GetAt(CString("VALUE"), CString("SPECIALDAYMONTHFORMAT")));

    // Compute game time (comp: uVar6 = iVar3 * 0xf + param_1)
    ULONG nTicks = nStartTime * 15 + nFromTime;
    ULONG nTotalDays = nTicks / 108000;
    ULONG nHours = (nTicks % 108000) / 4500;
    ULONG nDayMinutes = (nTicks % 4500) / 900;
    LONG nYear = nStartYear + nTotalDays / dwDaysPerYear;
    ULONG nRemainingDays = nTotalDays % dwDaysPerYear;

    // Find current month from MONTHS.2DA (comp: the do-while loop)
    DWORD dwMonth = 0;
    DWORD dwDayOfMonth = 0;
    LONG nMonthDays = 0;
    for (DWORD m = 0; m < dwMonthsPerYear; m++) {
        CString sKey;
        sKey.Format("%d", m);
        nMonthDays = atol(tMonths.GetAt(CString("DAYS"), sKey));
        if (nRemainingDays < (DWORD)nMonthDays) {
            dwMonth = m;
            dwDayOfMonth = nRemainingDays;
            break;
        }
        nRemainingDays -= nMonthDays;
    }

    if (dwMonth >= dwMonthsPerYear) return;

    // Get month name STRREF from MONTHS.2DA (comp: C2DArray__GetAt + CTlkTable__Fetch)
    CString sKey;
    sKey.Format("%d", dwMonth);
    LONG nMonthStrRef = atol(tMonths.GetAt(CString("NAME"), sKey));
    STR_RES strMonthName;
    g_pBaldurChitin->GetTlkTable().Fetch(nMonthStrRef, strMonthName);

    // Get month format STRREF (special for 1-day months, normal otherwise)
    LONG nMonthFmtStrRef = (nMonthDays == 1) ? nSpecialMonthFmt : nNormalMonthFmt;
    STR_RES strMonthFmt;
    g_pBaldurChitin->GetTlkTable().Fetch(nMonthFmtStrRef, strMonthFmt);

    // Format month name using the month format (e.g., "%d %s" or "%s")
    CString sMonthFormatted;
    sMonthFormatted.Format(strMonthFmt.szText, dwDayOfMonth + 1, strMonthName.szText);

    // Singular/plural for "day" (comp: TLK 0x29c9-0x29cd checks)
    STRREF strDayWord;
    if (nTotalDays == 0) {
        strDayWord = 0x29ca;  // "0 days" special case
    } else if (nTotalDays == 1) {
        strDayWord = 0x29c9;  // singular "day"
    } else {
        strDayWord = 0x29cb;  // plural "days"
    }
    STR_RES strDay;
    g_pBaldurChitin->GetTlkTable().Fetch(strDayWord, strDay);

    // Singular/plural for "hour" (comp: TLK 0x29cc-0x29cd)
    STRREF strHourWord;
    if (nHours == 1) {
        strHourWord = 0x29cc;  // singular "hour"
    } else {
        strHourWord = 0x29cd;  // plural "hours"
    }
    STR_RES strHour;
    g_pBaldurChitin->GetTlkTable().Fetch(strHourWord, strHour);

    // Fetch the caller's format STRREF and format final string
    STR_RES strFormat;
    g_pBaldurChitin->GetTlkTable().Fetch(strTimeFormat, strFormat);

    sTime.Format(strFormat.szText,
        nTotalDays, strDay.szText,
        nHours, strHour.szText,
        dwDayOfMonth + 1, (LPCSTR)strMonthName.szText, nYear);
}

// 0x54F970
void CTimerWorld::StartTime()
{
    m_active = TRUE;

    if (g_pBaldurChitin->m_pEngineWorld->m_nAutoHideInterface != 0) {
        g_pBaldurChitin->m_pEngineWorld->m_nAutoHideInterface--;
        g_pBaldurChitin->m_pEngineWorld->HideInterface();
    }

    if (g_pBaldurChitin->m_pEngineWorld->m_nAutoUnhideInterface > 0) {
        if (g_pBaldurChitin->GetObjectGame()->GetGameSave()->m_mode != 322) {
            g_pBaldurChitin->m_pEngineWorld->m_nAutoUnhideInterface--;
            g_pBaldurChitin->m_pEngineWorld->UnhideInterface();
        }
    }
}

// 0x54F9F0
void CTimerWorld::StopTime()
{
    m_active = FALSE;
    m_gameTime--;
    m_nLastPercentage = -1;

    if (g_pBaldurChitin->m_pEngineWorld->GetManager()->m_bHidden) {
        g_pBaldurChitin->m_pEngineWorld->m_nAutoHideInterface++;
        g_pBaldurChitin->m_pEngineWorld->UnhideInterface();
    }
}

// 0x45B540
BOOLEAN CTimerWorld::IsDay()
{
    ULONG time = m_gameTime % TIMESCALE_MSEC_PER_DAY;
    return time >= TIME_DAY && time < TIME_DUSK;
}

// 0x45B570
BOOLEAN CTimerWorld::IsNight()
{
    ULONG time = m_gameTime % TIMESCALE_MSEC_PER_DAY;
    return time >= TIME_NIGHT || time < TIME_DAWN;
}

// 0x45B5A0
BOOLEAN CTimerWorld::IsDawn()
{
    ULONG time = m_gameTime % TIMESCALE_MSEC_PER_DAY;
    return time >= TIME_DAWN && time < TIME_DAY;
}

// 0x45B5D0
BOOLEAN CTimerWorld::IsDusk()
{
    ULONG time = m_gameTime % TIMESCALE_MSEC_PER_DAY;
    return time >= TIME_DUSK && time < TIME_NIGHT;
}

// 0x45B500
BYTE CTimerWorld::GetCurrentHour()
{
    return m_gameTime % TIMESCALE_MSEC_PER_DAY / TIMESCALE_MSEC_PER_SEC / TIMESCALE_SEC_PER_MIN / TIMESCALE_MIN_PER_HOUR;
}

// NOTE: Inlined.
ULONG CTimerWorld::GetCurrentDayTime()
{
    return m_gameTime % TIMESCALE_MSEC_PER_DAY;
}

// NOTE: Inlined.
BOOLEAN CTimerWorld::IsDay(ULONG nTimeOfDay)
{
    ULONG time = nTimeOfDay % TIMESCALE_MSEC_PER_DAY;
    return time >= TIME_DAY && time < TIME_DUSK;
}

// NOTE: Inlined.
BOOLEAN CTimerWorld::IsNight(ULONG nTimeOfDay)
{
    ULONG time = nTimeOfDay % TIMESCALE_MSEC_PER_DAY;
    return time >= TIME_NIGHT || time < TIME_DAWN;
}

// NOTE: Inlined.
BOOLEAN CTimerWorld::IsDawn(ULONG nTimeOfDay)
{
    ULONG time = nTimeOfDay % TIMESCALE_MSEC_PER_DAY;
    return time >= TIME_DAWN && time < TIME_DAY;
}

// NOTE: Inlined.
BOOLEAN CTimerWorld::IsDusk(ULONG nTimeOfDay)
{
    ULONG time = nTimeOfDay % TIMESCALE_MSEC_PER_DAY;
    return time >= TIME_DUSK && time < TIME_NIGHT;
}

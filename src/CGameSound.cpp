#include "CGameSound.h"

#include "CBaldurChitin.h"
#include "CGameArea.h"
#include "CInfGame.h"
#include "CUtil.h"

static const DWORD CGAMESOUND_FLAG_ACTIVE = 0x1;
static const DWORD CGAMESOUND_FLAG_LOOPING = 0x2;
static const DWORD CGAMESOUND_FLAG_GLOBAL = 0x4;
static const DWORD CGAMESOUND_FLAG_RANDOM = 0x8;
static const DWORD CGAMESOUND_FLAG_LOW_MEMORY = 0x10;
static const int CGAMESOUND_CHANNEL_LOOPING = 16;
static const int CGAMESOUND_CHANNEL_RANDOM = 17;

static BYTE CGameSoundGetTimeOfDay()
{
    return g_pBaldurChitin->GetObjectGame()->GetWorldTimer()->GetCurrentHour();
}

static LONG CGameSoundGetPeriod(const CAreaFileSoundObject& soundObject)
{
    DWORD periodVarianceRange = 2 * soundObject.m_periodVariance + 1;
    DWORD periodVariance = 0;
    if (periodVarianceRange != 0) {
        periodVariance = rand() % periodVarianceRange;
    }

    return (soundObject.m_period - soundObject.m_periodVariance + periodVariance)
        * CTimerWorld::TIMESCALE_MSEC_PER_SEC;
}

static BOOL CGameSoundIsLowMemorySkipped(DWORD flags)
{
    return (flags & CGAMESOUND_FLAG_LOW_MEMORY) != 0 && g_pChitin->cDimm.GetMemoryAmount() == 1;
}

// 0x4C8B10
CGameSound::CGameSound(CGameArea* pArea, CAreaFileSoundObject* pSoundObject)
{
    // __FILE__: C:\Projects\Icewind2\src\Baldur\CGameSound.cpp
    // __LINE__: 107
    UTIL_ASSERT(pArea != NULL && pSoundObject != NULL);

    m_period = 0;
    m_periodCount = 0;
    m_currentSound = 0;
    m_objectType = TYPE_SOUND;
    memcpy(&m_soundObject, pSoundObject, sizeof(CAreaFileSoundObject));

    // __FILE__: C:\Projects\Icewind2\src\Baldur\CGameSound.cpp
    // __LINE__: 117
    UTIL_ASSERT(m_soundObject.m_soundObjectNum > 0);

    m_bLoopPlaying = FALSE;

    // TODO: Some strange call, check.
    m_timeOfDayActive = m_soundObject.m_timeOfDayActive;

    if (m_timeOfDayActive == 0x3FFC0) {
        m_timeOfDayActive = 0x3FFF80;
    } else if (m_timeOfDayActive == ~0x3FFC0) {
        m_timeOfDayActive = ~0x3FFF80;
    }

    if (m_soundObject.m_volume > 100) {
        // FIXME: Unused.
        CString sArea;
        pArea->m_resRef.CopyToString(sArea);

        m_soundObject.m_volume = 100;
    }

    if (m_soundObject.m_soundObjectNum == 1
        && (m_soundObject.m_dwFlags & 0x2) != 0) {
        if ((m_soundObject.m_dwFlags & 0x10) == 0 || g_pChitin->cDimm.GetMemoryAmount() != 1) {
            // NOTE: Uninline.
            m_looping.SetResRef(CResRef(m_soundObject.m_soundObject[0]), TRUE, TRUE);
        }

        m_looping.SetChannel(CGAMESOUND_CHANNEL_LOOPING, reinterpret_cast<DWORD>(pArea));
        m_looping.SetLoopingFlag(1);
        if ((m_soundObject.m_dwFlags & 0x4) == 0) {
            m_looping.SetRange(m_soundObject.m_range);
        }
        m_looping.m_nPitchVariance = m_soundObject.m_pitchVariance;
        m_looping.SetVolume(m_soundObject.m_volume);
        m_looping.m_nVolumeVariance = m_soundObject.m_volumeVariance;
    } else {
        m_soundObject.m_dwFlags &= ~0x2;
        if (m_soundObject.m_periodVariance > m_soundObject.m_period / 2) {
            m_soundObject.m_periodVariance = m_soundObject.m_period / 2;
        }

        int v1;
        if (2 * m_soundObject.m_periodVariance == -1) {
            v1 = 0;
        } else {
            v1 = rand() % (2 * m_soundObject.m_periodVariance + 1);
        }

        m_periodCount = 0;
        m_currentSound = static_cast<BYTE>(m_soundObject.m_soundObjectNum);
        m_period = (v1 + m_soundObject.m_period - m_soundObject.m_periodVariance) * CTimerWorld::TIMESCALE_MSEC_PER_SEC;
    }

    BYTE rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id,
        this,
        INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        CAIObjectType typeAI;
        typeAI.Set(m_typeAI);
        typeAI.SetName(pSoundObject->m_scriptName);
        m_typeAI.Set(typeAI);

        CVariable name;
        name.SetName(GetAIType().GetName());
        name.m_intValue = m_id;
        pArea->GetNamedCreatures()->AddKey(name);

        AddToArea(pArea,
            CPoint(m_soundObject.m_posX, m_soundObject.m_posY),
            0,
            LIST_BACK);
    } else {
        delete this;
    }
}

// 0x4C9010
CGameSound::~CGameSound()
{
    if (m_bLoopPlaying) {
        m_looping.Stop();
    }
}

// 0x47C830
BOOLEAN CGameSound::CanSaveGame(STRREF& strError)
{
    strError = -1;
    return TRUE;
}

// 0x766660
BOOLEAN CGameSound::CompressTime(DWORD deltaTime)
{
    return TRUE;
}

// 0x4C90A0
BOOLEAN CGameSound::DoAIUpdate(BOOLEAN active, LONG counter)
{
    if (!active) {
        return FALSE;
    }

    if ((m_id & m_AISpeed) != (counter & m_AISpeed)) {
        return FALSE;
    }

    DWORD flags = m_soundObject.m_dwFlags;
    if ((flags & CGAMESOUND_FLAG_ACTIVE) == 0) {
        return FALSE;
    }

    BYTE timeOfDay = CGameSoundGetTimeOfDay();

    if ((flags & CGAMESOUND_FLAG_LOOPING) != 0) {
        BOOL bTimeActive = ((m_timeOfDayActive >> timeOfDay) & 0x1) != 0;
        if (bTimeActive) {
            if (!m_bLoopPlaying && !CGameSoundIsLowMemorySkipped(flags)) {
                if ((flags & CGAMESOUND_FLAG_GLOBAL) != 0) {
                    m_looping.Play(FALSE);
                } else {
                    m_looping.Play(m_pos.x, m_pos.y, 0, FALSE);
                }
                m_bLoopPlaying = TRUE;
            }
        } else if (m_bLoopPlaying) {
            m_looping.Stop();
            m_bLoopPlaying = FALSE;
        }

        return FALSE;
    }

    if (((m_soundObject.m_timeOfDayActive >> timeOfDay) & 0x1) == 0) {
        return FALSE;
    }

    LONG periodCount = m_periodCount++;
    if (periodCount != m_period) {
        return FALSE;
    }

    CSound sound;
    BOOL bLowMemorySkipped = CGameSoundIsLowMemorySkipped(flags);

    if (!bLowMemorySkipped && m_soundObject.m_soundObjectNum > 0) {
        BYTE index;
        if ((flags & CGAMESOUND_FLAG_RANDOM) != 0) {
            index = static_cast<BYTE>(rand() % m_soundObject.m_soundObjectNum);
        } else {
            index = static_cast<BYTE>((m_currentSound + 1) % m_soundObject.m_soundObjectNum);
            m_currentSound = index;
        }

        sound.SetResRef(CResRef(m_soundObject.m_soundObject[index]), TRUE, TRUE);
    }

    sound.SetFireForget(TRUE);
    sound.SetChannel(CGAMESOUND_CHANNEL_RANDOM, reinterpret_cast<DWORD>(m_pArea));
    if ((flags & CGAMESOUND_FLAG_GLOBAL) == 0) {
        sound.SetRange(m_soundObject.m_range);
    }
    sound.m_nPitchVariance = m_soundObject.m_pitchVariance;
    sound.SetVolume(m_soundObject.m_volume);
    sound.m_nVolumeVariance = m_soundObject.m_volumeVariance;

    if (!bLowMemorySkipped) {
        if ((flags & CGAMESOUND_FLAG_GLOBAL) != 0) {
            sound.Play(FALSE);
        } else {
            sound.Play(m_pos.x, m_pos.y, 0, FALSE);
        }
    }

    m_periodCount = 0;
    m_period = CGameSoundGetPeriod(m_soundObject);

    return FALSE;
}

// 0x4C95B0
void CGameSound::RemoveFromArea()
{
    CGameObject::RemoveFromArea();

    BYTE rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(m_id,
        CGameObjectArray::THREAD_ASYNCH,
        NULL,
        INFINITE);
    if (rc != CGameObjectArray::SUCCESS) {
        // __FILE__: C:\Projects\Icewind2\src\Baldur\CGameSound.cpp
        // __LINE__: 346
        UTIL_ASSERT(FALSE);
    }

    delete this;
}

// 0x4C9610
void CGameSound::SetActive(BOOL bActive)
{
    if (bActive) {
        if ((m_soundObject.m_dwFlags & 0x1) == 0) {
            m_soundObject.m_dwFlags |= 0x1;
        }
    } else {
        if ((m_soundObject.m_dwFlags & 0x1) != 0) {
            if (m_bLoopPlaying) {
                m_looping.Stop();
                m_bLoopPlaying = FALSE;
            }

            m_soundObject.m_dwFlags &= ~0x1;
        }
    }
}

// 0x4C9670
BOOL CGameSound::IsActive()
{
    return (m_soundObject.m_dwFlags & 0x1) != 0;
}

// 0x4C9680
void CGameSound::Marshal(CAreaFileSoundObject** pSoundObject)
{
    // __FILE__: C:\Projects\Icewind2\src\Baldur\CGameSound.cpp
    // __LINE__: 432
    UTIL_ASSERT(pSoundObject != NULL);

    *pSoundObject = new CAreaFileSoundObject;

    // __FILE__: C:\Projects\Icewind2\src\Baldur\CGameSound.cpp
    // __LINE__: 435
    UTIL_ASSERT(*pSoundObject != NULL);

    memcpy(*pSoundObject, &m_soundObject, sizeof(CAreaFileSoundObject));
}

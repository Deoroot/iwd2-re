#include "CSpell.h"

#include "CBaldurChitin.h"
#include "CGameEffect.h"
#include "CGameSprite.h"
#include "CInfGame.h"
#include "CRuleTables.h"
#include "CUtil.h"

// NOTE: Inlined.
CSpell::CSpell()
{
}

// FIXME: `res` should be reference.
//
// 0x585FD0
CSpell::CSpell(CResRef res)
{
    SetResRef(res, TRUE, TRUE);
}

// 0x54A380
BOOL CSpell::Demand()
{
    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();

            return pRes != NULL;
        }
    }

    return FALSE;
}

// 0x54A3C0
BOOL CSpell::Release()
{
    if (pRes != NULL) {
        pRes->Release();

        return TRUE;
    }

    return FALSE;
}

// NOTE: For unknown reason this function is not `const`.
//
// 0x54A3E0
INT CSpell::GetAbilityCount()
{
    INT nAbilityCount = 0;

    // NOTE: Probably inlined `Demand` and `Release`.
    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nAbilityCount = pRes->m_pHeader->abilityCount;
                pRes->Release();
            }
        }
    }

    return nAbilityCount;
}

// NOTE: For unknown reason this function is not `const`.
//
// 0x54A430
BOOL CSpell::GetIcon(BYTE* resRef)
{
    // NOTE: Probably inlined `Demand` and `Release`.
    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                CResRef iconResRef(pRes->m_pHeader->itemIcon);
                iconResRef.GetResRef(resRef);
                pRes->Release();
            }
        }
    }

    return FALSE;
}

// 0x54A4A0
SPELL_ABILITY* CSpell::GetAbility(LONG abilityNum) const
{
    SPELL_ABILITY* pAbility = NULL;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                pAbility = pRes->GetAbility(abilityNum);
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 265
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return pAbility;
}

// 0x54A510
//
// Builds a configured `CGameEffect` instance from this spell's ability-effect
// slot at (`nAbility`, `nEffectIdx`), stamped for `pCaster` at `nLevel`
// (autocalc from caster + nClass + nSpecialization when `nLevel == 0`).
//
// Output effect layout follows IWD2's CGameEffect packing (not BG2EE's):
//   +0x48 -> spell school (`pHeader->school`)
//   +0x4C -> secondaryType (`pHeader->secondaryType`)
//   +0x8C -> set to 1 (cast-source marker)
//   +0x98 -> spell itemFlags (`pHeader->itemFlags`)
//   +0xC4 -> packed { byte 0: caster level, byte 1: nClass, byte 2: spec idx (cleric only) }
//   +0xC8 -> Empower-Spell feat value * 2
//   +0x40 -> damage bonus (+= specialization feat bonus, +2 when caster spec matches school)
//
// Returns NULL when: spell data unavailable, ability/effect index out of
// range, or effect decode failed. Caller takes ownership of the returned
// pointer.
//
// TODO: 2026-05-22 skeleton -- Empower-Spell-feat bonus block (decomp lines
// 101-145, depends on `FUN_00547040` per-class caster-level lookup and the
// `SPIN126-132` resref existence checks) is omitted; functional spells take
// the base damage path without the bonus. Recover when feat-bonus mechanics
// become testable.
CGameEffect* CSpell::BuildAbilityEffect(LONG nAbility, LONG nEffectIdx, CGameObject* pCaster, BYTE nClass, DWORD nSpecialization, BYTE nLevel)
{
    if (GetResRef() == "") {
        return NULL;
    }
    if (pRes == NULL) {
        return NULL;
    }
    pRes->Demand();
    if (pRes == NULL) {
        // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
        // __LINE__: 310
        UTIL_ASSERT(FALSE);
        return NULL;
    }
    int nEffectCount = pRes->GetAbilityEffectNo(nAbility);
    if (nEffectIdx >= nEffectCount) {
        pRes->Release();
        return NULL;
    }
    if (nLevel == 0) {
        if (pCaster == NULL || pCaster->GetObjectType() != CGameObject::TYPE_SPRITE) {
            nLevel = 10;
        } else {
            nLevel = static_cast<BYTE>(static_cast<CGameSprite*>(pCaster)->GetCasterLevel(this, nClass, nSpecialization));
        }
    }

    CPoint ptSource(-1, -1);
    CPoint ptTarget(-1, -1);
    ITEM_EFFECT* pAbilityEffect = pRes->GetAbilityEffect(nAbility, nEffectIdx, nLevel);
    CGameEffect* pEffect = CGameEffect::DecodeEffect(pAbilityEffect, ptSource, -1, ptTarget);
    if (pEffect == NULL) {
        pRes->Release();
        return NULL;
    }

    BYTE* pEffectBytes = reinterpret_cast<BYTE*>(pEffect);
    *reinterpret_cast<DWORD*>(pEffectBytes + 0x48) = pRes->m_pHeader->school;
    *reinterpret_cast<DWORD*>(pEffectBytes + 0x8C) = 1;
    *reinterpret_cast<DWORD*>(pEffectBytes + 0x4C) = pRes->m_pHeader->secondaryType;
    pEffect->m_sourceRes = GetResRef();
    *reinterpret_cast<DWORD*>(pEffectBytes + 0x98) = pRes->m_pHeader->itemFlags;

    if (pCaster != NULL && pCaster->GetObjectType() == CGameObject::TYPE_SPRITE) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(pCaster);
        DWORD nEffectSchool = *reinterpret_cast<DWORD*>(pEffectBytes + 0x48);
        if (nEffectSchool != 0) {
            BYTE nSpecSchool = g_pBaldurChitin->GetObjectGame()->GetRuleTables().MapCharacterSpecializationToSchool(static_cast<WORD>(pSprite->m_baseStats.m_specialization));
            UINT nSchoolFeat = 0;
            switch (nEffectSchool) {
            case 4: nSchoolFeat = 0x3C; break;
            case 6: nSchoolFeat = 0x3D; break;
            case 7: nSchoolFeat = 0x3E; break;
            case 8: nSchoolFeat = 0x3F; break;
            }
            if (nSchoolFeat != 0 && pSprite->HasFeat(nSchoolFeat) != 0) {
                INT nFeatValue = pSprite->GetFeatValue(nSchoolFeat);
                *reinterpret_cast<LONG*>(pEffectBytes + 0x40) += nFeatValue * 2;
            }
            if (nSpecSchool == nEffectSchool) {
                *reinterpret_cast<LONG*>(pEffectBytes + 0x40) += 2;
            }
        }

        // TODO: Empower-Spell-feat damage bonus block (FUN_00547040 + SPIN126-132 lookup).

        *reinterpret_cast<DWORD*>(pEffectBytes + 0xC4) = nLevel;
        *reinterpret_cast<LONG*>(pEffectBytes + 0xC8) = pSprite->GetFeatValue(0x40) * 2;
    } else {
        *reinterpret_cast<DWORD*>(pEffectBytes + 0xC4) = 1;
    }
    *reinterpret_cast<DWORD*>(pEffectBytes + 0xC4) |= (static_cast<DWORD>(nClass) & 0xFF) << 8;
    if (nClass == 3) {
        BYTE nSpecIdx = g_pBaldurChitin->GetObjectGame()->GetRuleTables().GetSpecializationIndex(3, nSpecialization);
        *reinterpret_cast<DWORD*>(pEffectBytes + 0xC4) |= static_cast<DWORD>(nSpecIdx) << 16;
    }

    pRes->Release();
    return pEffect;
}

// 0x54A970
SHORT CSpell::GetLevel() const
{
    SHORT nLevel = 0;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nLevel = static_cast<SHORT>(pRes->m_pHeader->spellLevel);
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 492
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return nLevel;
}

// 0x54A9E0
DWORD CSpell::GetNotUsableBy() const
{
    DWORD nNotUsableBy = 0;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nNotUsableBy = pRes->m_pHeader->notUsableBy;
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 536
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return nNotUsableBy;
}

// 0x54ACD0
STRREF CSpell::GetGenericName() const
{
    STRREF nGenericName = 0;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nGenericName = pRes->m_pHeader->genericName;
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 755
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return nGenericName;
}

// 0x54AA40
BOOL CSpell::CheckUsableBy(CGameSprite* pSprite) const
{
    // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
    // __LINE__: 623
    UTIL_ASSERT(pSprite != NULL);

    // NOTE: Uninline.
    DWORD dwNotUsableBy = GetNotUsableBy() & 0xE0000000;

    DWORD dwClassMask = pSprite->GetAIType().m_nClassMask;

    if ((dwClassMask & CLASSMASK_CLERIC) != 0
        && (dwNotUsableBy & 0x40000000) == 0) {
        return TRUE;
    }

    if ((dwClassMask & CLASSMASK_DRUID) != 0
        && (dwNotUsableBy & 0x80000000) == 0) {
        return TRUE;
    }

    if ((dwClassMask & CLASSMASK_SORCERER) != 0
        && (dwNotUsableBy & 0x80000000) == 0) {
        return TRUE;
    }

    if ((dwClassMask & CLASSMASK_WIZARD) != 0) {
        // NOTE: Uninline.
        BYTE nSchool = GetSchool();

        // NOTE: Uninline.
        dwNotUsableBy |= GetNotUsableBy() & 0x7FC0;

        switch (pSprite->GetSpecialization()) {
        case SPECMASK_WIZARD_ABJURER:
            return nSchool != 5 && nSchool != 8
                ? (dwNotUsableBy & 0x40) == 0
                : FALSE;
        case SPECMASK_WIZARD_CONJURER:
            return nSchool != 6
                ? (dwNotUsableBy & 0x80) == 0
                : FALSE;
        case SPECMASK_WIZARD_DIVINER:
            return nSchool != 2
                ? (dwNotUsableBy & 0x100) == 0
                : FALSE;
        case SPECMASK_WIZARD_ENCHANTER:
            return nSchool != 6
                ? (dwNotUsableBy & 0x200) == 0
                : FALSE;
        case SPECMASK_WIZARD_ILLUSIONIST:
            return nSchool != 7 && nSchool != 1
                ? (dwNotUsableBy & 0x400) == 0
                : FALSE;
        case SPECMASK_WIZARD_EVOKER:
            return nSchool != 3 && nSchool != 2
                ? (dwNotUsableBy & 0x800) == 0
                : FALSE;
        case SPECMASK_WIZARD_NECROMANCER:
            return nSchool != 5 && nSchool != 4
                ? (dwNotUsableBy & 0x1000) == 0
                : FALSE;
        case SPECMASK_WIZARD_TRANSMUTER:
            return nSchool != 7 && nSchool != 1
                ? (dwNotUsableBy & 0x2000) == 0
                : FALSE;
        }

        return (dwNotUsableBy & 0x4000) != 0;
    }

    if ((dwNotUsableBy & 0x0) == 0) {
        return TRUE;
    }

    return FALSE;
}

// 0x54AD30
STRREF CSpell::GetDescription() const
{
    STRREF nGenericDescription = 0;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nGenericDescription = pRes->m_pHeader->genericDescription;
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 799
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return nGenericDescription;
}

// 0x54AD90
SHORT CSpell::GetCasterType() const
{
    SHORT nCasterType = 0;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nCasterType = pRes->m_pHeader->itemType;
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 844
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return nCasterType;
}

// 0x54AE00
CGameEffect* CSpell::GetCastingEffect(int nEffect) const
{
    CGameEffect* pEffect = NULL;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();

            if (nEffect < pRes->GetCastingEffectNo()) {
                CPoint source(-1, -1);
                CPoint target(-1, -1);
                pEffect = CGameEffect::DecodeEffect(pRes->GetCastingEffect(nEffect),
                    source,
                    -1,
                    target);
            }

            pRes->Release();
        }
    }

    return pEffect;
}

// 0x54AEC0
SHORT CSpell::GetCastingEffectNo() const
{
    SHORT nCastingEffects;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nCastingEffects = pRes->GetCastingEffectNo();
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 940
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return nCastingEffects;
}

// 0x54AF30
WORD CSpell::GetAnimationType() const
{
    WORD nAnimationType = 0;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nAnimationType = pRes->m_pHeader->castingAnimationType;
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 985
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return nAnimationType;
}

// 0x54AFA0
DWORD CSpell::GetItemFlags() const
{
    DWORD dwItemFlags = 0;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                dwItemFlags = pRes->m_pHeader->itemFlags;
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 1076
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return dwItemFlags;
}

// 0x54B000
BYTE CSpell::GetSchool() const
{
    BYTE nSchool = 0;

    if (GetResRef() != "") {
        if (pRes != NULL) {
            pRes->Demand();
            if (pRes != NULL) {
                nSchool = pRes->m_pHeader->school;
                pRes->Release();
            } else {
                // __FILE__: C:\Projects\Icewind2\src\Baldur\CSpell.cpp
                // __LINE__: 1118
                UTIL_ASSERT(FALSE);
            }
        }
    }

    return nSchool;
}

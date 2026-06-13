#include "CProjectile.h"

#include <math.h>
#include <stdlib.h>

#include "CBaldurChitin.h"
#include "CGameEffect.h"
#include "CGameArea.h"
#include "CGameFireball3d.h"
#include "CMessage.h"
#include "CGameObjectArray.h"
#include "CParticle.h"
#include "CGameSprite.h"
#include "CGameTemporal.h"
#include "CInfinity.h"
#include "CInfGame.h"
#include "CPathSearch.h"
#include "CResBitmap.h"
#include "CUtil.h"
#include "IcewindMisc.h"

static LONG GetProjectileSourceDiagonalOffset(const CRect& rEllipse)
{
    LONG x = rEllipse.right;
    LONG y = rEllipse.bottom;
    LONG denom = x * x + y * y;
    if (denom == 0) {
        return 0;
    }

    return static_cast<LONG>(sqrt(static_cast<double>((x * x * y * y) / denom)));
}

static BOOL GetProjectileSourcePosition(LONG source, CPoint& pt)
{
    CGameObject* pSource;

    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(source,
            CGameObjectArray::THREAD_ASYNCH,
            &pSource,
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc != CGameObjectArray::SUCCESS) {
        return FALSE;
    }

    pt = pSource->GetPos();

    if (pSource->GetObjectType() == CGameObject::TYPE_SPRITE) {
        CGameSprite* pSprite = static_cast<CGameSprite*>(pSource);
        const CRect& rEllipse = pSprite->GetAnimation()->GetEllipseRect();
        LONG diagonalOffset = GetProjectileSourceDiagonalOffset(rEllipse);

        switch (pSprite->GetDirection()) {
        case 0:
        case 1:
            pt.y += rEllipse.bottom / 2;
            break;
        case 2:
        case 3:
            pt.x -= 2 * diagonalOffset;
            pt.y += 2 * diagonalOffset;
            break;
        case 4:
        case 5:
            pt.x -= 2 * rEllipse.right;
            pt.y += 1;
            break;
        case 6:
        case 7:
            pt.x -= 2 * diagonalOffset;
            pt.y -= 2 * diagonalOffset;
            break;
        case 8:
        case 9:
            pt.y -= 2 * rEllipse.bottom;
            break;
        case 10:
        case 11:
            pt.x += 2 * diagonalOffset;
            pt.y -= 2 * diagonalOffset;
            break;
        case 12:
        case 13:
            pt.x += 2 * rEllipse.right;
            pt.y += 1;
            break;
        case 14:
        case 15:
            pt.x += 2 * diagonalOffset;
            pt.y += 2 * diagonalOffset;
            break;
        default:
            break;
        }
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(source,
        CGameObjectArray::THREAD_ASYNCH,
        INFINITE);

    return TRUE;
}

// 0x6A3130
BOOLEAN CProjectile::IsProjectile()
{
    return TRUE;
}

// 0x5551B0
void CProjectile::RemoveSelf()
{
    RemoveFromArea();

    BYTE rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(m_id,
        CGameObjectArray::THREAD_ASYNCH,
        NULL,
        INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        delete this;
    }
}

// 0x51EA90
void CProjectile::AddEffect(CGameEffect* pEffect)
{
    pEffect->m_projectileType = m_projectileType;
    m_effectList.AddTail(pEffect);
}

// 0x529F10
void CProjectile::ClearEffects()
{
    POSITION pos = m_effectList.GetHeadPosition();
    while (pos != NULL) {
        CGameEffect* node = m_effectList.GetNext(pos);
        delete node;
    }
    m_effectList.RemoveAll();
}

// 0x529F40
LONG CProjectile::DetermineHeight(CGameSprite* pSprite)
{
    if (!m_bHasHeight) {
        return 0;
    }

    if (pSprite->GetObjectType() != TYPE_SPRITE) {
        return 32;
    }

    return pSprite->GetAnimation()->GetCastHeight();
}

// 0x78E740
void CProjectile::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
}

// 0x529FB0
void CProjectile::OnArrival()
{
    CProjectile* pProjectile;
    BYTE rc;

    if (m_callBackProjectile != CGameObjectArray::INVALID_INDEX) {
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(m_callBackProjectile,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pProjectile),
                INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

        if (rc != CGameObjectArray::SUCCESS) {
            return;
        }

        pProjectile->CallBack();

        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(m_callBackProjectile,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }

    // NOTE: Uninline.
    PlaySound(m_arrivalSoundRef, m_loopArrivalSound, TRUE);

    if (m_nTargetId != CGameObjectArray::INVALID_INDEX) {
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(m_nTargetId,
                CGameObjectArray::THREAD_ASYNCH,
                reinterpret_cast<CGameObject**>(&pProjectile),
                INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

        if (rc == CGameObjectArray::SUCCESS) {
            pProjectile->RemoveSelf();

            g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(m_nTargetId,
                CGameObjectArray::THREAD_ASYNCH,
                INFINITE);
        }
    }

    DeliverEffects();
    RemoveFromArea();

    rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(m_id,
        CGameObjectArray::THREAD_ASYNCH,
        NULL,
        INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        delete this;
    }
}

// 0x52A1A0
//
// Delivers the projectile's accumulated gameplay effects to its target on
// arrival (called from OnArrival). Resolves the target (m_targetId); if it
// cannot hold effects (not an AI object) they are discarded, otherwise each
// effect is handed to the target as a CMessageAddEffect (which, when run,
// AddEffect()s it onto the target -- i.e. the damage).
//
// PARTIAL vs 0x52A1A0: the original wraps the per-effect CMessageAddEffects in a
// message-list (subtype 105, ctor 0x5152C0 / vtbl 0x84D564 / Run 0x5157F0 -- a
// separate, reused container class) and queues that once; here we queue each
// recovered CMessageAddEffect directly (identical application path; ADD_EFFECT
// is already in the Iwd2MessageRunRecovered whitelist). Also deferred: the
// immunity gate (FUN_004E7120 projectile-type immunity + the target's
// per-caster-class immunity array at +0x2BF, then
// CGameEffect::FeedBackImmuneToResource) -- effects currently always apply.
void CProjectile::DeliverEffects()
{
    CGameObject* pTarget;
    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(m_targetId,
            CGameObjectArray::THREAD_ASYNCH, &pTarget, INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }

    if ((pTarget->GetObjectType() & CGameObject::TYPE_AIBASE) == 0) {
        // Target cannot hold effects -> discard them.
        ClearEffects();
    } else {
        POSITION pos = m_effectList.GetHeadPosition();
        while (pos != NULL) {
            CGameEffect* pEffect = m_effectList.GetNext(pos);
            CMessage* pMsg = new CMessageAddEffect(pEffect, m_sourceId, m_targetId);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(pMsg, FALSE);
        }
        // The effects are now owned by the queued messages; drop our references.
        m_effectList.RemoveAll();
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(m_targetId,
        CGameObjectArray::THREAD_ASYNCH, INFINITE);
}

// 0x52A480
SHORT CProjectile::GetDirection(CPoint target)
{
    CPoint ptStart;
    ptStart.x = m_pos.x;
    ptStart.y = 4 * m_pos.y / 3;

    CPoint ptTarget;
    ptTarget.x = target.x;
    ptTarget.y = 4 * target.y / 3;

    return CGameSprite::GetDirection(ptStart, ptTarget);
}

// 0x536FC0
// TRUE when the target is immune to this projectile: the projectile type is
// on the target's projectile-immunity list, or m_nSpellLevel (when <= 8)
// indexes a set slot of the spell-level immunity table (the table
// CGameEffectImmunityToSpellLevel writes).
BOOL CProjectile::IsTargetImmune(CGameSprite* pSprite)
{
    if (pSprite->GetDerivedStats()->m_cImmunitiesProjectile.OnList(m_projectileType) == 0
        && ((m_nSpellLevel & 0xFF) > 8
            || pSprite->GetDerivedStats()->m_cImmunitiesSpellLevel.m_levels[m_nSpellLevel & 0xFF] == 0)) {
        return FALSE;
    }

    return TRUE;
}

// 0x52A4E0
void CProjectile::PlaySound(CResRef resRef, BOOL loop, BOOL fireAndForget)
{
    m_sound.Stop();
    if (resRef != "") {
        m_sound.SetResRef(resRef, TRUE, TRUE);
        if (loop) {
            m_sound.SetLoopingFlag(TRUE);
        }
        if (fireAndForget) {
            m_sound.SetFireForget(TRUE);
        }
        m_sound.SetChannel(15, reinterpret_cast<DWORD>(m_pArea));
        m_sound.Play(m_pos.x, m_pos.y, 0, FALSE);
    }
}

// 0x78E730
void CProjectile::CallBack()
{
}

// 0x51EAF0
//
// Factory that maps a numeric projectile type to a concrete CProjectile
// subclass. The original dispatches ~327 hardcoded types (projectileType - 1
// indexes a 386-entry jump table) plus a generic path for types > 0x1000
// (handled by the school-overlay sub-factory at 0x560310). The casting-glow
// overlays (CProjectileBAM), the summon/spell-hit VFX (CProjectileSummonVFX) and
// the canonical travelling arrow (CProjectileArrow) are recovered here; the
// remaining hardcoded classes are left unimplemented rather than guessed.
CProjectile* CProjectile::DecodeProjectile(USHORT projectileType, CGameAIBase* pCaster, BYTE castDelay)
{
    IcewindCVisualEffect visualEffect;

    if (projectileType > 0x1000) {
        CProjectile* pSpellHit = CProjectileSummonVFX::DecodeSpellHitProjectile(
            projectileType - 0x1001, pCaster, FALSE);
        if (pSpellHit != NULL) {
            pSpellHit->m_projectileType = projectileType - 1;
        }
        return pSpellHit;
    }

    CProjectile* pProjectile = NULL;
    switch (projectileType) {
    case 0x2:
    case 0x5:
    case 0x6:
        // ARARROW -- the canonical travelling arrow.
        pProjectile = new CProjectileArrow();
        break;

    case 0x3: {
        // The exploding flame missile: flies its default SPFLMARR cell to the
        // target and runs the strike pass there, launch sound TRA_10.
        CProjectileExplodingFlame* pMissile = new CProjectileExplodingFlame();
        pMissile->m_bHasHeight = TRUE;
        pMissile->m_fireSoundRef = CResRef("TRA_10");
        pMissile->m_arrivalSoundRef = CResRef("");
        pProjectile = pMissile;
        break;
    }

    case 0x8:    // exploding thrown axe ("AXE")
    case 0xD:    // exploding crossbow bolt ("BOLT")
    case 0x12:   // exploding sling stone ("MAGICSTN")
    case 0x1C:   // exploding throwing dagger ("DAGGER")
    case 0x21:   // exploding dart ("DART")
    case 0x39: { // exploding spear ("SPEAR")
        // The exploding thrown-weapon missiles: the strike-pass leaf with its
        // cell swapped to the weapon BAM. Only the bolt has a launch sound.
        CProjectileExplodingWeapon* pMissile = new CProjectileExplodingWeapon();
        CResRef fireSoundRef("");
        switch (projectileType) {
        case 0x8: pMissile->SetVidCell(CResRef("AXE")); break;
        case 0xD:
            pMissile->SetVidCell(CResRef("BOLT"));
            fireSoundRef = "TRA_10";
            break;
        case 0x12: pMissile->SetVidCell(CResRef("MAGICSTN")); break;
        case 0x1C: pMissile->SetVidCell(CResRef("DAGGER")); break;
        case 0x21: pMissile->SetVidCell(CResRef("DART")); break;
        case 0x39: pMissile->SetVidCell(CResRef("SPEAR")); break;
        }
        pMissile->m_bHasHeight = TRUE;
        pMissile->m_fireSoundRef = fireSoundRef;
        pMissile->m_arrivalSoundRef = CResRef("");
        pProjectile = pMissile;
        break;
    }

    case 0x68:   // palette range 0x45
    case 0xCC: { // palette range 0x47
        // The palette-tinted exploding-missile variants: the strike-pass leaf
        // flying its SPFIREBL cell with the palette re-ranged and the trail
        // and explosion colour ranges stamped to match, launch sound TRA_06.
        // (The original hands the palette to the cell before re-ranging it.)
        CProjectileExplodingWeapon* pMissile = new CProjectileExplodingWeapon();
        BYTE rangeValue = projectileType == 0x68 ? 0x45 : 0x47;
        pMissile->m_pVidCell->SetPalette(pMissile->m_palette);
        pMissile->m_palette.SetRange(0, rangeValue, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
        memset(pMissile->m_trailColorRanges, rangeValue, sizeof(pMissile->m_trailColorRanges));
        pMissile->m_explodeColorRange = rangeValue;
        pMissile->m_bHasHeight = TRUE;
        pMissile->m_fireSoundRef = CResRef("TRA_06");
        pMissile->m_arrivalSoundRef = CResRef("");
        pProjectile = pMissile;
        break;
    }

    case 0x4F:
        // The invisible per-target strike bolt the exploding missiles fire.
        pProjectile = new CProjectileStrike();
        break;

    case 0x20:
    case 0x23:
    case 0x24: {
        // The thrown dart (standard dart items carry missile type 0x24),
        // silent launch.
        CProjectileDart* pDart = new CProjectileDart();
        pDart->m_bHasHeight = TRUE;
        pDart->m_fireSoundRef = CResRef("");
        pDart->m_arrivalSoundRef = CResRef("");
        pProjectile = pDart;
        break;
    }

    case 0x4: {
        // The flaming arrow: the flame-trailed leaf flying its default
        // SPFLMARR cell, launch sound TRA_24.
        CProjectileSPFLMARR* pMissile = new CProjectileSPFLMARR();
        pMissile->m_bHasHeight = TRUE;
        pMissile->m_fireSoundRef = CResRef("TRA_24");
        pMissile->m_arrivalSoundRef = CResRef("");
        pProjectile = pMissile;
        break;
    }

    case 0x9:    // thrown axe ("AXE")
    case 0xE:    // crossbow bolt ("BOLT")
    case 0x1D:   // throwing dagger ("DAGGER")
    case 0x22: { // dart ("DART")
        // The thrown/launched weapon missiles: the flame-trailed leaf with
        // its cell swapped to the weapon BAM, launch sound TRA_10.
        CProjectileSPFLMARR* pMissile = new CProjectileSPFLMARR();
        switch (projectileType) {
        case 0x9: pMissile->SetVidCell(CResRef("AXE")); break;
        case 0xE: pMissile->SetVidCell(CResRef("BOLT")); break;
        case 0x1D: pMissile->SetVidCell(CResRef("DAGGER")); break;
        case 0x22: pMissile->SetVidCell(CResRef("DART")); break;
        }
        pMissile->m_bHasHeight = TRUE;
        pMissile->m_fireSoundRef = CResRef("TRA_10");
        pMissile->m_arrivalSoundRef = CResRef("");
        pProjectile = pMissile;
        break;
    }

    case 0x66:   // palette range 0x45
    case 0x67:   // palette range 0x44
    case 0xBC: { // palette range 0x47
        // The palette-tinted weapon-missile variants: the flame-trailed leaf
        // flying its SPFLMARR cell with the palette re-ranged and the trail
        // colour ranges stamped to match, launch sound TRA_10. (The original
        // hands the palette to the cell before re-ranging it, in this order.)
        CProjectileSPFLMARR* pMissile = new CProjectileSPFLMARR();
        BYTE rangeValue = 0;
        switch (projectileType) {
        case 0x66: rangeValue = 0x45; break;
        case 0x67: rangeValue = 0x44; break;
        case 0xBC: rangeValue = 0x47; break;
        }
        pMissile->m_pVidCell->SetPalette(pMissile->m_palette);
        pMissile->m_palette.SetRange(0, rangeValue, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
        memset(pMissile->m_trailColorRanges, rangeValue, sizeof(pMissile->m_trailColorRanges));
        pMissile->m_bHasHeight = TRUE;
        pMissile->m_fireSoundRef = CResRef("TRA_10");
        pMissile->m_arrivalSoundRef = CResRef("");
        pProjectile = pMissile;
        break;
    }

    case 0xDA:
        // MMissiT -- Magic Missile homing sub-missile (spawned by the launcher).
        // The factory case stamps the projectile's launch ("whoosh") sound, which
        // CProjectileTravelling::Fire plays when the missile enters the area. The
        // launcher (CProjectileMagicMissileMulti) then clears it on every
        // sub-missile after the first, so a volley whooshes once. (The original
        // handler at 0x5252AD also sets m_bHasHeight = TRUE and clears
        // m_arrivalSoundRef -- inert while the launch-height path is stubbed.)
        pProjectile = new CProjectileMMissiT(0);
        pProjectile->m_fireSoundRef = CResRef("TRA_02");
        break;

    case 0x18: {
        // CHROMORB -- Chromatic Orb's travelling orb ("COrbT" BAM): the
        // copy-from-back tinted travelling VFX, launch sound TRA_23.
        IcewindCProjectileTravellingVFX* pOrb = new IcewindCProjectileTravellingVFX(CResRef("COrbT"));
        pOrb->m_visualEffect.SetCopyFromBack(TRUE);
        pOrb->m_fireSoundRef = CResRef("TRA_23");
        pOrb->m_bHasHeight = TRUE;
        pOrb->m_arrivalSoundRef = CResRef("");
        pProjectile = pOrb;
        break;
    }

    case 0x25: {
        // MAGICMIS -- the plain single Magic Missile bolt (SPMAGMIS BAM, no
        // sparkle trail), launch whoosh TRA_02 like the launcher's
        // sub-missiles.
        CProjectileSparkle* pSparkle = new CProjectileSparkle(CVidPalette::TYPE_RESOURCE);
        pSparkle->m_fireSoundRef = CResRef("TRA_02");
        pSparkle->m_bHasHeight = TRUE;
        pSparkle->m_arrivalSoundRef = CResRef("");
        pProjectile = pSparkle;
        break;
    }

    case 0x2F:   // SPARKLBL blue
    case 0x30:   // SPARKLGO gold
    case 0x31:   // SPARKLPU purple
    case 0x32:   // SPARKLIC ice
    case 0x33:   // SPARKLST stone
    case 0x34:   // SPARKLBK black
    case 0x35:   // SPARKLCH chromatic
    case 0x36:   // SPARKLRE red
    case 0x37:   // SPARKLGR green
    case 0xB8:   // SPARKLMA magenta
    case 0xB9: { // SPARKLOR orange
        // The coloured sparkle streams: a CProjectileSparkle with its cell
        // swapped to the "TRAVEL" dot and the trail colour stamped
        // (m_sparkleColor indexes the "STTRAVL1" colour-table bitmap rows).
        // Stone/black/chromatic skip the palette-bitmap request; the launch
        // sound varies per colour and is empty for most.
        CProjectileSparkle* pSparkle = new CProjectileSparkle(CVidPalette::TYPE_RESOURCE);
        pSparkle->SetVidCell(CResRef("TRAVEL"));
        SHORT sparkleColor = 0;
        CResRef fireSoundRef("");
        BOOL requestPalette = TRUE;
        switch (projectileType) {
        case 0x2F: sparkleColor = 2;   fireSoundRef = "TRA_04A"; break;
        case 0x30: sparkleColor = 4;   fireSoundRef = "TRA_04C"; break;
        case 0x31: sparkleColor = 6;   break;
        case 0x32: sparkleColor = 9;   fireSoundRef = "TRA_01";  break;
        case 0x33: sparkleColor = 0xA; requestPalette = FALSE;   break;
        case 0x34: sparkleColor = 1;   requestPalette = FALSE;   break;
        case 0x35: sparkleColor = 3;   requestPalette = FALSE;   break;
        case 0x36: sparkleColor = 7;   break;
        case 0x37: sparkleColor = 5;   fireSoundRef = "TRA_04D"; break;
        case 0xB8: sparkleColor = 0xB; fireSoundRef = "TRA_04A"; break;
        case 0xB9: sparkleColor = 0xC; fireSoundRef = "TRA_03";  break;
        }
        if (requestPalette) {
            pSparkle->SetTravelPalette(CString("STTRAVL1"));
        }
        pSparkle->m_bSparkleTrail = TRUE;
        pSparkle->m_sparkleColor = sparkleColor;
        pSparkle->m_fireSoundRef = fireSoundRef;
        pSparkle->m_bHasHeight = TRUE;
        pSparkle->m_arrivalSoundRef = CResRef("");
        pProjectile = pSparkle;
        break;
    }

    case 0x41: {
        // GAZE -- an invisible sparkle carrier at double speed (4x the BAM
        // base velocity after the constructor's own doubling); delivers its
        // payload with no visible missile.
        CProjectileSparkle* pSparkle = new CProjectileSparkle(CVidPalette::TYPE_RESOURCE);
        pSparkle->SetVidCell(CResRef("TRAVEL"));
        pSparkle->SetTravelPalette(CString("STTRAVL1"));
        pSparkle->m_visible = 0;
        pSparkle->m_bSparkleTrail = TRUE;
        pSparkle->m_sparkleColor = 0xA;
        pSparkle->m_bHasHeight = TRUE;
        pSparkle->m_velocity = static_cast<SHORT>(pSparkle->m_velocity << 1);
        pSparkle->m_fireSoundRef = CResRef("TRA_04C");
        pSparkle->m_arrivalSoundRef = CResRef("");
        pProjectile = pSparkle;
        break;
    }

    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
        // SPMAGMIS -- Magic Missile launcher; band count = projectileType - 0x43
        // (1..5 missiles), palette flag 1.
        pProjectile = new CProjectileSPMAGMIS(
            static_cast<SHORT>(projectileType - 0x43), 1);
        break;

    case 0x6F:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76: {
        // Spell-school casting-glow overlays.
        CResRef visualResRef;
        switch (projectileType) {
        case 0x6F: visualResRef = "NecroCG"; break;
        case 0x70: visualResRef = "AlterCG"; break;
        case 0x71: visualResRef = "EnchaCG"; break;
        case 0x72: visualResRef = "AbjurCG"; break;
        case 0x73: visualResRef = "IllusCG"; break;
        case 0x74: visualResRef = "ConjuCG"; break;
        case 0x75: visualResRef = "InvocCG"; break;
        case 0x76: visualResRef = "DivinCG"; break;
        }
        BYTE sequenceDelay = castDelay ? castDelay : 0x32;
        pProjectile = new CProjectileBAM(visualResRef, CResRef(""), sequenceDelay, 0, visualEffect);
        break;
    }

    case 0x121:
    case 0x122:
    case 0x123:
    case 0x124:
    case 0x125: {
        // Summon-group VFX overlays (mirror CreateSummonGroupProjectile):
        // copy-from-back tint, EFF_M13 arrival sound, offset above the target.
        CResRef visualResRef;
        switch (projectileType) {
        case 0x121: visualResRef = "MSumm1X"; break;
        case 0x122: visualResRef = "ASumm1X"; break;
        case 0x123: visualResRef = "CEElemX"; break;
        case 0x124: visualResRef = "CFElemX"; break;
        case 0x125: visualResRef = "CWElemX"; break;
        }
        visualEffect.SetCopyFromBack(TRUE);
        CProjectileSummonVFX* pSummon = new CProjectileSummonVFX(visualResRef, visualEffect);
        pSummon->SetArrivalSound(CResRef("EFF_M13"));
        pSummon->SetOffsetAboveTarget(TRUE);
        pProjectile = pSummon;
        break;
    }

    case 0x15B:
    case 0x161:
    case 0x162: {
        // Single spell-hit VFX overlays: copy-from-back tint only.
        CResRef visualResRef;
        switch (projectileType) {
        case 0x15B: visualResRef = "PortalH"; break;
        case 0x161: visualResRef = "IllusH"; break;
        case 0x162: visualResRef = "CCDamaH"; break;
        }
        visualEffect.SetCopyFromBack(TRUE);
        pProjectile = new CProjectileSummonVFX(visualResRef, visualEffect);
        break;
    }

    case 0x28: {
        // Lightning Bolt ("LightnT" BAM): SPWI002/308/997 and Eye of the Mage.
        // The line beam that rakes everything it crosses; height on, range 400.
        CProjectileLightningBolt* pBolt = new CProjectileLightningBolt(CResRef("LightnT"));
        pBolt->m_beamRange = 400;
        pBolt->m_targetMap.m_nRange = 50;
        pBolt->m_velocity = 20;
        pBolt->m_visualEffect.SetCopyFromBack(TRUE);
        pBolt->m_dirCount = 16;
        pBolt->m_bHasHeight = TRUE;
        pBolt->m_useHeightOffset = 1;
        pBolt->m_fireSoundRef = CResRef("TRA_09");
        pProjectile = pBolt;
        break;
    }

    case 0x12E: {
        // Smashing Wave ("SWaveX" BAM): SPPR522. Slower (10), ground-hugging
        // (no height) and its north-half facings are not folded.
        CProjectileLightningBolt* pBolt = new CProjectileLightningBolt(CResRef("SWaveX"));
        pBolt->m_velocity = 10;
        pBolt->m_dirCount = 16;
        pBolt->m_bMirrorNorth = 0;
        pBolt->m_visualEffect.SetCopyFromBack(TRUE);
        pBolt->m_bHasHeight = FALSE;
        pBolt->m_useHeightOffset = 0;
        pBolt->m_targetMap.m_nRange = 50;
        pBolt->m_beamRange = 400;
        pBolt->m_fireSoundRef = CResRef("TRA_56");
        pProjectile = pBolt;
        break;
    }

    case 0x139: {
        // Lance of Disruption ("LoDisrT" BAM): SPWI319. Longer rake (600).
        CProjectileLightningBolt* pBolt = new CProjectileLightningBolt(CResRef("LoDisrT"));
        pBolt->m_velocity = 20;
        pBolt->m_dirCount = 16;
        pBolt->m_visualEffect.SetCopyFromBack(TRUE);
        pBolt->m_bHasHeight = TRUE;
        pBolt->m_useHeightOffset = 1;
        pBolt->m_targetMap.m_nRange = 50;
        pBolt->m_beamRange = 600;
        pBolt->m_fireSoundRef = CResRef("TRA_59");
        pProjectile = pBolt;
        break;
    }

    case 0x17A: {
        // The acid-breath weapon ("HDABreT" BAM): SPIN222 ("Breathes Acid").
        // Same shape as the lance, range 600.
        CProjectileLightningBolt* pBolt = new CProjectileLightningBolt(CResRef("HDABreT"));
        pBolt->m_velocity = 20;
        pBolt->m_dirCount = 16;
        pBolt->m_visualEffect.SetCopyFromBack(TRUE);
        pBolt->m_bHasHeight = TRUE;
        pBolt->m_useHeightOffset = 1;
        pBolt->m_targetMap.m_nRange = 50;
        pBolt->m_beamRange = 600;
        pBolt->m_fireSoundRef = CResRef("TRA_59");
        pProjectile = pBolt;
        break;
    }

    case 0x131:
        // The wandering tornado (Whirlwind / Wing Buffet).
        pProjectile = new CProjectileWhirlwind();
        break;

    case 0x169:
        // Gate VFX overlay: no copy-from-back tint, no arrival sound.
        pProjectile = new CProjectileSummonVFX(CResRef("GateX"), visualEffect);
        break;

    case 0x2E:
        // 0x52026A: the unused sparkle slot -- the original asserts FALSE
        // (CProjectile.cpp:631) and then builds the same plain-CProjectile
        // fallback as the default case; same omitted-assert rationale.
    default:
        // 0x528DEF: unknown / unrecovered type.  The binary asserts FALSE
        // (CProjectile.cpp:3127) and then builds a plain CProjectile, so the
        // factory never returns NULL for an in-range type.  ~80 hardcoded
        // projectile classes still fall through here.
        // HACK: the assert is omitted -- the binary's UtilAssert (0x780C00)
        // shows a continueable "Run Debugger?" dialog, ours suspends and
        // shuts the game down, so asserting would kill every cast with an
        // unrecovered projectile class -- replaces 0x528DEF.
        pProjectile = new CProjectile();
        break;
    }

    // Common tail (0x528E1C): the factory stamps the 0-based projectile type on
    // every object it builds before returning it.
    if (pProjectile != NULL) {
        pProjectile->m_projectileType = projectileType - 1;
    }
    return pProjectile;
}

// -----------------------------------------------------------------------------
// INCOMPLETE: only CProjectileBAM subclass recovered; the global projectile
// dispatch switch (0x57B0C0) and remaining projectile types are not yet
// implemented. Fire/AIUpdate/Render mapped from Ghidra.

// 0x530790
//
// Default constructor, used by DecodeProjectile's default case (every
// subclass constructor inlines this same body before its own overrides, so
// subclasses run it implicitly here too).  The binary's store of the MFC
// nil-string sentinel (0x8C1758) into field_17E is the inlined CString
// default constructor -- our implicit member init.  The word zeroed at
// +0x9A is m_sparkleColor (the field right after the plain effect list),
// and the +0xE0/+0xE6 zeroes are m_driftDecay/m_renderFlags, declared on
// CProjectileTravelling but cleared by this base constructor.
CProjectile::CProjectile()
{
    m_nSpellLevel = 0;
    m_projectileType = 0;
    field_70 = 0;
    m_sourceId = 0;
    m_targetId = 0;
    m_sparkleColor = 0;
    m_pArea = NULL;
    m_nDeltaZ = 0;
    m_nDeltaZLast = 0;
    m_nOrigDistance = 0;
    field_17C = 0;
    m_callBackProjectile = -1;
    m_bHasHeight = FALSE;
    m_fireSoundRef = "";
    m_loopFireSound = FALSE;
    m_arrivalSoundRef = "";
    m_loopArrivalSound = FALSE;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x57CAC0
CProjectileBAM::CProjectileBAM(const CResRef& visualResRef, const CResRef& arrivalSoundRef, BYTE sequenceDelay, BYTE initialDelay, const IcewindCVisualEffect& visualEffect)
    : m_visualEffect(visualEffect)
{
    m_projectileType = 0;
    m_sourceId = CGameObjectArray::INVALID_INDEX;
    m_targetId = CGameObjectArray::INVALID_INDEX;
    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_pArea = NULL;
    m_arrivalSoundRef = arrivalSoundRef;
    m_loopArrivalSound = FALSE;
    m_bHasHeight = TRUE;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;

    m_vidCell.SetResRef(visualResRef, FALSE, TRUE, TRUE);
    m_vidCell.FrameSet(0);

    m_sequenceDelay = sequenceDelay;
    BYTE sequenceLength = m_vidCell.GetSequenceLength(2, FALSE);
    if (sequenceLength < m_sequenceDelay) {
        m_vidCell.SequenceSet(0);
        m_sequenceDelay -= sequenceLength;
    } else {
        m_vidCell.SequenceSet(2);
        m_sequenceDelay = 0;
    }

    m_initialDelay = initialDelay;
}

// 0x57CEE0 (virtual)
void CProjectileBAM::AIUpdate()
{
    if (m_initialDelay != 0) {
        m_initialDelay--;
        return;
    }

    if (m_sequenceDelay == 0) {
        if (m_vidCell.GetCurrentSequenceId() == 2) {
            if (m_vidCell.IsEndOfSequence(FALSE)) {
                OnArrival();
                return;
            }

            m_vidCell.FrameAdvance();
        } else {
            m_vidCell.SequenceSet(2);
            m_vidCell.FrameSet(0);
        }
    } else {
        m_sequenceDelay--;
        if (!m_vidCell.IsEndOfSequence(FALSE)) {
            m_vidCell.FrameAdvance();
        } else {
            m_vidCell.SequenceSet(1);
            m_vidCell.FrameSet(0);
        }
    }

    m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
}

// 0x57CFB0 (virtual)
void CProjectileBAM::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    CRect rFX;
    CSize frameSize;
    CRect rGCBounds;
    CPoint newPos;
    CPoint ptReference;
    CInfinity* pInfinity;

    (void)pVidMode;

    if (pArea == NULL || m_initialDelay != 0) {
        return;
    }

    pInfinity = pArea->GetInfinity();

    m_vidCell.GetCurrentCenterPoint(ptReference, FALSE);
    m_vidCell.GetCurrentFrameSize(frameSize, FALSE);

    rFX.SetRect(0, 0, frameSize.cx, frameSize.cy);

    newPos.x = m_pos.x;
    newPos.y = pArea->GetHeightOffset(m_pos, m_listType) + m_pos.y - m_posZ;

    DWORD dwPrepFlags;
    if (m_visualEffect.m_dwFlags != 0) {
        dwPrepFlags = CInfinity::FXPREP_COPYFROMBACK;
    } else {
        dwPrepFlags = CInfinity::FXPREP_CLEARFILL;
    }

    rGCBounds.left = newPos.x - ptReference.x;
    rGCBounds.top = newPos.y - ptReference.y;
    rGCBounds.right = rGCBounds.left + rFX.Width();
    rGCBounds.bottom = rGCBounds.top + rFX.Height();

    pInfinity->FXPrep(rFX,
        dwPrepFlags,
        nSurface,
        newPos,
        ptReference);

    if (pInfinity->FXLock(rFX, dwPrepFlags)) {
        pInfinity->FXRender(&m_vidCell,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags,
            m_visualEffect.m_nTransValue);

        pInfinity->FXRenderClippingPolys(newPos.x,
            newPos.y,
            0,
            ptReference,
            rGCBounds,
            FALSE,
            dwPrepFlags);

        pInfinity->FXUnlock(dwPrepFlags, NULL, CPoint(0, 0));
        pInfinity->FXBltFrom(nSurface,
            rFX,
            newPos.x,
            newPos.y,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags | 0x1);
    }
}

// 0x57D230 (virtual)
void CProjectileBAM::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
    CGameObject* pTarget;
    CPoint sourcePos;
    BYTE rc;

    (void)nType;

    m_sourceId = source;
    m_targetId = target;
    m_pArea = pArea;
    sourcePos = targetPos;

    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(target,
            CGameObjectArray::THREAD_ASYNCH,
            &pTarget,
            INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);

    if (rc == CGameObjectArray::SUCCESS) {
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(target,
            CGameObjectArray::THREAD_ASYNCH,
            INFINITE);
    }

    GetProjectileSourcePosition(source, sourcePos);
    m_sound.Play(sourcePos.x, sourcePos.y, 0, FALSE);

    rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        CGameObject::AddToArea(pArea, sourcePos, nHeight, LIST_FRONT);
        DeliverEffects();
    } else {
        delete this;
    }
}

// -----------------------------------------------------------------------------

// 0x57E490
CProjectileSummonVFX::CProjectileSummonVFX(const CResRef& visualResRef, const IcewindCVisualEffect& visualEffect)
    : m_visualEffect(visualEffect)
{
    m_projectileType = 0;
    m_sourceId = CGameObjectArray::INVALID_INDEX;
    m_targetId = CGameObjectArray::INVALID_INDEX;
    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_pArea = NULL;
    m_arrivalSoundRef = "";
    m_loopArrivalSound = FALSE;
    m_bHasHeight = FALSE;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
    m_offsetAboveTarget = FALSE;

    m_vidCell.SetResRef(visualResRef, FALSE, TRUE, TRUE);
    m_vidCell.SequenceSet(0);
    m_vidCell.FrameSet(0);
}

// 0x57E580 (virtual)
void CProjectileSummonVFX::AIUpdate()
{
    if (m_vidCell.IsEndOfSequence(FALSE)) {
        OnArrival();
        return;
    }

    m_vidCell.FrameAdvance();
    m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
}

// Models the inherited family Render (0x578480, recovered as
// IcewindCProjectileTravellingVFX::Render) under this class's divergent
// local layout; in the binary CProjectileSummonVFX is a family child
// (vtable 0x851234 slot 19 = 0x578480) -- re-parenting is pending.
void CProjectileSummonVFX::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    CRect rFX;
    CSize frameSize;
    CRect rGCBounds;
    CPoint newPos;
    CPoint ptReference;
    CInfinity* pInfinity;

    (void)pVidMode;

    if (pArea == NULL) {
        return;
    }

    pInfinity = pArea->GetInfinity();

    m_vidCell.GetCurrentCenterPoint(ptReference, FALSE);
    m_vidCell.GetCurrentFrameSize(frameSize, FALSE);

    rFX.SetRect(0, 0, frameSize.cx, frameSize.cy);

    newPos.x = m_pos.x;
    newPos.y = pArea->GetHeightOffset(m_pos, m_listType) + m_pos.y - m_posZ;

    DWORD dwPrepFlags;
    if (m_visualEffect.m_dwFlags != 0) {
        dwPrepFlags = CInfinity::FXPREP_COPYFROMBACK;
    } else {
        dwPrepFlags = CInfinity::FXPREP_CLEARFILL;
    }

    rGCBounds.left = newPos.x - ptReference.x;
    rGCBounds.top = newPos.y - ptReference.y;
    rGCBounds.right = rGCBounds.left + rFX.Width();
    rGCBounds.bottom = rGCBounds.top + rFX.Height();

    pInfinity->FXPrep(rFX,
        dwPrepFlags,
        nSurface,
        newPos,
        ptReference);

    if (pInfinity->FXLock(rFX, dwPrepFlags)) {
        pInfinity->FXRender(&m_vidCell,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags,
            m_visualEffect.m_nTransValue);

        pInfinity->FXRenderClippingPolys(newPos.x,
            newPos.y,
            0,
            ptReference,
            rGCBounds,
            FALSE,
            dwPrepFlags);

        pInfinity->FXUnlock(dwPrepFlags, NULL, CPoint(0, 0));
        pInfinity->FXBltFrom(nSurface,
            rFX,
            newPos.x,
            newPos.y,
            ptReference.x,
            ptReference.y,
            m_visualEffect.m_dwFlags | 0x1);
    }
}

// 0x57E710 (virtual)
void CProjectileSummonVFX::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
    BYTE rc;

    (void)nHeight;
    (void)nType;

    m_sourceId = source;
    m_targetId = target;
    m_pArea = pArea;
    m_pos.x = targetPos.x;
    m_pos.y = m_offsetAboveTarget ? targetPos.y - 100 : targetPos.y + 1;
    m_posZ = 0;

    // Per-cast visual variety (binary 0x57E776): randomize the overlay vidcell's
    // starting sequence.  The constructor hardcodes the randomize flag (+0x2b8)
    // to 1 (0x57E50A), so the pick is unconditional -- when the BAM holds more
    // than one sequence choose one at random, else sequence 0.  Overrides the
    // constructor's fixed SequenceSet(0) so repeated hits don't always show the
    // same variant.
    SHORT nSeqCount = m_vidCell.GetNumberSequences(FALSE) & 0xFF;
    m_vidCell.SequenceSet(nSeqCount > 1 ? static_cast<SHORT>(rand() % nSeqCount) : 0);

    rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE);
    if (rc == CGameObjectArray::SUCCESS) {
        CGameObject::AddToArea(pArea, m_pos, 0, CGAMEOBJECT_LIST_FRONT);
        // Impact cue (binary 0x57E838): the overlay plays its fire-sound on launch
        // -- the spell-hit sound (e.g. EFF_M06 for an Invocation hit such as Magic
        // Missile).  The original passes the loop flag at +0x15a (one-shot for these
        // cues, not modelled here) and fireAndForget FALSE.
        PlaySound(m_fireSoundRef, FALSE, FALSE);
        DeliverEffects();
    } else {
        delete this;
    }
}

void CProjectileSummonVFX::SetArrivalSound(const CResRef& arrivalSoundRef)
{
    m_arrivalSoundRef = arrivalSoundRef;
}

void CProjectileSummonVFX::SetOffsetAboveTarget(BOOL offsetAboveTarget)
{
    m_offsetAboveTarget = offsetAboveTarget;
}

// 0x560310
//
// Sub-factory for the casting/"spell hit" overlay projectiles -- the
// DecodeProjectile path for projectileType > 0x1000 (typeIndex =
// projectileType - 0x1001, bPositive selects the good/evil sound variant).
// 107 of the 112 cases build a CProjectileSummonVFX overlay (resref "<spell>H"
// or "<x>X") with per-case copy-from-back / tint-from-flags / caster color-glow
// flash / fire sound / offset-above-target.
//
// Not yet recovered (faithfully omitted, documented per case): the aura-attach
// config on a few overlays, and 4 exotic projectile classes (CallLightning x3
// at 0x5348C0, Sunray at 0x57E860). The color-glow is applied via the caster's
// AddEffect (vtbl+0x78) exactly as the original; AddEffect's own full immunity
// filter (0x733050) is a separate arc.
CProjectile* CProjectileSummonVFX::DecodeSpellHitProjectile(int typeIndex, CGameAIBase* pCaster, BOOL bPositive)
{
    CProjectileSummonVFX* p = NULL;
    switch (typeIndex) {
    case 0:  // plain base CProjectile (ctor 0x530790)
        return new CProjectile();
    case 1: {
        p = new CProjectileSummonVFX(CResRef("AbjurH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0xD7, 0x8C, 0x46, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P01") : CResRef("EFF_M02");
        break;
    }
    case 2: {
        p = new CProjectileSummonVFX(CResRef("AlterH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x4B, 0xD2, 0xA0, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P07") : CResRef("EFF_M08");
        break;
    }
    case 3: {
        p = new CProjectileSummonVFX(CResRef("InvocH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x6B, 0x06, 0xC9, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P05") : CResRef("EFF_M06");
        break;
    }
    case 4: {
        p = new CProjectileSummonVFX(CResRef("NecroH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0xB4, 0xD2, 0x50, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P06") : CResRef("EFF_M07");
        break;
    }
    case 5: {
        p = new CProjectileSummonVFX(CResRef("ConjuH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x69, 0xD7, 0x46, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P02") : CResRef("EFF_M03");
        break;
    }
    case 6: {
        p = new CProjectileSummonVFX(CResRef("EnchaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x50, 0xD2, 0x50, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P04") : CResRef("EFF_M05");
        break;
    }
    case 7: {
        p = new CProjectileSummonVFX(CResRef("IllusH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x50, 0x5A, 0xD2, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = CResRef("EFF_M34");
        break;
    }
    case 8: {
        p = new CProjectileSummonVFX(CResRef("DivinH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x64, 0x46, 0xD2, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = bPositive ? CResRef("EFF_P03") : CResRef("EFF_M04");
        break;
    }
    case 9: {
        p = new CProjectileSummonVFX(CResRef("ArmorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M48");
        break;
    }
    case 10: {
        p = new CProjectileSummonVFX(CResRef("SArmorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M36");
        break;
    }
    case 11: {
        p = new CProjectileSummonVFX(CResRef("GArmorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M36");
        break;
    }
    case 12: {
        p = new CProjectileSummonVFX(CResRef("StrengH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M12");
        break;
    }
    case 13: {
        p = new CProjectileSummonVFX(CResRef("ConfusH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M16");
        break;
    }
    case 14: {
        p = new CProjectileSummonVFX(CResRef("SOFlamH"), IcewindCVisualEffect());
        break;
    }
    case 15: {
        p = new CProjectileSummonVFX(CResRef("DSpellH"), IcewindCVisualEffect());
        break;
    }
    case 16: {
        p = new CProjectileSummonVFX(CResRef("DisintH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M43");
        break;
    }
    case 17: {
        p = new CProjectileSummonVFX(CResRef("PWSileH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M44");
        break;
    }
    case 18: {
        p = new CProjectileSummonVFX(CResRef("None"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        break;
    }
    case 19: {
        p = new CProjectileSummonVFX(CResRef("FODeatH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M07");
        break;
    }
    case 20: {
        p = new CProjectileSummonVFX(CResRef("MSwordH"), IcewindCVisualEffect());
        break;
    }
    case 21: {
        p = new CProjectileSummonVFX(CResRef("MSumm1H"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 22: {
        p = new CProjectileSummonVFX(CResRef("MSumm2H"), IcewindCVisualEffect());
        break;
    }
    case 23: {
        p = new CProjectileSummonVFX(CResRef("MSumm3H"), IcewindCVisualEffect());
        break;
    }
    case 24: {
        p = new CProjectileSummonVFX(CResRef("MSumm4H"), IcewindCVisualEffect());
        break;
    }
    case 25: {
        p = new CProjectileSummonVFX(CResRef("MSumm5H"), IcewindCVisualEffect());
        break;
    }
    case 26: {
        p = new CProjectileSummonVFX(CResRef("MSumm6H"), IcewindCVisualEffect());
        break;
    }
    case 27: {
        p = new CProjectileSummonVFX(CResRef("MSumm7H"), IcewindCVisualEffect());
        break;
    }
    case 28: {
        p = new CProjectileSummonVFX(CResRef("CFElemH"), IcewindCVisualEffect());
        break;
    }
    case 29: {
        p = new CProjectileSummonVFX(CResRef("CEElemH"), IcewindCVisualEffect());
        break;
    }
    case 30: {
        p = new CProjectileSummonVFX(CResRef("CWElemH"), IcewindCVisualEffect());
        break;
    }
    case 31: {
        p = new CProjectileSummonVFX(CResRef("BlessH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P31");
        break;
    }
    case 32: {
        p = new CProjectileSummonVFX(CResRef("CurseH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P32");
        break;
    }
    case 33: {
        p = new CProjectileSummonVFX(CResRef("PrayerH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P31");
        break;
    }
    case 34: {
        p = new CProjectileSummonVFX(CResRef("RecitaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P44");
        break;
    }
    case 35: {
        p = new CProjectileSummonVFX(CResRef("CLWounH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P26");
        break;
    }
    case 36: {
        p = new CProjectileSummonVFX(CResRef("CMWounH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P26");
        break;
    }
    case 37: {
        p = new CProjectileSummonVFX(CResRef("CSWounH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P34");
        break;
    }
    case 38: {
        p = new CProjectileSummonVFX(CResRef("CCWounH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P46");
        break;
    }
    case 39: {
        p = new CProjectileSummonVFX(CResRef("HealH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P37");
        break;
    }
    case 40: {
        p = new CProjectileSummonVFX(CResRef("ASumm1H"), IcewindCVisualEffect());
        break;
    }
    case 41: {
        p = new CProjectileSummonVFX(CResRef("ASumm2H"), IcewindCVisualEffect());
        break;
    }
    case 42: {
        p = new CProjectileSummonVFX(CResRef("ASumm3H"), IcewindCVisualEffect());
        break;
    }
    case 43: {
        p = new CProjectileSummonVFX(CResRef("SPoisoH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P17");
        break;
    }
    case 44: {
        p = new CProjectileSummonVFX(CResRef("NPoisoH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P41");
        break;
    }
    case 45:  // CallLightning projectile (class not recovered)
        return NULL;
    case 46: {
        p = new CProjectileSummonVFX(CResRef("SChargH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P42");
        break;
    }
    case 47: {
        p = new CProjectileSummonVFX(CResRef("RParalH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P33");
        break;
    }
    case 48: {
        p = new CProjectileSummonVFX(CResRef("FActioH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P40");
        break;
    }
    case 49: {
        p = new CProjectileSummonVFX(CResRef("MMagicH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P13");
        break;
    }
    case 50: {
        p = new CProjectileSummonVFX(CResRef("SOOneH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P35");
        break;
    }
    case 51: {
        p = new CProjectileSummonVFX(CResRef("CStrenH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M41");
        break;
    }
    case 52:  // CallLightning projectile (class not recovered)
        return NULL;
    case 53: {
        p = new CProjectileSummonVFX(CResRef("RDeadH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P18");
        break;
    }
    case 54: {
        p = new CProjectileSummonVFX(CResRef("ResurrH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P18");
        break;
    }
    case 55: {
        p = new CProjectileSummonVFX(CResRef("CCommaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x80, 0x00, 0x80, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        p->m_fireSoundRef = CResRef("EFF_P47");
        break;
    }
    case 56: {
        p = new CProjectileSummonVFX(CResRef("RWOTFaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P36");
        break;
    }
    case 57:  // Sunray projectile (class not recovered)
        return NULL;
    case 58: {
        p = new CProjectileSummonVFX(CResRef("SStoneA"), IcewindCVisualEffect());
        p->m_fireSoundRef = CResRef("CRE_P03");
        break;
    }
    case 59: {
        p = new CProjectileSummonVFX(CResRef("DDoorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M09");
        break;
    }
    case 60: {
        p = new CProjectileSummonVFX(CResRef("DDoorH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M09");
        break;
    }
    case 61: {
        p = new CProjectileSummonVFX(CResRef("CoColdH"), IcewindCVisualEffect());
        break;
    }
    case 62: {
        p = new CProjectileSummonVFX(CResRef("SSOrbH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P38");
        break;
    }
    case 63: {
        p = new CProjectileSummonVFX(CResRef("FireH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        // Fire alone routes its glow through the message queue; the other
        // elemental hits below AddEffect directly.
        if (pCaster != NULL) {
            CMessage* message = new CMessageAddEffect(
                IcewindMisc::CreateEffectColorGlowDissipate(0xFF, 0x00, 0x00, 0x1E),
                pCaster->m_id,
                pCaster->m_id);
            g_pBaldurChitin->GetMessageHandler()->AddMessage(message, FALSE);
        }
        break;
    }
    case 64: {
        p = new CProjectileSummonVFX(CResRef("ColdH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x00, 0x00, 0xFF, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        break;
    }
    case 65: {
        p = new CProjectileSummonVFX(CResRef("ElectrH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0xFF, 0x4B, 0xFF, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        break;
    }
    case 66: {
        p = new CProjectileSummonVFX(CResRef("AcidH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x00, 0xFF, 0x00, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        break;
    }
    case 67: {
        p = new CProjectileSummonVFX(CResRef("ParalH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        if (pCaster != NULL) {
            pCaster->AddEffect(IcewindMisc::CreateEffectColorGlowDissipate(0x2D, 0x00, 0x5A, 0x1E),
                CGameAIBase::EFFECT_LIST_TIMED, TRUE, TRUE);
        }
        break;
    }
    case 68: {
        p = new CProjectileSummonVFX(CResRef("MRageH"), IcewindCVisualEffect());
        break;
    }
    case 69: {
        p = new CProjectileSummonVFX(CResRef("RWOTFaG"), IcewindCVisualEffect());
        break;
    }
    case 70: {
        p = new CProjectileSummonVFX(CResRef("BDeath"), IcewindCVisualEffect());
        break;
    }
    case 71: {
        p = new CProjectileSummonVFX(CResRef("PortalH"), IcewindCVisualEffect());
        break;
    }
    case 72:  // CallLightning projectile (class not recovered)
        return NULL;
    case 73: {
        p = new CProjectileSummonVFX(CResRef("BBarrH1"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->m_fireSoundRef = CResRef("ARE_P20");
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 74: {
        p = new CProjectileSummonVFX(CResRef("BBarrH2"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->SetOffsetAboveTarget(TRUE);
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 75: {
        p = new CProjectileSummonVFX(CResRef("CoBonH1"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->m_fireSoundRef = CResRef("ARE_P21");
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 76: {
        p = new CProjectileSummonVFX(CResRef("CoBonH2"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->SetOffsetAboveTarget(TRUE);
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 77: {
        p = new CProjectileSummonVFX(CResRef("CLDamaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 78: {
        p = new CProjectileSummonVFX(CResRef("CMDamaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 79: {
        p = new CProjectileSummonVFX(CResRef("CSDamaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 80: {
        p = new CProjectileSummonVFX(CResRef("CCDamaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 81: {
        p = new CProjectileSummonVFX(CResRef("CDiseaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P108");
        break;
    }
    case 82: {
        p = new CProjectileSummonVFX(CResRef("PoisonH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P108");
        break;
    }
    case 83: {
        p = new CProjectileSummonVFX(CResRef("SLivinH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P109");
        break;
    }
    case 84: {
        p = new CProjectileSummonVFX(CResRef("HarmH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P103");
        break;
    }
    case 85: {
        p = new CProjectileSummonVFX(CResRef("DestruH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P113");
        break;
    }
    case 86: {
        p = new CProjectileSummonVFX(CResRef("ExaltaH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P106");
        break;
    }
    case 87: {
        p = new CProjectileSummonVFX(CResRef("CloudbH"), IcewindCVisualEffect());
        break;
    }
    case 88: {
        p = new CProjectileSummonVFX(CResRef("MTouchH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P107");
        break;
    }
    case 89: {
        p = new CProjectileSummonVFX(CResRef("MTouchH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P107");
        break;
    }
    case 90: {
        p = new CProjectileSummonVFX(CResRef("CGraceH"), IcewindCVisualEffect());
        break;
    }
    case 91: {
        p = new CProjectileSummonVFX(CResRef("SEaterH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M104");
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 92: {
        p = new CProjectileSummonVFX(CResRef("SWaveH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_P110");
        break;
    }
    case 93: {
        p = new CProjectileSummonVFX(CResRef("SuffocA"), IcewindCVisualEffect());
        break;
    }
    case 94: {
        p = new CProjectileSummonVFX(CResRef("ADHWilH"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M105");
        break;
    }
    case 95: {
        p = new CProjectileSummonVFX(CResRef("MFMissX"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->m_fireSoundRef = CResRef("EFF_M103");
        break;
    }
    case 96: {
        p = new CProjectileSummonVFX(CResRef("VSpherX"), IcewindCVisualEffect());
        break;
    }
    case 97: {
        p = new CProjectileSummonVFX(CResRef("WVDeatH"), IcewindCVisualEffect());
        break;
    }
    case 98: {
        p = new CProjectileSummonVFX(CResRef("UWardX"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->SetOffsetAboveTarget(TRUE);
        // NOTE: extra aura-attach config (proj +0x2AF/+0x2B0/+0x2B4) omitted.
        break;
    }
    case 99: {
        p = new CProjectileSummonVFX(CResRef("WVHitH"), IcewindCVisualEffect());
        break;
    }
    case 100: {
        p = new CProjectileSummonVFX(CResRef("WDeath1"), IcewindCVisualEffect());
        break;
    }
    case 101: {
        p = new CProjectileSummonVFX(CResRef("WDeath2"), IcewindCVisualEffect());
        break;
    }
    case 102: {
        p = new CProjectileSummonVFX(CResRef("DDeath"), IcewindCVisualEffect());
        break;
    }
    case 103: {
        p = new CProjectileSummonVFX(CResRef("DDeath2"), IcewindCVisualEffect());
        break;
    }
    case 104: {
        p = new CProjectileSummonVFX(CResRef("MSumm1X"), IcewindCVisualEffect());
        break;
    }
    case 105: {
        p = new CProjectileSummonVFX(CResRef("ASumm1X"), IcewindCVisualEffect());
        break;
    }
    case 106: {
        p = new CProjectileSummonVFX(CResRef("CEElemX"), IcewindCVisualEffect());
        break;
    }
    case 107: {
        p = new CProjectileSummonVFX(CResRef("CFElemX"), IcewindCVisualEffect());
        break;
    }
    case 108: {
        p = new CProjectileSummonVFX(CResRef("CWElemX"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        p->SetOffsetAboveTarget(TRUE);
        break;
    }
    case 109: {
        p = new CProjectileSummonVFX(CResRef("GELoopX"), IcewindCVisualEffect());
        p->m_visualEffect.SetCopyFromBack(TRUE);
        break;
    }
    case 110: {
        p = new CProjectileSummonVFX(CResRef("DAttacH"), IcewindCVisualEffect());
        p->m_visualEffect.SetTintFromFlags(TRUE);
        p->m_fireSoundRef = CResRef("CRE_P01");
        break;
    }
    case 111: {
        p = new CProjectileSummonVFX(CResRef("WoMoonX"), IcewindCVisualEffect());
        break;
    }
    default:
        return NULL;
    }
    return p;
}

// 0x52AD60
//
// CProjectileTravelling -- shared base ctor for the travelling weapon/spell
// projectiles. Builds the heap CVidCell animation cell from the visual resref,
// a range palette and a bitmap, then seeds the travelling state. The leaf
// classes (CProjectileArrow etc.) prepare the resref and call this, then set
// their own vtable and per-projectile configuration.
//
// STEP 1: the CProjectile base sub-object is constructed by its (implicit)
// member ctor; this body initializes the travelling additions. The original
// also zeroes the motion-integrator work fields in the +0x9C..0xC0 "drift gap"
// (see the projectile-factory layout note) -- the subpixel position, per-tick
// step, carry and random-spread band that AimAtPoint reads each tick -- plus
// the +0xE6 render flags; these are modelled by-name on CProjectileTravelling
// and seeded here. The remaining unread gap defaults (+0xC4, +0xD0..0xDC) are
// omitted. m_driftDecay (the carry modulus) is left unwritten by the original; it
// is only consulted when a carry is non-zero, which the zeroed carries prevent,
// so it is zero-seeded here for definedness.
CProjectileTravelling::CProjectileTravelling(const CResRef& resRef)
    : m_palette(CVidPalette::TYPE_RANGE)
{
    m_mirrorMinX = 0;
    m_mirrorMinY = 0;
    m_distLifetime = 0;
    m_lifetime = 0x7FFF;

    m_pVidCell = new CVidCell(resRef, FALSE);

    m_pShadowCell = NULL;
    m_hasShadowCell = 0;
    m_mirror = 0;
    m_visible = 1;
    m_direction = 0;
    m_paletteSwap = 0;
    m_tinted = 0;
    m_useHeightOffset = 0;
    m_velocity = 0x14;
    m_renderFlags = 0x20000;
    // +0x1D8 is left unwritten by the original base ctor (a leaf sets it, e.g.
    // ARARROW = 0x10); default to 1 (non-directional) for definedness so Render
    // skips the directional-sequence pick until a leaf overrides it.
    m_dirCount = 1;

    // Motion-integrator state (AimAtPoint): subpixel position, step, carry and
    // random-spread band, all zero so the path starts straight from the launch.
    m_posAccumX = 0;
    m_posAccumY = 0;
    m_stepX = 0;
    m_stepY = 0;
    m_driftX = 0;
    m_driftY = 0;
    m_jitterMinX = 0;
    m_jitterMinY = 0;
    m_jitterMaxX = 0;
    m_jitterMaxY = 0;
    m_hasDrift = 0;
    m_driftDecay = 0;
    m_targetX = 0;
    m_targetY = 0;
    m_flightDistSq = 0;

    // Default projectile terrain-cost table (.data 0x8A8154): terrain types
    // 0, 10 and 13 are impassable, everything else costs 5.
    m_terrainTable[0] = CPathSearch::COST_IMPASSABLE;
    m_terrainTable[1] = 5;
    m_terrainTable[2] = 5;
    m_terrainTable[3] = 5;
    m_terrainTable[4] = 5;
    m_terrainTable[5] = 5;
    m_terrainTable[6] = 5;
    m_terrainTable[7] = 5;
    m_terrainTable[8] = 5;
    m_terrainTable[9] = 5;
    m_terrainTable[10] = CPathSearch::COST_IMPASSABLE;
    m_terrainTable[11] = 5;
    m_terrainTable[12] = 5;
    m_terrainTable[13] = CPathSearch::COST_IMPASSABLE;
    m_terrainTable[14] = 5;
    m_terrainTable[15] = 5;

    m_nSpellLevel = 0;
    m_sourceId = 0;
    m_targetId = 0;
    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x52B010 (vtable slot 0, partial)
//
// Frees the heap CVidCell; the embedded palette and bitmap destruct
// automatically. The full original destructor (which also releases the cell's
// requested resources) is recovered with the rest of the virtual interface.
CProjectileTravelling::~CProjectileTravelling()
{
    delete m_pVidCell;
}

// 0x52B900 (vtable slot 3 -- AIUpdate)
//
// Per-tick flight. Field semantics confirmed by a Frida trace of a Magic
// Missile cast on the original IWD2.exe: target (+0xC8/+0xCC) stays constant,
// the position (CGameObject m_pos) closes on it at ~velocity (+0x70) per tick,
// the lifetime (+0x29E) counts down from 0x7FFF, and the trail field (+0xE2)
// stays 0.
//
// PARTIAL: the pause-gate (skip while the engine single-steps another object),
// the moving-target homing branch (shares the live target and interpolates
// height from its animation), and the trailing sub-projectile (unrecovered
// factory 0x51AE40; branch disabled for Magic Missile) are documented stubs.
// The advance/arrival/expiry/lifetime/sound core is recovered and trace-verified
// (the per-tick aim step itself, 0x52BD20, is recovered -- see AimAtPoint).
void CProjectileTravelling::AIUpdate()
{
    m_pVidCell->FrameAdvance();

    // Arrival: target and position share the same 16-unit x and 12-unit y cell.
    int posX = m_pos.x;
    if (((m_targetX + ((m_targetX >> 31) & 0xF)) >> 4) == ((posX + ((posX >> 31) & 0xF)) >> 4)
        && m_targetY / 12 == m_pos.y / 12) {
        OnArrival();
        return;
    }

    // Arrival within the per-tick travel radius (velocity + 1); y weighted 16/9.
    int dy = m_targetY - m_pos.y;
    int dist = (dy * dy * 16) / 9 + (m_targetX - posX) * (m_targetX - posX);
    int radius = m_velocity + 1;
    if (dist <= radius * radius) {
        OnArrival();
        return;
    }
    if (m_flightDistSq == 0) {
        OnArrival();
        return;
    }

    // Lifetime countdown.
    SHORT life = m_lifetime;
    m_lifetime = life - 1;
    if (life == 0) {
        RemoveSelf();
        return;
    }

    if (m_targetId == CGameObjectArray::INVALID_INDEX) {
        AimAtPoint(m_targetX, m_targetY);
    } else {
        // Homing: the original shares the live target, aims at its position and
        // interpolates the projectile height from its animation. STUB: aim at
        // the recorded target point; live-target tracking and height interp are
        // recovered with the rest of the homing path.
        AimAtPoint(m_targetX, m_targetY);
    }

    // Trailing sub-projectile (+0xE2 != 0) via the unrecovered factory 0x51AE40
    // -- omitted (trace shows it disabled for Magic Missile).

    m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
}

// 0x52C050 (vtable slot 27 -- Fire; the launch)
//
// Ghidra recovers no function at the vtable target; transcribed from capstone
// disassembly (0x85E bytes) with the field semantics Frida-confirmed. Records
// the source/target/area, resolves the launch origin and the target point, and
// computes the flight distance and lifetime.
//
// PARTIAL: the trajectory setup (distance^2 + lifetime + target point), the
// area insertion (AddToArea), the subpixel-position seed and the initial facing
// are recovered -- the seed values are Frida-confirmed exact against the
// original (m_posAccumX = launch.x << 10, m_posAccumY = (launch.y << 12) / 3),
// and AddToArea's arguments are Frida-confirmed (pNewArea == the pArea arg, pos
// == the launch origin, listType 0). The remaining launch actions are documented
// stubs:
//   * the launch height (posZ): the original gates on m_bHasHeight and, for a
//     creature source, reads the source's current animation height (source
//     object's CGameAnimation at +0x50F0, GetHeight virtual; 0x20 for a
//     non-creature source) -- the animation-height stub shared with AIUpdate;
//     passed as 0 (ground) here.
//   * the attached-object create (CMessageHandler::AddMessage 0x4F7500 +
//     CMessage 0x554D20 -> m_nTargetId); not exercised by Magic Missile
//     (m_nTargetId stays INVALID).
// The launch sound (the inlined PlaySound at 0x52C6BA) is recovered below.
// The projectile registers itself in the global object array (CGameObjectArray::
// Add) to obtain an m_id before AddToArea, which adds that m_id to the area's
// object lists so the engine drives its AIUpdate/Render.
void CProjectileTravelling::Fire(CGameArea* pArea, LONG source, LONG target,
                                 CPoint targetPos, LONG nHeight, SHORT nType)
{
    (void)nHeight;
    (void)nType;

    m_sourceId = source;
    m_targetId = target;
    m_pArea = pArea;

    CPoint ptTarget;
    if (m_targetId != CGameObjectArray::INVALID_INDEX) {
        // Homing: aim at the live target object's current position.
        CGameObject* pTargetObj;
        BYTE rc;
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(m_targetId,
                CGameObjectArray::THREAD_ASYNCH, &pTargetObj, INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
        if (rc != CGameObjectArray::SUCCESS) {
            return;
        }
        ptTarget = pTargetObj->GetPos();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(m_targetId,
            CGameObjectArray::THREAD_ASYNCH, INFINITE);
    } else {
        ptTarget = targetPos;
    }

    // Launch origin from the source (facing-adjusted).
    CPoint ptSource;
    if (!GetProjectileSourcePosition(m_sourceId, ptSource)) {
        return;
    }

    // Flight distance^2 (y weighted 16/9) -- the metric AIUpdate tests for
    // arrival; drives the lifetime.
    int dx = ptTarget.x - ptSource.x;
    int dy = ptTarget.y - ptSource.y;
    m_flightDistSq = dx * dx + (dy * dy * 16) / 9;

    // Distance-based lifetime (only when m_distLifetime is set; Magic Missile leaves
    // it 0, keeping the 0x7FFF default).
    if (m_distLifetime != 0) {
        m_lifetime = static_cast<SHORT>(
            static_cast<int>(sqrt(static_cast<double>(m_flightDistSq))) / m_velocity + 1);
    }

    m_targetX = ptTarget.x;
    m_targetY = ptTarget.y;

    // Insert the projectile into the area so the engine drives its AIUpdate /
    // Render. CGameObject::AddToArea sets the base m_pos / m_posZ / area
    // membership and registers with the area's object lists. pArea and the
    // launch position (ptSource) are Frida-confirmed; posZ (the launch height)
    // is the documented animation-height stub, passed as 0.
    // Register in the global object array (assigns m_id), then add to the area.
    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE);
    AddToArea(pArea, ptSource, 0, 0);

    // Launch sound: the original inlines CProjectile::PlaySound's body here
    // (0x52C6BA), immediately after the area insertion -- it plays m_fireSoundRef
    // on the projectile's CSound at the launch position (m_pos, just set by
    // AddToArea), channel 15. A Magic Missile sub-missile carries "TRA_02" (the
    // travel whoosh); leaves with an empty fire-sound stay silent. The original
    // reads a per-projectile fire-sound loop flag at +0x15A (FALSE for the
    // recovered leaves), so the cue is one-shot here.
    PlaySound(m_fireSoundRef, FALSE, FALSE);

    // Subpixel launch position (1/1024 fixed point), seeded from the launch
    // origin. Frida-confirmed exact: X scaled << 10, Y also 4/3 y-scaled (the
    // iso squash) so AimAtPoint's decode reproduces m_pos. Without this the
    // flight would start from (0, 0).
    m_posAccumX = ptSource.x << 10;
    m_posAccumY = (ptSource.y << 12) / 3;

    // Initial facing toward the target, both points 4/3 y-scaled. The original
    // reads m_pos here -- which the preceding AddToArea set to this same launch
    // origin; AddToArea is not yet wired, so aim from ptSource directly.
    CPoint ptStartScaled;
    ptStartScaled.x = ptSource.x;
    ptStartScaled.y = (ptSource.y * 4) / 3;
    CPoint ptTargetScaled;
    ptTargetScaled.x = ptTarget.x;
    ptTargetScaled.y = (ptTarget.y * 4) / 3;
    m_facing = static_cast<SHORT>(CGameSprite::GetDirection(ptStartScaled, ptTargetScaled));
}

// 0x52BD20 (vtable slot 33 -- the per-tick motion integrator; AIUpdate's "aim")
//
// Ghidra recovers no function at the vtable target; transcribed from capstone
// disassembly (0x323 bytes, ret 8 -> __thiscall(this, int x, int y)). Steps the
// projectile one tick toward world point (x, y): computes the 16-direction
// facing, a velocity-scaled unit step in 1/1024 fixed point, accumulates it into
// the subpixel position, decodes that back to m_pos, and syncs an attached
// object's height.
//
// All work fields live in the CProjectile gap region in the binary
// (+0x9C..0xE0, +0x1DC) and are modelled by-name on CProjectileTravelling per
// the layout-drift note. The y axis is pre-scaled 4/3 to undo the isometric
// squash before the facing/distance maths (CGameSprite::GetDirection then
// applies its own iso ratios). The rand() term (_rand 0x7E8160) spreads the
// step within the [m_jitterMinX, m_jitterMaxX) band when set -- 0 for Magic Missile, so
// the path stays straight. Depends on Fire seeding the subpixel position
// (m_posAccumX/Y), which is currently stubbed.
void CProjectileTravelling::AimAtPoint(int x, int y)
{
    // Facing toward the target; both points y-scaled 4/3 (undo the iso squash).
    CPoint ptStart;
    ptStart.x = m_pos.x;
    ptStart.y = (m_pos.y * 4) / 3;
    CPoint ptTarget;
    ptTarget.x = x;
    ptTarget.y = (y * 4) / 3;
    m_facing = static_cast<SHORT>(CGameSprite::GetDirection(ptStart, ptTarget));

    // Velocity-scaled unit step (1/1024 fixed point).
    int dx = x - m_pos.x;
    int dyScaled = ptTarget.y - ptStart.y;
    int dist = static_cast<int>(
        sqrt(static_cast<double>(dx * dx + dyScaled * dyScaled)) + 0.5);
    if (dist == 0) {
        m_stepX = 1;
        m_stepY = 1;
    } else {
        m_stepX = ((dx << 10) * m_velocity) / dist;
        m_stepY = ((dyScaled << 10) * m_velocity) / dist;
        m_targetX = x;
        m_targetY = y;

        // Fold in the per-tick carry, then a random spread within the band.
        m_stepX += m_driftX;
        m_stepY += m_driftY;
        int spreadX = m_jitterMaxX - m_jitterMinX;
        if (spreadX > 0) {
            m_stepX += m_jitterMinX + rand() % spreadX;
        }
        int spreadY = m_jitterMaxY - m_jitterMinY;
        if (spreadY > 0) {
            m_stepY += m_jitterMinY + rand() % spreadY;
        }
    }

    // Advance the subpixel position by the step.
    m_posAccumX += m_stepX;
    m_posAccumY += m_stepY;

    // Bleed each carry down by one modulus per tick (zero once exhausted).
    if (m_driftX < 0) {
        m_driftX = (m_driftX <= -m_driftDecay) ? m_driftX + m_driftDecay : 0;
    } else if (m_driftX > 0) {
        m_driftX = (m_driftX >= m_driftDecay) ? m_driftX - m_driftDecay : 0;
    }
    if (m_driftY < 0) {
        m_driftY = (m_driftY <= -m_driftDecay) ? m_driftY + m_driftDecay : 0;
    } else if (m_driftY > 0) {
        m_driftY = (m_driftY >= m_driftDecay) ? m_driftY - m_driftDecay : 0;
    }

    // Decode the subpixel position back to the cell position (undo the 1/1024
    // fixed point and, for y, the 4/3 iso scale).
    m_pos.x = m_posAccumX >> 10;
    m_pos.y = ((m_posAccumY * 3) / 4) >> 10;

    // Keep the attached object (m_nTargetId) at the projectile's height.
    if (m_nTargetId != CGameObjectArray::INVALID_INDEX) {
        CGameObject* pObj;
        BYTE rc;
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(m_nTargetId,
                CGameObjectArray::THREAD_ASYNCH, &pObj, INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
        if (rc != CGameObjectArray::SUCCESS) {
            return;
        }
        pObj->m_posZ = m_posZ;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(m_nTargetId,
            CGameObjectArray::THREAD_ASYNCH, INFINITE);
    }
}

// 0x5297D0 (vtable slot 32 -- base blit flags)
//
// Trivial getter: `mov eax, [ecx+0xe6]; ret`. Returns the render-flags field
// the ctor seeds to 0x20000. (A Magic Missile render trace observed 0x20008 --
// the extra 0x8 is OR'd into the field elsewhere on the launch path, not by
// this getter.)
DWORD CProjectileTravelling::GetRenderFlags()
{
    return m_renderFlags;
}

// 0x52C8C0
//
// Picks the cell's animation sequence from the current movement facing, for
// directional projectiles (arrows: m_dirCount == 16). The 16 facings fold onto
// 5 mirrored sequences (the mirror flag is applied separately in Render); an
// 8-direction cell folds onto 3. m_direction caches the last facing the main
// cell was sequenced to, so the sequence is only reset when the facing changes.
// pCell defaults to the main cell; callers pass the shadow cell to sequence it
// without disturbing m_direction.
void CProjectileTravelling::UpdateDirectionSequence(CVidCell* pCell)
{
    if (pCell == NULL) {
        pCell = m_pVidCell;
    }

    SHORT dirCount = m_dirCount;
    if (dirCount == 1) {
        return;
    }
    SHORT facing = m_facing;
    if (m_direction == facing) {
        return;
    }

    if (dirCount == 0x10) {
        switch (facing) {
        case 0: case 8:
            pCell->SequenceSet(0);
            break;
        case 1: case 7: case 9: case 0xF:
            pCell->SequenceSet(1);
            break;
        case 2: case 6: case 0xA: case 0xE:
            pCell->SequenceSet(2);
            break;
        case 3: case 5: case 0xB: case 0xD:
            pCell->SequenceSet(3);
            break;
        case 4: case 0xC:
            pCell->SequenceSet(4);
            break;
        default:
            UTIL_ASSERT(FALSE);
        }
    } else if (dirCount == 8) {
        switch ((facing / 2) * 2) {
        case 0: case 8:
            pCell->SequenceSet(0);
            break;
        case 2: case 6: case 0xA: case 0xE:
            pCell->SequenceSet(2);
            break;
        case 4: case 0xC:
            pCell->SequenceSet(4);
            break;
        default:
            UTIL_ASSERT(FALSE);
        }
    }

    if (pCell == m_pVidCell) {
        m_direction = facing;
    }
}

// 0x52B6B0
//
// Cell draw rect + reference point, mirror/shadow aware: the cell alone,
// the union with the ground shadow cell (the flying cell lifted by m_posZ),
// or the mirror-clamped bounds (flipped about the frame for the north-window
// facings).
void CProjectileTravelling::GetCellBounds(CRect& rBounds, CPoint& ptRef, CVidCell* pCell)
{
    CPoint center;
    CSize size;

    if (pCell == NULL) {
        pCell = m_pVidCell;
    }

    if (m_mirror == 0 && m_hasShadowCell == 0) {
        pCell->GetCurrentCenterPoint(ptRef, FALSE);
        pCell->GetCurrentFrameSize(size, FALSE);
        rBounds.SetRect(0, 0, size.cx, size.cy);
    }

    if (m_hasShadowCell != 0) {
        CPoint shadowCenter;

        pCell->GetCurrentCenterPoint(center, FALSE);
        center.y += m_posZ;
        m_pShadowCell->GetCurrentCenterPoint(shadowCenter, FALSE);

        ptRef.x = center.x;
        ptRef.y = center.y;
        if (ptRef.x < shadowCenter.x) {
            ptRef.x = shadowCenter.x;
        }
        if (ptRef.y < shadowCenter.y) {
            ptRef.y = shadowCenter.y;
        }

        pCell->GetCurrentFrameSize(size, FALSE);
        rBounds.SetRect(0,
            0,
            size.cx + (ptRef.x - center.x),
            size.cy + (ptRef.y - center.y));

        m_pShadowCell->GetCurrentFrameSize(size, FALSE);
        if (rBounds.right < size.cx + (ptRef.x - shadowCenter.x)) {
            rBounds.right = size.cx + (ptRef.x - shadowCenter.x);
        }
        if (rBounds.bottom < size.cy + (ptRef.y - shadowCenter.y)) {
            rBounds.bottom = size.cy + (ptRef.y - shadowCenter.y);
        }
    }

    if (m_mirror != 0) {
        pCell->GetCurrentCenterPoint(center, FALSE);
        pCell->GetCurrentFrameSize(size, FALSE);
        if (CGameSprite::DIR_W < m_direction && m_direction < CGameSprite::DIR_E) {
            center.y = size.cy - center.y;
        }
        center.y += m_posZ;
        int minX = m_mirrorMinX;
        int minY = m_mirrorMinY;
        ptRef.x = center.x;
        ptRef.y = center.y;
        if (ptRef.x < minX) ptRef.x = minX;
        if (ptRef.y < minY) ptRef.y = minY;
        rBounds.SetRect(0, 0, size.cx + (ptRef.x - center.x), size.cy + (ptRef.y - center.y));
        int extX = (ptRef.x - minX) + m_mirrorMinX * 2;
        int extY = m_mirrorMinY * 2 + (ptRef.y - minY);
        if (rBounds.right < extX) rBounds.right = extX;
        if (rBounds.bottom < extY) rBounds.bottom = extY;
    }
}

// 0x5324A0
//
// CProjectileArrow -- the canonical travelling arrow (DecodeProjectile types
// 0x2/0x5/0x6). Builds the "ARARROW" travelling base, then configures the arrow:
// the first animation sequence, a three-range palette recolour from the game
// master bitmap, a tinted 16-direction cell, and 5x the base velocity.
//
// The original also seeds the fire-sound resref (+0x152) and a +0x17E field;
// both are deferred -- the CResRef members default-construct empty and neither
// is read on the flight/render path. The arrow-specific impact overrides are
// deferred too (see the class comment).
CProjectileArrow::CProjectileArrow()
    : CProjectileTravelling(CResRef("ARARROW"))
{
    m_pVidCell->SequenceSet(0);

    m_palette.SetRange(5, 0x1B, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_palette.SetRange(4, 0x17, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_palette.SetRange(2, 0x2E, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_pVidCell->SetPalette(m_palette);

    m_tinted = 1;
    m_useHeightOffset = 0;
    m_mirror = 0;
    m_hasShadowCell = 0;
    m_dirCount = 0x10;
    m_velocity = static_cast<SHORT>(m_velocity * 5);

    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x531790
//
// CProjectileDart -- the thrown dart. Builds the "DART" travelling base, then
// configures a tinted unmirrored 16-direction missile at 3x the base
// velocity with an empty fire-sound resref.
CProjectileDart::CProjectileDart()
    : CProjectileTravelling(CResRef("DART"))
{
    m_pVidCell->SequenceSet(0);

    m_tinted = 1;
    m_useHeightOffset = 0;
    m_mirror = 0;
    m_hasShadowCell = 0;
    m_dirCount = 0x10;
    m_fireSoundRef = CResRef("");
    m_velocity = static_cast<SHORT>(m_velocity * 3);

    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    field_17E = "";
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x532720
//
// CProjectileSPFLMARR -- the generic flame-trailed weapon missile. Builds the
// "SPFLMARR" travelling base, then configures it: first animation sequence, a
// two-range palette recolour (0x43/0x2E) from the game master bitmap, an
// untinted unmirrored 16-direction cell at 5x the base velocity, and the
// default 'A' trail colour ranges (the tinted DecodeProjectile cases restamp
// them).
//
// As with the other leaves, the empty fire-sound resref (+0x152) and +0x17E
// field the original seeds are deferred (both default-construct empty).
CProjectileSPFLMARR::CProjectileSPFLMARR()
    : CProjectileTravelling(CResRef("SPFLMARR"))
{
    m_pVidCell->SequenceSet(0);

    m_palette.SetRange(0, 0x43, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_palette.SetRange(1, 0x2E, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_pVidCell->SetPalette(m_palette);

    m_tinted = 0;
    m_useHeightOffset = 0;
    m_mirror = 0;
    m_hasShadowCell = 0;
    m_dirCount = 0x10;
    m_velocity = static_cast<SHORT>(m_velocity * 5);

    m_trailTick = 0;
    memset(m_trailColorRanges, 0x41, sizeof(m_trailColorRanges));

    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x5329A0
void CProjectileSPFLMARR::AIUpdate()
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->m_nTimeStop != 0 && pGame->m_nTimeStopCaster != m_id) {
        return;
    }

    CString sSoundName("");

    m_trailTick++;
    if (m_trailTick == 1) {
        m_trailTick = 0;

        int nJitterY = rand() % 5;
        int nJitterX = rand() % 5;
        new CGameTemporal(0x301,
            m_trailColorRanges,
            sSoundName,
            m_pArea,
            CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX * 4 / 3,
                ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY * 4 / 3),
            -m_posZ,
            CPoint(0, 0),
            0,
            0,
            CGameTemporal::COLLISION_DESTROY);

        nJitterY = rand() % 5;
        nJitterX = rand() % 5;
        new CGameTemporal(0x301,
            m_trailColorRanges,
            sSoundName,
            m_pArea,
            CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX,
                ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY),
            -m_posZ,
            CPoint(0, 0),
            0,
            0,
            CGameTemporal::COLLISION_DESTROY);

        nJitterY = rand() % 5;
        nJitterX = rand() % 5;
        new CGameTemporal(0x301,
            m_trailColorRanges,
            sSoundName,
            m_pArea,
            CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX * 2 / 3,
                ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY * 2 / 3),
            -m_posZ,
            CPoint(0, 0),
            0,
            0,
            CGameTemporal::COLLISION_DESTROY);
    }

    CProjectileTravelling::AIUpdate();
}

// 0x52CCE0
//
// CProjectileExploding -- the exploding weapon missile base. Builds the
// travelling base on the caller's cell, then arms the strike machinery: one
// strike at everyone of any type inside range 256, re-checked every 100 linger
// ticks. The two explosion cells stay empty here (the deferred leaf Fire and
// Explode paths load them).
CProjectileExploding::CProjectileExploding(const CResRef& resRef)
    : CProjectileTravelling(resRef)
    , m_targetType(0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0)
{
    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    field_17E = "";
    m_nTargetId = CGameObjectArray::INVALID_INDEX;

    m_childProjectileType = 0x4E;
    m_strikesLeft = 1;
    m_nState = 0;
    m_lingerPeriod = 100;
    m_lingerCountdown = 0;
    m_targetType.Set(CAIObjectType::ANYONE);
    m_strikeRange = 0x100;
    m_preCheckRange = 0x100;
    m_bPreScan = 0;
    m_bBurstPending = 0;
    m_tinted = 0;
    m_bExplodeCell1Active = 0;
    m_bExplodeCell2Active = 0;
    m_bCheckNonSprites = 0;
    m_burstType = 0xFF;
}

// 0x52DD60 (vtable slot 3)
//
// The base flying/linger tick -- the leaves override it with their
// flame-trail variants. Flying: advance the cell, re-aim at the recorded
// target point and track the sound. Lingering: animate the explosion cells,
// run the strike pass every m_lingerPeriod ticks, and while charges remain
// fire the pending delayed burst (one-shot) with the colour ranges matching
// m_burstType and a hold of lingerPeriod * strikesLeft.
void CProjectileExploding::AIUpdate()
{
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->m_nTimeStop != 0 && pGame->m_nTimeStopCaster != m_id) {
        return;
    }

    if (m_nState == 0) {
        m_pVidCell->FrameAdvance();

        LONG nDeltaX = m_targetX - m_pos.x;
        LONG nDeltaY = m_targetY - m_pos.y;
        LONG nRadius = m_velocity + 1;
        if ((nDeltaY * nDeltaY * 16) / 9 + nDeltaX * nDeltaX <= nRadius * nRadius) {
            OnArrival();
            return;
        }

        AimAtPoint(m_targetX, m_targetY);

        // Trailing sub-projectile (m_bSparkleTrail != 0) via the unrecovered
        // factory 0x51AE40 -- omitted like the same documented stub in
        // CProjectileTravelling::AIUpdate. (The original builds the 0xCA
        // object from m_sparkleColor and adds it to the area, LIST_FRONT.)

        m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
        return;
    }

    if (m_bExplodeCell1Active) {
        m_explodeCell1.FrameAdvance();
    }

    if (m_bExplodeCell2Active) {
        m_explodeCell2.FrameAdvance();
    }

    if (m_lingerCountdown != 0) {
        m_lingerCountdown = m_lingerCountdown - 1;
        return;
    }

    m_lingerCountdown = static_cast<SHORT>(m_lingerPeriod);
    if (AreaEffect(0) != 0) {
        m_strikesLeft = m_strikesLeft - 1;
    }

    if (m_strikesLeft < 1) {
        RemoveFromArea();
        if (g_pBaldurChitin->GetObjectGame()->m_cObjectArray.Delete(m_id,
                CGameObjectArray::THREAD_ASYNCH, NULL, INFINITE)
            == CGameObjectArray::SUCCESS) {
            delete this;
        }

        return;
    }

    if (m_bBurstPending == 0) {
        return;
    }

    BYTE colorRangeValues[7];
    switch (m_burstType) {
    case 0:
        memset(colorRangeValues, 0x43, sizeof(colorRangeValues));
        m_bBurstPending = 0;
        break;
    case 1:
        memset(colorRangeValues, 0x35, sizeof(colorRangeValues));
        m_bBurstPending = 0;
        break;
    case 2:
        memset(colorRangeValues, 0x31, sizeof(colorRangeValues));
        m_bBurstPending = 0;
        break;
    case 3:
        memset(colorRangeValues, 0x47, sizeof(colorRangeValues));
        m_bBurstPending = 0;
        break;
    case 4:
        memset(colorRangeValues, 0x42, sizeof(colorRangeValues));
        m_bBurstPending = 0;
        break;
    case 5:
        memset(colorRangeValues, 0x41, sizeof(colorRangeValues));
        m_bBurstPending = 0;
        break;
    case 0xFF:
        m_bBurstPending = 0;
        return;
    default:
        // __FILE__: C:\Projects\Icewind2\src\Baldur\CProjectile.cpp
        // __LINE__: 5675
        UTIL_ASSERT(FALSE);
    }

    new CGameFireball3d(m_burstType, colorRangeValues, m_pArea, m_pos, m_strikeRange,
        static_cast<BYTE>(m_velocity), CGameTemporal::COLLISION_DESTROY,
        static_cast<USHORT>(m_lingerPeriod * m_strikesLeft));
}

// 0x78E730 (vtable slot 34; COMDAT-folded with CProjectile::CallBack)
void CProjectileExploding::Explode()
{
}

// 0x52D430
//
// The strike pass (BG2 PDB: CProjectileArea::AreaEffect). Gathers every
// m_targetType creature in m_strikeRange (front and back area lists), fires a
// child projectile of type m_childProjectileType + 1 at each one that passes
// the range/LOS filter, cloning this missile's effect list onto the child,
// and triggers the Explode hook if anything went out. Returns whether the
// pre-scan found targets (always 1 when m_bPreScan is off).
int CProjectileExploding::AreaEffect(BYTE bCheckRange)
{
    CTypedPtrList<CPtrList, LONG*> targets;
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();

    BOOL bStruck = FALSE;
    int nResult;
    if (m_bPreScan == 0) {
        nResult = 1;
        bStruck = TRUE;
    } else {
        m_pArea->GetCloseObjects(m_posVertList, m_pos, m_targetType, m_preCheckRange,
            m_terrainTable, targets, TRUE, m_bCheckNonSprites);
        m_pArea->GetAllInRangeBack(m_pos, m_targetType, m_preCheckRange,
            m_terrainTable, targets, TRUE, FALSE, m_bCheckNonSprites);
        nResult = targets.GetCount() != 0;
        if (nResult == 0) {
            return nResult;
        }
    }

    targets.RemoveAll();
    m_pArea->GetCloseObjects(m_posVertList, m_pos, m_targetType, m_strikeRange,
        m_terrainTable, targets, TRUE, m_bCheckNonSprites);
    m_pArea->GetAllInRangeBack(m_pos, m_targetType, m_strikeRange,
        m_terrainTable, targets, TRUE, FALSE, m_bCheckNonSprites);

    POSITION pos = targets.GetHeadPosition();
    while (pos != NULL) {
        LONG nTargetId = reinterpret_cast<LONG>(targets.GetNext(pos));

        CGameObject* pObject;
        if (pGame->m_cObjectArray.GetShare(nTargetId, CGameObjectArray::THREAD_ASYNCH,
                &pObject, INFINITE)
            != CGameObjectArray::SUCCESS) {
            return nResult;
        }

        LONG nReleaseId = nTargetId;
        if (pObject->GetObjectType() & CGameObject::TYPE_AIBASE) {
            CGameSprite* pSprite = static_cast<CGameSprite*>(pObject);

            CPoint ptTarget;
            if (pObject->GetObjectType() == CGameObject::TYPE_SPRITE) {
                ptTarget = pSprite->m_posOld;
            } else {
                ptTarget = pObject->GetPos();
            }

            LONG nDeltaX = ptTarget.x - m_pos.x;
            LONG nDeltaY = ptTarget.y - m_pos.y;
            if (bCheckRange == 0
                || (nDeltaY * nDeltaY * 16) / 9 + nDeltaX * nDeltaX < m_strikeRange * m_strikeRange
                || m_pArea->CheckLOS(m_pos, ptTarget, m_terrainTable, FALSE)) {
                if (pObject->GetObjectType() == CGameObject::TYPE_SPRITE) {
                    pSprite->m_posOld = pObject->GetPos();
                }

                bStruck = TRUE;
                CProjectile* pChild = DecodeProjectile(
                    static_cast<USHORT>(m_childProjectileType + 1), NULL, 0);
                pChild->m_nSpellLevel = m_nSpellLevel;
                pChild->m_casterResRef = m_casterResRef;
                pChild->m_fireSoundRef = CResRef("");

                POSITION posEffect = m_effectList.GetHeadPosition();
                while (posEffect != NULL) {
                    CGameEffect* pCopy = m_effectList.GetNext(posEffect)->Copy();
                    pCopy->m_slotNum = pChild->m_projectileType;
                    pChild->m_effectList.AddTail(pCopy);
                }

                pChild->Fire(m_pArea, m_id, nTargetId, pObject->GetPos(), 0x32, 0);
            }

            // The original releases m_callBackProjectile here, not the target
            // it shared above -- the struck target's share is leaked.
            nReleaseId = m_callBackProjectile;
        }

        pGame->m_cObjectArray.ReleaseShare(nReleaseId, CGameObjectArray::THREAD_ASYNCH, INFINITE);
    }

    if (bStruck) {
        Explode();
    }

    return nResult;
}

// 0x52D9F0 (vtable slot 27 -- Fire; the launch)
//
// The exploding missile's launch. Shares the source for the whole call (spin
// on SHARED/DENIED like the source-position helper), resolves the
// facing-adjusted launch origin, registers in the object array and the area
// (launch height = the source sprite's cast height when m_bHasHeight, 0x20
// for a non-sprite source), plays the launch sound (the inlined
// CProjectile::PlaySound, one-shot unless m_loopFireSound), seeds the
// subpixel position and per-tick steps toward the target and the initial
// facing. A zero-distance launch arrives immediately.
void CProjectileExploding::Fire(CGameArea* pArea, LONG source, LONG target,
    CPoint targetPos, LONG nHeight, SHORT nType)
{
    (void)nHeight;
    (void)nType;

    m_sourceId = source;
    m_targetId = target;
    m_pArea = pArea;

    CGameObject* pSource;
    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(m_sourceId,
            CGameObjectArray::THREAD_ASYNCH, &pSource, INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }

    CPoint ptSource;
    GetProjectileSourcePosition(m_sourceId, ptSource);

    if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Add(&m_id, this, INFINITE)
        != CGameObjectArray::SUCCESS) {
        // The original frees itself first and reads the source id back off
        // the freed object for the release.
        LONG sourceId = m_sourceId;
        delete this;
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(sourceId,
            CGameObjectArray::THREAD_ASYNCH, INFINITE);
        return;
    }

    LONG nLaunchZ;
    if (m_bHasHeight) {
        if (pSource->GetObjectType() == CGameObject::TYPE_SPRITE) {
            nLaunchZ = static_cast<CGameSprite*>(pSource)->GetAnimation()->GetCastHeight();
        } else {
            nLaunchZ = 0x20;
        }
    } else {
        nLaunchZ = 0;
    }

    AddToArea(pArea, ptSource, nLaunchZ, CGameObject::LIST_FRONT);

    PlaySound(m_fireSoundRef, m_loopFireSound, FALSE);

    m_targetX = targetPos.x;
    m_targetY = targetPos.y;
    m_pos.x = ptSource.x;
    m_pos.y = ptSource.y;
    m_posAccumX = ptSource.x << CParticle::RESOLUTION_INC;
    m_posAccumY = ((ptSource.y << CParticle::RESOLUTION_INC) * 4) / 3;

    LONG nDeltaX = m_targetX - m_pos.x;
    LONG nDeltaY = (m_targetY * 4) / 3 - (m_pos.y * 4) / 3;
    LONG nDist = static_cast<LONG>(
        sqrt(static_cast<double>(nDeltaX * nDeltaX + nDeltaY * nDeltaY)) + 0.5);
    if (nDist == 0) {
        OnArrival();
    } else {
        m_stepX = (nDeltaX << CParticle::RESOLUTION_INC) * m_velocity / nDist;
        m_stepY = (nDeltaY << CParticle::RESOLUTION_INC) * m_velocity / nDist;

        CPoint ptStartScaled(m_pos.x, (m_pos.y * 4) / 3);
        CPoint ptTargetScaled(m_targetX, (m_targetY * 4) / 3);
        m_facing = CGameSprite::GetDirection(ptStartScaled, ptTargetScaled);
    }

    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(m_sourceId,
        CGameObjectArray::THREAD_ASYNCH, INFINITE);
}

// 0x52D7F0 (vtable slot 28 -- OnArrival)
//
// Arrival: notify the callback projectile (its CallBack virtual, under an
// exclusive deny grab), switch to the linger state so AIUpdate runs the
// strike pass, hide the missile and play the arrival sound (the inlined
// CProjectile::PlaySound, fire-and-forget).
void CProjectileExploding::OnArrival()
{
    if (m_callBackProjectile != CGameObjectArray::INVALID_INDEX) {
        CGameObject* pObject;
        BYTE rc;
        do {
            rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetDeny(
                m_callBackProjectile, CGameObjectArray::THREAD_ASYNCH, &pObject, INFINITE);
        } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
        if (rc != CGameObjectArray::SUCCESS) {
            return;
        }

        static_cast<CProjectile*>(pObject)->CallBack();
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseDeny(m_callBackProjectile,
            CGameObjectArray::THREAD_ASYNCH, INFINITE);
    }

    m_nState = 1;
    m_visible = 0;
    PlaySound(m_arrivalSoundRef, m_loopArrivalSound, TRUE);
}

// 0x52E9F0
//
// CProjectileExplodingFlame -- the exploding flame missile. Builds the
// "SPFLMARR" exploding base, then configures it like its plain sibling
// CProjectileSPFLMARR: first animation sequence, a two-range palette recolour
// (0x43/0x39, ranges 0 and 2) from the game master bitmap, an untinted
// unmirrored 16-direction cell at 3x the base velocity, an empty fire-sound
// resref, and the default 'A' trail colour ranges.
CProjectileExplodingFlame::CProjectileExplodingFlame()
    : CProjectileExploding(CResRef("SPFLMARR"))
{
    m_pVidCell->SequenceSet(0);

    m_palette.SetRange(0, 0x43, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_palette.SetRange(2, 0x39, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_pVidCell->SetPalette(m_palette);

    m_velocity = static_cast<SHORT>(m_velocity * 3);
    m_tinted = 0;
    m_useHeightOffset = 0;
    m_mirror = 0;
    m_hasShadowCell = 0;
    m_dirCount = 0x10;
    m_fireSoundRef = CResRef("");

    memset(m_trailColorRanges, 0x41, sizeof(m_trailColorRanges));
    m_trailTick = 0;
    m_childProjectileType = 0x4E;
}

// 0x52EC80
void CProjectileExplodingFlame::AIUpdate()
{
    CString sSoundName("");

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->m_nTimeStop == 0 || pGame->m_nTimeStopCaster == m_id) {
        if (m_nState == 0) {
            // Flying: advance the cell, chase the (live) target, drop the
            // flame trail.
            m_pVidCell->FrameAdvance();

            LONG nDeltaX = m_targetX - m_pos.x;
            LONG nDeltaY = m_targetY - m_pos.y;
            LONG nRadius = m_velocity + 1;
            if (nRadius * nRadius < (nDeltaY * nDeltaY * 16) / 9 + nDeltaX * nDeltaX) {
                if (m_targetId == CGameObjectArray::INVALID_INDEX) {
                    AimAtPoint(m_targetX, m_targetY);
                } else {
                    CGameObject* pTarget;
                    if (pGame->m_cObjectArray.GetShare(m_targetId,
                            CGameObjectArray::THREAD_ASYNCH, &pTarget, INFINITE)
                        != CGameObjectArray::SUCCESS) {
                        RemoveSelf();
                        return;
                    }

                    if (m_pArea != pTarget->m_pArea) {
                        LONG nTargetId = m_targetId;
                        RemoveSelf();
                        pGame->m_cObjectArray.ReleaseShare(nTargetId,
                            CGameObjectArray::THREAD_ASYNCH, INFINITE);
                        return;
                    }

                    CPoint ptTarget = pTarget->GetPos();
                    pGame->m_cObjectArray.ReleaseShare(m_targetId,
                        CGameObjectArray::THREAD_ASYNCH, INFINITE);
                    AimAtPoint(ptTarget.x, ptTarget.y);
                }

                m_trailTick = m_trailTick + 1;
                if (m_trailTick == 1) {
                    m_trailTick = 0;

                    int nJitterY = rand() % 5;
                    int nJitterX = rand() % 5;
                    new CGameTemporal(0x300,
                        m_trailColorRanges,
                        sSoundName,
                        m_pArea,
                        CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX * 4 / 3,
                            ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY * 4 / 3),
                        -m_posZ,
                        CPoint(0, 0),
                        0,
                        0,
                        CGameTemporal::COLLISION_DESTROY);

                    nJitterY = rand() % 5;
                    nJitterX = rand() % 5;
                    new CGameTemporal(0x300,
                        m_trailColorRanges,
                        sSoundName,
                        m_pArea,
                        CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX,
                            ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY),
                        -m_posZ,
                        CPoint(0, 0),
                        0,
                        0,
                        CGameTemporal::COLLISION_DESTROY);

                    nJitterY = rand() % 5;
                    nJitterX = rand() % 5;
                    new CGameTemporal(0x300,
                        m_trailColorRanges,
                        sSoundName,
                        m_pArea,
                        CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX * 2 / 3,
                            ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY * 2 / 3),
                        -m_posZ,
                        CPoint(0, 0),
                        0,
                        0,
                        CGameTemporal::COLLISION_DESTROY);
                }

                // Trailing sub-projectile (+0xE2 != 0) via the unrecovered
                // factory 0x51AE40 -- omitted like the same documented stub in
                // CProjectileTravelling::AIUpdate.

                m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
            } else {
                OnArrival();
            }
        } else {
            // Lingering after arrival: run the strike pass every
            // m_lingerPeriod ticks until the charges run out.
            if (m_lingerCountdown == 0) {
                m_lingerCountdown = static_cast<SHORT>(m_lingerPeriod);
                if (AreaEffect(0) != 0) {
                    m_strikesLeft = m_strikesLeft - 1;
                }

                if (m_strikesLeft < 1) {
                    RemoveFromArea();
                    if (pGame->m_cObjectArray.Delete(m_id, CGameObjectArray::THREAD_ASYNCH,
                            NULL, INFINITE)
                        == CGameObjectArray::SUCCESS) {
                        delete this;
                    }

                    return;
                }
            }

            m_lingerCountdown = m_lingerCountdown - 1;
        }
    }
}

// 0x52E230
//
// CProjectileExplodingWeapon -- the exploding thrown-weapon missile. Builds
// the "SPFIREBL" exploding base, then configures a mirrored 16-direction
// missile at 2x the base velocity (the MMissiT mirror geometry), a one-range
// palette recolour (0x43) from the game master bitmap, an empty fire-sound
// resref and the default 'A' trail colour ranges.
CProjectileExplodingWeapon::CProjectileExplodingWeapon()
    : CProjectileExploding(CResRef("SPFIREBL"))
{
    m_pVidCell->SequenceSet(0);

    m_palette.SetRange(0, 0x43, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_pVidCell->SetPalette(m_palette);

    m_mirrorMinX = 0xF;
    m_mirrorMinY = 0xB;
    m_explodeColorRange = 0x43;
    m_tinted = 0;
    m_useHeightOffset = 0;
    m_mirror = 1;
    m_leafRenderParam = 0x80;
    m_hasShadowCell = 0;
    m_dirCount = 0x10;
    m_velocity = static_cast<SHORT>(m_velocity << 1);
    m_fireSoundRef = CResRef("");

    memset(m_trailColorRanges, 0x41, sizeof(m_trailColorRanges));
    m_trailTick = 0;
    m_childProjectileType = 0x4E;
}

// 0x52E4D0
//
// Same tick as CProjectileExplodingFlame::AIUpdate minus the live-target
// homing: the missile only ever re-aims at the recorded target point.
void CProjectileExplodingWeapon::AIUpdate()
{
    CString sSoundName("");

    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->m_nTimeStop == 0 || pGame->m_nTimeStopCaster == m_id) {
        if (m_nState == 0) {
            m_pVidCell->FrameAdvance();

            LONG nDeltaX = m_targetX - m_pos.x;
            LONG nDeltaY = m_targetY - m_pos.y;
            LONG nRadius = m_velocity + 1;
            if (nRadius * nRadius < (nDeltaY * nDeltaY * 16) / 9 + nDeltaX * nDeltaX) {
                AimAtPoint(m_targetX, m_targetY);

                m_trailTick = m_trailTick + 1;
                if (m_trailTick == 1) {
                    m_trailTick = 0;

                    int nJitterY = rand() % 5;
                    int nJitterX = rand() % 5;
                    new CGameTemporal(0x300,
                        m_trailColorRanges,
                        sSoundName,
                        m_pArea,
                        CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX * 4 / 3,
                            ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY * 4 / 3),
                        -m_posZ,
                        CPoint(0, 0),
                        0,
                        0,
                        CGameTemporal::COLLISION_DESTROY);

                    nJitterY = rand() % 5;
                    nJitterX = rand() % 5;
                    new CGameTemporal(0x300,
                        m_trailColorRanges,
                        sSoundName,
                        m_pArea,
                        CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX,
                            ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY),
                        -m_posZ,
                        CPoint(0, 0),
                        0,
                        0,
                        CGameTemporal::COLLISION_DESTROY);

                    nJitterY = rand() % 5;
                    nJitterX = rand() % 5;
                    new CGameTemporal(0x300,
                        m_trailColorRanges,
                        sSoundName,
                        m_pArea,
                        CPoint(((m_pos.x + nJitterX - 2) << CGameSprite::EXACT_SCALE) - m_stepX * 2 / 3,
                            ((m_pos.y + nJitterY - 2) << CGameSprite::EXACT_SCALE) - m_stepY * 2 / 3),
                        -m_posZ,
                        CPoint(0, 0),
                        0,
                        0,
                        CGameTemporal::COLLISION_DESTROY);
                }

                // Trailing sub-projectile (+0xE2 != 0) via the unrecovered
                // factory 0x51AE40 -- omitted like the same documented stub in
                // CProjectileTravelling::AIUpdate.

                m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
            } else {
                OnArrival();
            }
        } else {
            if (m_lingerCountdown == 0) {
                m_lingerCountdown = static_cast<SHORT>(m_lingerPeriod);
                if (AreaEffect(0) != 0) {
                    m_strikesLeft = m_strikesLeft - 1;
                }

                if (m_strikesLeft < 1) {
                    RemoveFromArea();
                    if (pGame->m_cObjectArray.Delete(m_id, CGameObjectArray::THREAD_ASYNCH,
                            NULL, INFINITE)
                        == CGameObjectArray::SUCCESS) {
                        delete this;
                    }

                    return;
                }
            }

            m_lingerCountdown = m_lingerCountdown - 1;
        }
    }
}

// 0x52F1C0 (vtable slot 34)
//
// The flame missile's explosion: one fireball burst at the missile position,
// radius = the strike range, expansion speed = half the flight velocity,
// colour ranges hardwired to 0x43.
void CProjectileExplodingFlame::Explode()
{
    BYTE colorRangeValues[7];
    memset(colorRangeValues, 0x43, sizeof(colorRangeValues));
    new CGameFireball3d(CGameFireball3d::TYPE_FIREBALL, colorRangeValues, m_pArea, m_pos,
        m_strikeRange, static_cast<BYTE>(m_velocity) >> 1, CGameTemporal::COLLISION_DESTROY, 0);
}

// 0x52E940 (vtable slot 34)
//
// The weapon missile's explosion: the same fireball burst, coloured by the
// leaf's explosion colour range (the tinted DecodeProjectile cases restamp
// it).
void CProjectileExplodingWeapon::Explode()
{
    BYTE colorRangeValues[7];
    memset(colorRangeValues, m_explodeColorRange, sizeof(colorRangeValues));
    new CGameFireball3d(CGameFireball3d::TYPE_FIREBALL, colorRangeValues, m_pArea, m_pos,
        m_strikeRange, static_cast<BYTE>(m_velocity) >> 1, CGameTemporal::COLLISION_DESTROY, 0);
}

// 0x5300E0
//
// CProjectileStrike -- the invisible per-target strike bolt. Builds the
// "SPFIREBL" travelling base, then hides it and arms the distance-derived
// flight lifetime; the strike pass clones the parent's effect list onto it
// before firing, so it is a pure effect carrier.
CProjectileStrike::CProjectileStrike()
    : CProjectileTravelling(CResRef("SPFIREBL"))
{
    m_pVidCell->SequenceSet(0);

    m_palette.SetRange(0, 0x43, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
    m_pVidCell->SetPalette(m_palette);

    m_visible = 0;
    m_dirCount = 0x10;

    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    field_17E = "";
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
    m_distLifetime = 1;
}

// 0x57E030
//
// CProjectileMMissiT -- the Magic Missile homing sub-missile (DecodeProjectile
// type 0xDA). Builds the "MMissiT" travelling base, then configures a mirrored
// 16-direction-ish missile at double the base velocity. The factory constructs
// it with nPaletteFlag == 0, so the optional palette recolour is skipped.
//
// As with the other leaves, the fire-sound resref (+0x152) and the +0x17E field
// the original seeds are deferred (empty CResRef default, not read on the
// flight path).
CProjectileMMissiT::CProjectileMMissiT(SHORT nPaletteFlag)
    : CProjectileTravelling(CResRef("MMissiT"))
{
    m_pVidCell->SequenceSet(0);

    m_velocity = static_cast<SHORT>(m_velocity << 1);
    m_mirrorMinX = 0xF;
    m_mirrorMinY = 0xB;
    m_tinted = 0;
    m_useHeightOffset = 0;
    m_mirror = 1;
    m_leafRenderParam = 0x80;
    m_hasShadowCell = 0;
    m_renderFlags |= 8;

    if (nPaletteFlag != 0) {
        if (nPaletteFlag == 1) {
            m_palette.SetRange(0, 0x21, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
            m_pVidCell->SetPalette(m_palette);
        } else {
            UTIL_ASSERT(FALSE);
        }
    }

    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x52CA10
//
// CProjectileSparkle -- the single travelling spell missile, "SPMAGMIS" BAM
// (the Magic Missile bolt; the sparkle-stream factory cases swap the cell to
// "TRAVEL" afterwards). Same shape as CProjectileMMissiT -- a mirrored
// missile at double the base velocity with the optional TYPE_RANGE palette
// recolour -- plus an explicit clearing of the shadow-cell, direction and
// lifetime fields.
//
// As with the other leaves, the original's copies of an empty global CResRef
// into m_fireSoundRef (+0x152) and of an empty 8-byte global into field_17E
// are deferred -- both members default-construct empty.
CProjectileSparkle::CProjectileSparkle(SHORT nPaletteType)
    : CProjectileTravelling(CResRef("SPMAGMIS"))
{
    m_pShadowCell = NULL;
    m_dirCount = 0;
    m_direction = 0;
    m_facing = 0;
    m_visible = 0;
    m_paletteSwap = 0;
    m_distLifetime = 0;
    m_lifetime = 0;

    m_pVidCell->SequenceSet(0);

    m_velocity = static_cast<SHORT>(m_velocity << 1);
    m_mirrorMinX = 0xF;
    m_mirrorMinY = 0xB;
    m_tinted = 0;
    m_useHeightOffset = 0;
    m_mirror = 1;
    m_leafRenderParam = 0x80;
    m_hasShadowCell = 0;
    m_renderFlags |= 8;

    if (nPaletteType != 0) {
        if (nPaletteType == CVidPalette::TYPE_RANGE) {
            m_palette.SetRange(0, 0x21, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
            m_pVidCell->SetPalette(m_palette);
        } else {
            UTIL_ASSERT(FALSE);
        }
    }

    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x5295D0
//
// Replace the travelling animation cell built by the base constructor with
// another BAM (the sparkle cases swap "SPMAGMIS" for the "TRAVEL" dot).
void CProjectileTravelling::SetVidCell(CResRef resRef)
{
    if (m_pVidCell != NULL) {
        delete m_pVidCell;
    }
    m_pVidCell = new CVidCell(resRef, FALSE);
}

// 0x529660
//
// Request the sparkle colour-table bitmap ("STTRAVL1"; resource type 1 =
// bitmap) and arm the render-time palette swap (m_paletteSwap; Render picks
// the row given by m_sparkleColor). Cancels the previous request when the
// resref actually changes; an empty name is ignored. (Name is a #guess.)
void CProjectileTravelling::SetTravelPalette(CString bitmapName)
{
    if (bitmapName.GetLength() != 0) {
        CResRef resRef(bitmapName);
        if (resRef != m_travelPaletteRef) {
            if (m_pTravelPaletteRes != NULL
                && m_travelPaletteRef != CResRef("")
                && m_travelPaletteRequested) {
                m_pTravelPaletteRes->CancelRequest();
            }
            if (resRef == CResRef("")) {
                m_pTravelPaletteRes = NULL;
                m_travelPaletteRef = CResRef("");
            } else {
                CRes* pRes = g_pChitin->cDimm.GetResObject(resRef, 1, TRUE);
                if (pRes != NULL) {
                    m_pTravelPaletteRes = static_cast<CResBitmap*>(pRes);
                    m_travelPaletteRequested = TRUE;
                    pRes->Request();
                    m_travelPaletteRef = resRef;
                } else {
                    m_pTravelPaletteRes = NULL;
                    m_travelPaletteRef = CResRef("");
                }
            }
        }
        field_298 = 0;
        m_paletteSwap = 1;
    }
}

// 0x5309C0
//
// CProjectileMagicMissileMulti -- the Magic Missile launcher base. Builds the
// travelling base, then pre-spawns nCount sub-missiles (DecodeProjectile of
// nSubType + 1) into the sub-missile list; the launcher's Fire later drains them
// into the area. Each sub-missile is born with the "TRA_02" launch sound (stamped
// by its DecodeProjectile case); the original clears it on every sub-missile after
// the first (0x530ACB, assigning the empty default CResRef), so a volley plays a
// single launch whoosh rather than one per missile.
CProjectileMagicMissileMulti::CProjectileMagicMissileMulti(const CResRef& resRef,
    SHORT nCount, USHORT nSubType, BYTE nPaletteFlag)
    : CProjectileTravelling(resRef)
{
    m_pVidCell->SequenceSet(0);
    m_missileCount = nCount;
    m_subType = nSubType;
    m_renderFlags |= 8;

    if (nPaletteFlag != 0) {
        if (nPaletteFlag == 1) {
            m_palette.SetRange(0, 0x21, *g_pBaldurChitin->GetObjectGame()->GetMasterBitmap());
            m_pVidCell->SetPalette(m_palette);
        } else {
            UTIL_ASSERT(FALSE);
        }
    }

    for (SHORT i = 0; i < nCount; ++i) {
        CProjectile* pSub = CProjectile::DecodeProjectile(
            static_cast<USHORT>(m_subType + 1), NULL, 0);
        if (i != 0) {
            pSub->m_fireSoundRef = CResRef();
        }
        m_subMissiles.AddTail(pSub);
    }

    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x531120
//
// CProjectileSPMAGMIS -- the Magic Missile launcher leaf (DecodeProjectile types
// 0x44-0x48). nCount = the band's missile count (1..5); the sub-missiles are
// "MMissiT" (sub-type 0xD9 -> spawned type 0xDA).
CProjectileSPMAGMIS::CProjectileSPMAGMIS(SHORT nCount, SHORT nPaletteFlag)
    : CProjectileMagicMissileMulti(CResRef("SPMAGMIS"), nCount, 0xD9,
        static_cast<BYTE>(nPaletteFlag))
{
    m_tinted = 0;
    m_useHeightOffset = 0;
    m_hasShadowCell = 0;
    m_mirrorMinX = 10;
    m_mirrorMinY = 7;
    m_mirror = 1;
    m_leafRenderParam = 0x80;
    m_dirCount = 1;

    m_callBackProjectile = CGameObjectArray::INVALID_INDEX;
    m_nTargetId = CGameObjectArray::INVALID_INDEX;
}

// 0x530C90 (vtable slot 27 -- Fire; the multi-missile launch)
//
// Recovered from the ~344-instruction Ghidra-empty original (capstone disasm).
// Launches the pre-spawned MMissiT sub-missiles in a fan: it resolves the caster
// (source) and target positions, builds the launch direction (source - target,
// the y un-squashed by 4/3 to undo the iso projection), then walks the
// sub-missile list two at a time and gives each pair an equal-and-opposite
// perpendicular drift (the running spread index times the unit normal). The
// drift seeds the per-tick carry (m_driftX/m_driftY) which AimAtPoint folds into
// the homing step and bleeds off by m_driftDecay each tick, so the missiles splay
// out then curve back onto the shared target -- the classic Magic Missile
// spread. An odd man out flies straight.
//
// Per sub-missile it also clones the launcher's gameplay effects onto the
// missile's own list (so each missile carries the damage), copies the spell
// level/caster resref, and jitters the missile velocity by rand()%20 - 10 for a
// staggered arrival. It then drains the staging list and deletes itself (the
// launcher is never added to the area; CMessageFireProjectile::Run does not
// touch it after Fire, so the self-delete is safe).
//
// NOTE: the cloned effects only deliver once the sub-missile impact path
// (CProjectile::DeliverEffects, slot 0x78) is recovered; today they ride inert.
// The launcher's own m_nSpellLevel/m_casterResRef are not set on the cast path
// yet, so they propagate as the ctor defaults.
void CProjectileSPMAGMIS::Fire(CGameArea* pArea, LONG source, LONG target,
                               CPoint targetPos, LONG nHeight, SHORT nType)
{
    (void)nHeight;
    CGameObjectArray* pArray = g_pBaldurChitin->GetObjectGame()->GetObjectArray();
    BYTE rc;

    // Resolve the caster (source) position.
    CGameObject* pSource;
    do {
        rc = pArray->GetShare(source, CGameObjectArray::THREAD_ASYNCH, &pSource, INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }
    CPoint ptSource = pSource->GetPos();
    pArray->ReleaseShare(source, CGameObjectArray::THREAD_ASYNCH, INFINITE);

    // Resolve the target position.
    CGameObject* pTarget;
    do {
        rc = pArray->GetShare(target, CGameObjectArray::THREAD_ASYNCH, &pTarget, INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }
    CPoint ptTarget = pTarget->GetPos();
    pArray->ReleaseShare(target, CGameObjectArray::THREAD_ASYNCH, INFINITE);

    // Launch direction (source - target), the y un-squashed by 4/3.
    int dirX = ptSource.x - ptTarget.x;
    int dirY = (ptSource.y * 4) / 3 - (ptTarget.y * 4) / 3;
    int dist = static_cast<int>(
        sqrt(static_cast<double>(dirX * dirX + dirY * dirY)) + 0.5);

    int normX;
    int normY;
    if (dist == 0) {
        dist = 1;
        normX = 1;
        normY = 1;
    } else {
        normX = ((dirX << 10) * m_velocity) / dist;
        normY = ((dirY << 10) * m_velocity) / dist;
    }
    // Unit-normal length (truncated) and the running per-pair spread index.
    int normLen = static_cast<int>(sqrt(static_cast<double>(normX * normX + normY * normY)));
    int spreadIndex = 1;

    // Walk the sub-missiles two at a time, splaying each pair symmetrically.
    POSITION pos = m_subMissiles.GetHeadPosition();
    while (pos != NULL) {
        CProjectileTravelling* pA = static_cast<CProjectileTravelling*>(m_subMissiles.GetNext(pos));
        PrimeAndFireSubMissile(pA, pArea, source, target, targetPos, nType);

        CProjectileTravelling* pB = NULL;
        if (pos != NULL) {
            pB = static_cast<CProjectileTravelling*>(m_subMissiles.GetNext(pos));
            PrimeAndFireSubMissile(pB, pArea, source, target, targetPos, nType);
        }

        if (pB != NULL) {
            // Equal-and-opposite perpendicular drift, growing each pair.
            int offX = spreadIndex * normX;
            int offY = spreadIndex * normY;
            // Per-tick bleed-off of the lateral drift.  Binary 0x530C90 divides
            // by 40 (0x28); a larger divisor -> smaller decay -> the splay holds
            // longer before the missiles home in.  /10 bled the drift off ~4x too
            // fast, pulling the missiles back to centre early (a too-narrow fan).
            USHORT band = static_cast<USHORT>((spreadIndex * normLen) / 40);
            int hasOffset = (offX != 0 || offY != 0) ? 1 : 0;

            pA->m_driftX = offY;
            pA->m_driftY = -offX;
            pA->m_driftDecay = band;
            pA->m_hasDrift = hasOffset;

            pB->m_driftX = -offY;
            pB->m_driftY = offX;
            pB->m_driftDecay = band;
            pB->m_hasDrift = hasOffset;

            ++spreadIndex;
        } else {
            // Odd man out: no drift, flies straight to the target.
            pA->m_driftX = 0;
            pA->m_driftY = 0;
            pA->m_driftDecay = static_cast<USHORT>(m_velocity);
            pA->m_hasDrift = 0;
        }
    }

    m_subMissiles.RemoveAll();
    delete this;
}

// Per sub-missile launch prep shared by the pair and odd-man paths of
// CProjectileSPMAGMIS::Fire (inlined in the original at 0x530C90): clone the
// launcher's effects onto the missile, jitter its velocity, copy the caster
// fields, then fire it. The perpendicular drift is set by the caller afterwards
// (the original pokes the missile's carry fields after this returns).
void CProjectileSPMAGMIS::PrimeAndFireSubMissile(CProjectileTravelling* pMissile,
    CGameArea* pArea, LONG source, LONG target, CPoint targetPos, SHORT nType)
{
    for (POSITION ep = m_effectList.GetHeadPosition(); ep != NULL; ) {
        CGameEffect* pEffect = m_effectList.GetNext(ep);
        CGameEffect* pClone = pEffect->Copy();
        pClone->m_projectileType = pMissile->m_projectileType;
        pMissile->m_effectList.AddTail(pClone);
    }

    pMissile->m_velocity = static_cast<SHORT>(rand() % 20 + pMissile->m_velocity - 10);
    pMissile->m_nSpellLevel = m_nSpellLevel;
    pMissile->m_casterResRef = m_casterResRef;

    pMissile->Fire(pArea, source, target, targetPos, 0, nType);
}

// 0x52B190 (vtable slot 19 -- Render)
//
// Tile-based draw of a travelling projectile: visibility/passability gates,
// field-driven blit flags (mirror/tint/shadow), the directional-sequence
// pick, then the CInfinity FX pipeline. Facings past CGameSprite::DIR_N
// mirror the blit horizontally; the DIR_W..DIR_E window mirrors vertically
// and flips the draw rect. m_paletteSwap swaps the cell palette from the
// travel-palette bitmap around the main cell draw (sparkle streams).
void CProjectileTravelling::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    (void)pArea;

    // __FILE__: C:\Projects\Icewind2\src\Baldur\CProjectile.cpp
    // __LINE__: 4190
    UTIL_ASSERT(pVidMode != NULL);

    COLORREF tintColor = RGB(0xFF, 0xFF, 0xFF);

    DWORD flags = GetRenderFlags();
    if (m_mirror != 0) {
        flags |= 0x200;
    }

    // Tile-visibility gate (32x32 visibility tiles).
    LONG tileIndex = (m_pos.y / 32) * m_pArea->m_visibility.m_nWidth + (m_pos.x / 32);
    if (!m_pArea->m_visibility.IsTileVisible(tileIndex)) {
        return;
    }

    // Passability gate via the projectile terrain table (16x12 search cells).
    CPoint searchPos(m_pos.x / 16, m_pos.y / 12);
    if (m_pArea->m_search.GetMobileCost(searchPos, m_terrainTable, 3, TRUE) == CPathSearch::COST_IMPASSABLE) {
        return;
    }
    if (m_visible == 0) {
        return;
    }

    if (m_tinted != 0) {
        flags |= 0x10000;
    }
    if (m_hasShadowCell != 0) {
        flags |= 0x4;
    }

    // Directional projectiles (m_dirCount > 1, e.g. arrows): pick the animation
    // sequence matching the current facing before the cell is measured.
    if (m_dirCount > 1) {
        UpdateDirectionSequence(NULL);
    }

    // Clip to the area viewport. The original reads these as raw CGameArea
    // offsets (+0x514..0x560), but they live in the embedded CInfinity
    // (m_cInfinity at +0x4CC): the scroll origin (nCurrentX/Y) plus the screen
    // rect (rViewPort). Accessed by name so the layout drift does not misread
    // them (a raw +0x514 read gave an inverted viewport -> everything clipped).
    CInfinity* pInfinity = m_pArea->GetInfinity();
    CRect rViewport;
    rViewport.left = pInfinity->nCurrentX;
    rViewport.top = pInfinity->nCurrentY;
    rViewport.right = (pInfinity->rViewPort.right - pInfinity->rViewPort.left)
                      + pInfinity->nCurrentX;
    rViewport.bottom = (pInfinity->rViewPort.bottom - pInfinity->rViewPort.top)
                       + pInfinity->nCurrentY;

    CRect rFX;
    CPoint ptRef;
    GetCellBounds(rFX, ptRef, NULL);

    CPoint newPos;
    newPos.x = m_pos.x;
    LONG zOffset;
    if (m_mirror == 0 && m_hasShadowCell == 0) {
        newPos.y = m_pos.y - m_posZ;
        zOffset = 0;
    } else {
        newPos.y = m_pos.y;
        zOffset = m_posZ;
    }
    if (m_useHeightOffset != 0) {
        newPos.y += m_pArea->GetHeightOffset(m_pos, m_listType);
    }

    CRect rGCBounds(0, 0, 0, 0);
    if (CGameSprite::DIR_N < m_direction) {
        rGCBounds.left = (ptRef.x - rFX.right) + newPos.x + rFX.left;
    } else {
        rGCBounds.left = newPos.x - ptRef.x;
    }
    if (CGameSprite::DIR_W < m_direction && m_direction < CGameSprite::DIR_E) {
        if (m_mirror == 0 && m_hasShadowCell == 0) {
            rGCBounds.top = (rFX.top - rFX.bottom) + ptRef.y + newPos.y;
        } else {
            ptRef.y = (rFX.bottom - rFX.top) - ptRef.y;
            rGCBounds.top = (newPos.y - m_posZ) - ptRef.y;
        }
        zOffset = -zOffset;
    } else {
        rGCBounds.top = newPos.y - ptRef.y;
    }
    rGCBounds.right = (rFX.right - rFX.left) + rGCBounds.left;
    rGCBounds.bottom = (rFX.bottom - rFX.top) + rGCBounds.top;

    if (!IntersectRect(&rViewport, &rGCBounds, &rViewport)) {
        return;
    }

    if (m_tinted != 0 || m_mirror != 0) {
        tintColor = m_pArea->GetTintColor(newPos, m_listType);
    }

    if (CGameSprite::DIR_N < m_direction) {
        flags |= CInfinity::MIRROR_FX;
    }
    if (CGameSprite::DIR_W < m_direction && m_direction < CGameSprite::DIR_E) {
        flags |= CInfinity::MIRROR_FX_UPDOWN;
    }
    flags |= CInfinity::FXPREP_COPYFROMBACK;

    pInfinity->FXPrep(rFX, flags, nSurface, newPos, ptRef);
    if (pInfinity->FXLock(rFX, flags)) {
        if (m_tinted != 0) {
            m_pVidCell->SetTintColor(tintColor);
        }
        if (m_hasShadowCell != 0) {
            pInfinity->FXRender(m_pShadowCell, ptRef.x, ptRef.y, flags, 0);
        }
        if (m_paletteSwap == 0) {
            pInfinity->FXRender(m_pVidCell, ptRef.x, ptRef.y - zOffset, flags, 0x80);
        } else {
            m_pTravelPaletteRes->Demand();
            int nColorCount = m_pTravelPaletteRes->GetColorCount();
            RGBQUAD* pColorTable = m_pTravelPaletteRes->GetColorTable();
            m_pVidCell->SetPalette(pColorTable, nColorCount, CVidPalette::TYPE_RESOURCE);
            pInfinity->FXRender(m_pVidCell, ptRef.x, ptRef.y - zOffset, flags, 0x80);
            m_pTravelPaletteRes->Release();
        }
        pInfinity->FXRenderClippingPolys(newPos.x, newPos.y, 0, ptRef, rGCBounds, FALSE, flags);
        pInfinity->FXUnlock(flags, NULL, CPoint(0, 0));
        pInfinity->FXBltFrom(nSurface, rFX, newPos.x, newPos.y, ptRef.x, ptRef.y, flags);
    }
}

// -----------------------------------------------------------------------------

// 0x578110
//
// The shared CProjectileTravelling state (heap cell from the resref, range
// palette, velocity 0x14, lifetime 0x7FFF, render flags 0x20000, zeroed
// motion fields) is identical in the binary and comes from the base ctor;
// only the differences are written here. field_17E = "" transcribes the
// binary's explicit CString assignment of the nil string.
IcewindCProjectileTravellingVFX::IcewindCProjectileTravellingVFX(const CResRef& resRef)
    : CProjectileTravelling(resRef)
{
    // Default projectile terrain-cost table (.data 0x8A8154): terrain types
    // 0, 10 and 13 are impassable, everything else costs 5.
    m_terrainTable[0] = CPathSearch::COST_IMPASSABLE;
    m_terrainTable[1] = 5;
    m_terrainTable[2] = 5;
    m_terrainTable[3] = 5;
    m_terrainTable[4] = 5;
    m_terrainTable[5] = 5;
    m_terrainTable[6] = 5;
    m_terrainTable[7] = 5;
    m_terrainTable[8] = 5;
    m_terrainTable[9] = 5;
    m_terrainTable[10] = CPathSearch::COST_IMPASSABLE;
    m_terrainTable[11] = 5;
    m_terrainTable[12] = 5;
    m_terrainTable[13] = CPathSearch::COST_IMPASSABLE;
    m_terrainTable[14] = 5;
    m_terrainTable[15] = 5;
    field_17E = "";
    m_useHeightOffset = 1;
    m_bHasHeight = TRUE;
    m_bMirrorNorth = 1;
    m_bMirrorEast = 1;
}

// 0x578AB0 (vtable slot 3)
// The family's flight tick is a verbatim copy of CProjectileTravelling::
// AIUpdate (0x52B900): cell frame advance, 16x12-cell then radius
// (velocity+1, y weighted 16/9) arrival tests into the OnArrival virtual,
// lifetime countdown into RemoveSelf, AimAtPoint homing (point target when
// m_targetId is INVALID_INDEX), trailing sub-projectile, sound follow.
// Delegating shares that recovered body and its documented partial stubs
// (pause-gate, live-target height interpolation, trailing factory).
void IcewindCProjectileTravellingVFX::AIUpdate()
{
    CProjectileTravelling::AIUpdate();
}

// 0x5791D0 (vtable slot 27)
// UNIMPLEMENTED: the family's own launch; delegate to the recovered
// CProjectileTravelling launch until it is recovered.
void IcewindCProjectileTravellingVFX::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
    CProjectileTravelling::Fire(pArea, source, target, targetPos, nHeight, nType);
}

// 0x578ED0 (vtable slot 33)
// UNIMPLEMENTED: the family's own flight aiming; delegate to the recovered
// CProjectileTravelling integrator until it is recovered.
void IcewindCProjectileTravellingVFX::AimAtPoint(int x, int y)
{
    CProjectileTravelling::AimAtPoint(x, y);
}

// 0x578480 (vtable slot 19)
//
// The family draw: unlike CProjectileTravelling::Render the blit flags come
// from the attached visual effect (copy-from-back arms the translucent 0x200
// path), the area tint is applied when tinting or copy-from-back is on, and
// the east/north facings mirror through the m_bMirror* gates.
void IcewindCProjectileTravellingVFX::Render(CGameArea* pArea, CVidMode* pVidMode, int nSurface)
{
    (void)pArea;

    // __FILE__: C:\Projects\Icewind2\src\Baldur\IcewindCProjectileBAM.cpp
    // __LINE__: 140
    UTIL_ASSERT(pVidMode != NULL);

    DWORD flags = m_visualEffect.m_dwFlags;
    COLORREF tintColor = RGB(0xFF, 0xFF, 0xFF);

    // Tile-visibility gate (32x32 visibility tiles).
    LONG tileIndex = (m_pos.y / 32) * m_pArea->m_visibility.m_nWidth + (m_pos.x / 32);
    if (!m_pArea->m_visibility.IsTileVisible(tileIndex)) {
        return;
    }

    // Passability gate via the seeded terrain table (16x12 search cells).
    CPoint searchPos(m_pos.x / 16, m_pos.y / 12);
    if (m_pArea->m_search.GetMobileCost(searchPos, m_terrainTable, 3, TRUE) == CPathSearch::COST_IMPASSABLE) {
        return;
    }
    if (m_visible == 0) {
        return;
    }

    if (m_hasShadowCell != 0) {
        flags |= 0x4;
    }

    if (m_dirCount > 1) {
        UpdateDirectionSequence(NULL);
    }

    // Viewport from the area's embedded CInfinity, accessed by name (see
    // CProjectileTravelling::Render).
    CInfinity* pInfinity = m_pArea->GetInfinity();
    CRect rViewport;
    rViewport.left = pInfinity->nCurrentX;
    rViewport.top = pInfinity->nCurrentY;
    rViewport.right = (pInfinity->rViewPort.right - pInfinity->rViewPort.left)
                      + pInfinity->nCurrentX;
    rViewport.bottom = (pInfinity->rViewPort.bottom - pInfinity->rViewPort.top)
                       + pInfinity->nCurrentY;

    CRect rFX;
    CPoint ptRef;
    GetCellBounds(rFX, ptRef, NULL);

    CPoint newPos;
    newPos.x = m_pos.x;
    newPos.y = m_pos.y;
    LONG zOffset;
    if (m_hasShadowCell == 0) {
        newPos.y -= m_posZ;
        zOffset = 0;
    } else {
        zOffset = m_posZ;
    }
    if (m_useHeightOffset != 0 || m_bHasHeight) {
        newPos.y += m_pArea->GetHeightOffset(m_pos, m_listType);
    }

    CRect rGCBounds(0, 0, 0, 0);
    if (CGameSprite::DIR_N < m_direction) {
        rGCBounds.left = (ptRef.x - rFX.right) + newPos.x + rFX.left;
    } else {
        rGCBounds.left = newPos.x - ptRef.x;
    }
    if (CGameSprite::DIR_W < m_direction && m_direction < CGameSprite::DIR_E) {
        rGCBounds.top = newPos.y;
        if (m_hasShadowCell != 0) {
            ptRef.y = (rFX.bottom - rFX.top) - ptRef.y;
            rGCBounds.top = newPos.y - m_posZ;
        }
        rGCBounds.top -= ptRef.y;
        zOffset = -zOffset;
    } else {
        rGCBounds.top = newPos.y - ptRef.y;
    }
    rGCBounds.bottom = (rFX.bottom - rFX.top) + rGCBounds.top;
    rGCBounds.right = (rFX.right - rFX.left) + rGCBounds.left;

    if (!IntersectRect(&rViewport, &rGCBounds, &rViewport)) {
        return;
    }

    if (m_visualEffect.m_bTintEnabled == TRUE || m_visualEffect.m_bCopyFromBack == TRUE) {
        tintColor = m_pArea->GetTintColor(newPos, m_listType);
    }

    if (CGameSprite::DIR_N < m_direction && m_bMirrorEast == TRUE) {
        flags |= CInfinity::MIRROR_FX;
    }
    if (CGameSprite::DIR_W < m_direction && m_direction < CGameSprite::DIR_E
        && m_bMirrorNorth == TRUE) {
        flags ^= CInfinity::MIRROR_FX;
    }

    pInfinity->FXPrep(rFX, CInfinity::FXPREP_COPYFROMBACK | flags, nSurface, newPos, ptRef);
    if (pInfinity->FXLock(rFX, flags)) {
        if (m_visualEffect.m_bTintEnabled == TRUE) {
            m_pVidCell->SetTintColor(tintColor);
        }
        if (m_hasShadowCell != 0) {
            pInfinity->FXRender(m_pShadowCell, ptRef.x, ptRef.y, flags, 0);
        }
        if (m_paletteSwap == 0) {
            pInfinity->FXRender(m_pVidCell,
                ptRef.x,
                ptRef.y - zOffset,
                flags,
                m_visualEffect.m_nTransValue);
        } else {
            m_pTravelPaletteRes->Demand();
            int nColorCount = m_pTravelPaletteRes->GetColorCount();
            RGBQUAD* pColorTable = m_pTravelPaletteRes->GetColorTable();
            m_pVidCell->SetPalette(pColorTable, nColorCount, CVidPalette::TYPE_RESOURCE);
            pInfinity->FXRender(m_pVidCell,
                ptRef.x,
                ptRef.y - zOffset,
                flags,
                m_visualEffect.m_nTransValue);
            m_pTravelPaletteRes->Release();
        }
        pInfinity->FXRenderClippingPolys(newPos.x, newPos.y, 0, ptRef, rGCBounds, FALSE, flags);
        pInfinity->FXUnlock(flags, NULL, CPoint(0, 0));
        pInfinity->FXBltFrom(nSurface, rFX, newPos.x, newPos.y, ptRef.x, ptRef.y, flags);
    }
}

// 0x578970
//
// Bounding box and reference point for the draw: the cell alone, or, with a
// shadow cell, the union of the flying cell (lifted by m_posZ) and the
// ground shadow.
void IcewindCProjectileTravellingVFX::GetCellBounds(CRect& rBounds, CPoint& ptRef, CVidCell* pCell)
{
    CSize size;

    if (pCell == NULL) {
        pCell = m_pVidCell;
    }

    if (m_hasShadowCell == 0) {
        pCell->GetCurrentCenterPoint(ptRef, FALSE);
        pCell->GetCurrentFrameSize(size, FALSE);
        rBounds.SetRect(0, 0, size.cx, size.cy);
    }

    if (m_hasShadowCell != 0) {
        CPoint cellCenter;
        CPoint shadowCenter;

        pCell->GetCurrentCenterPoint(cellCenter, FALSE);
        cellCenter.y += m_posZ;
        m_pShadowCell->GetCurrentCenterPoint(shadowCenter, FALSE);

        ptRef.x = cellCenter.x;
        ptRef.y = cellCenter.y;
        if (ptRef.x < shadowCenter.x) {
            ptRef.x = shadowCenter.x;
        }
        if (ptRef.y < shadowCenter.y) {
            ptRef.y = shadowCenter.y;
        }

        pCell->GetCurrentFrameSize(size, FALSE);
        rBounds.SetRect(0,
            0,
            size.cx + (ptRef.x - cellCenter.x),
            size.cy + (ptRef.y - cellCenter.y));

        m_pShadowCell->GetCurrentFrameSize(size, FALSE);
        if (rBounds.right < size.cx + (ptRef.x - shadowCenter.x)) {
            rBounds.right = size.cx + (ptRef.x - shadowCenter.x);
        }
        if (rBounds.bottom < size.cy + (ptRef.y - shadowCenter.y)) {
            rBounds.bottom = size.cy + (ptRef.y - shadowCenter.y);
        }
    }
}

// 0x579860
//
// Facing -> animation sequence. Like CProjectileTravelling::
// UpdateDirectionSequence but mirror-aware: with m_bMirrorNorth the BAM has
// no north-half sequences (5..8), so those facings fold onto the south ones
// (3..0) and Render flips the blit vertically through the MIRROR_FX toggle.
void IcewindCProjectileTravellingVFX::UpdateDirectionSequence(CVidCell* pCell)
{
    if (pCell == NULL) {
        pCell = m_pVidCell;
    }

    SHORT dirCount = m_dirCount;
    if (dirCount == 1) {
        return;
    }
    SHORT facing = m_facing;
    if (m_direction == facing) {
        return;
    }

    if (dirCount == 0x10) {
        switch (facing) {
        case 4: case 0xC:
            pCell->SequenceSet(4);
            break;
        case 5: case 0xB:
            if (m_bMirrorNorth == 0) {
                pCell->SequenceSet(5);
            } else {
                pCell->SequenceSet(3);
            }
            break;
        case 3: case 0xD:
            pCell->SequenceSet(3);
            break;
        case 6: case 0xA:
            if (m_bMirrorNorth == 0) {
                pCell->SequenceSet(6);
            } else {
                pCell->SequenceSet(2);
            }
            break;
        case 2: case 0xE:
            pCell->SequenceSet(2);
            break;
        case 7: case 9:
            if (m_bMirrorNorth == 0) {
                pCell->SequenceSet(7);
            } else {
                pCell->SequenceSet(1);
            }
            break;
        case 1: case 0xF:
            pCell->SequenceSet(1);
            break;
        case 8:
            if (m_bMirrorNorth == 0) {
                pCell->SequenceSet(8);
            } else {
                pCell->SequenceSet(0);
            }
            break;
        case 0:
            pCell->SequenceSet(0);
            break;
        default:
            // __FILE__: C:\Projects\Icewind2\src\Baldur\IcewindCProjectileBAM.cpp
            // __LINE__: 907
            UTIL_ASSERT(FALSE);
        }
    } else if (dirCount == 8) {
        switch ((facing / 2) * 2) {
        case 0: case 8:
            pCell->SequenceSet(0);
            break;
        case 2: case 6: case 0xA: case 0xE:
            pCell->SequenceSet(2);
            break;
        case 4: case 0xC:
            pCell->SequenceSet(4);
            break;
        default:
            // __FILE__: C:\Projects\Icewind2\src\Baldur\IcewindCProjectileBAM.cpp
            // __LINE__: 942
            UTIL_ASSERT(FALSE);
        }
    } else if (dirCount == 2) {
        switch (facing) {
        case 0: case 4: case 8: case 0xC:
            pCell->SequenceSet(0);
            break;
        case 1: case 2: case 3: case 5: case 6: case 7:
        case 9: case 0xA: case 0xB: case 0xD: case 0xE: case 0xF:
            pCell->SequenceSet(1);
            break;
        default:
            // __FILE__: C:\Projects\Icewind2\src\Baldur\IcewindCProjectileBAM.cpp
            // __LINE__: 974
            UTIL_ASSERT(FALSE);
        }
    }

    if (pCell == m_pVidCell) {
        m_direction = m_facing;
    }
}

// -----------------------------------------------------------------------------

// 0x57F640
// The base ctor runs with "WhirlwX" and the strike tracker / leaf sound
// members construct implicitly; this body sets the leaf state, points the
// tracker at this projectile (the tornado: scan every 3rd service within
// radius 70, re-strike after 33 in-range passes, 8 strikes total, never the
// caster), turns height handling back off, and loops the EFF_P105 fire
// sound.
CProjectileWhirlwind::CProjectileWhirlwind()
    : IcewindCProjectileTravellingVFX(CResRef("WhirlwX"))
{
    m_nLifetime = 1000;
    m_nLegBudget = 1000;
    field_2B6 = 0;
    m_bFinishing = 0;
    m_wanderSeed = 0;
    m_dirCount = 1;
    m_velocity = 5;
    m_targetMap.m_pOwner = this;
    m_targetMap.m_maxStrikesTotal = 8;
    m_targetMap.m_nRange = 70;
    m_targetMap.m_strikeInterval = 33;
    m_targetMap.m_servicePeriod = 3;
    m_targetMap.m_bSkipSource = TRUE;
    m_visualEffect.SetCopyFromBack(TRUE);
    m_bHasHeight = FALSE;
    m_useHeightOffset = 0;
    m_fireSoundRef = "EFF_P105";
    m_loopFireSound = TRUE;
}

// 0x57F760 (vtable slot 0)
// The members (strike tracker map, leaf sound, heap cell through the base
// chain) destruct implicitly.
CProjectileWhirlwind::~CProjectileWhirlwind()
{
}

// 0x57F8D0 (vtable slot 3)
// The wander tick. The original opens with the engine single-step gate
// (game +0x4B40/+0x4B44: skip unless this object is the stepped one) --
// omitted like the same documented stub in CProjectileTravelling::AIUpdate.
void CProjectileWhirlwind::AIUpdate()
{
    // Dissipating: wait for the aftermath sound, then remove and free.
    if (m_bFinishing == 1) {
        if (m_loopSound.IsSoundPlaying()) {
            return;
        }

        RemoveFromArea();
        if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(
                m_id, CGameObjectArray::THREAD_ASYNCH, NULL, INFINITE)
            == CGameObjectArray::SUCCESS) {
            delete this;
        }
        return;
    }

    // Wander leg bookkeeping: out of budget picks the next wander point.
    if (m_nLegBudget < 1) {
        POINT nextPoint;
        PickWanderPoint(&nextPoint, FALSE);
        m_targetX = nextPoint.x;
        m_targetY = nextPoint.y;
        m_nLegBudget = rand() % 50 + 50;
    } else {
        m_nLegBudget--;
    }

    // Expired (or the strike tracker hit its total cap): go invisible, play
    // the aftermath sound and dissipate once it ends.
    if (m_nLifetime < 1 || m_targetMap.m_bDone == 1) {
        m_loopSound.SetResRef(CResRef("AFT_P23"), TRUE, TRUE);
        m_loopSound.SetFireForget(TRUE);
        m_loopSound.SetChannel(15, reinterpret_cast<DWORD>(m_pArea));
        m_loopSound.Play(m_pos.x, m_pos.y, 0, FALSE);
        m_bFinishing = 1;
        m_visible = 0;
        return;
    }
    m_nLifetime--;

    // Advance the subpixel integrator (y carries the 3/4 isometric scale).
    m_posAccumX += m_stepX;
    m_posAccumY += m_stepY;
    m_pos.x = m_posAccumX >> CGameSprite::EXACT_SCALE;
    m_pos.y = ((m_posAccumY * 3) / 4) >> CGameSprite::EXACT_SCALE;

    // Off the area: remove and free.
    if (m_pos.x < 0 || m_pos.y < 0
        || m_pArea->GetInfinity()->nAreaX <= m_pos.x
        || m_pArea->GetInfinity()->nAreaY <= m_pos.y) {
        RemoveFromArea();
        if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(
                m_id, CGameObjectArray::THREAD_ASYNCH, NULL, INFINITE)
            == CGameObjectArray::SUCCESS) {
            delete this;
        }
        return;
    }

    // Wall collision: reflect the step off impassable squares (terrain cost
    // table FF, or structure-height code 8), preferring to flip the dominant
    // axis last, and re-aim a few ticks ahead after any bounce.
    SHORT nTableIndex;
    CPoint gridPt(m_pos.x / CPathSearch::GRID_SQUARE_SIZEX, m_pos.y / CPathSearch::GRID_SQUARE_SIZEY);
    if (m_pArea->m_search.GetLOSCost(gridPt, m_terrainTable, nTableIndex, FALSE) == CPathSearch::COST_IMPASSABLE
        || nTableIndex == 8) {
        int stepX = m_stepX;
        int stepY = m_stepY;
        int absX = stepX < 0 ? -stepX : stepX;
        int absY = stepY < 0 ? -stepY : stepY;

        if (absX < absY) {
            // Would flipping y alone clear the square?
            gridPt.x = m_pos.x / CPathSearch::GRID_SQUARE_SIZEX;
            gridPt.y = ((((m_posAccumY - 2 * stepY) * 3) / 4) >> CGameSprite::EXACT_SCALE)
                / CPathSearch::GRID_SQUARE_SIZEY;
            if (m_pArea->m_search.GetLOSCost(gridPt, m_terrainTable, nTableIndex, FALSE) == CPathSearch::COST_IMPASSABLE
                || nTableIndex == 8) {
                // No: flip x first ...
                m_stepX = -stepX;
                m_posAccumX -= 2 * stepX;
                m_pos.x = m_posAccumX >> CGameSprite::EXACT_SCALE;
                gridPt.x = m_pos.x / CPathSearch::GRID_SQUARE_SIZEX;
                gridPt.y = m_pos.y / CPathSearch::GRID_SQUARE_SIZEY;
                if (m_pArea->m_search.GetLOSCost(gridPt, m_terrainTable, nTableIndex, FALSE) != CPathSearch::COST_IMPASSABLE
                    && nTableIndex != 8) {
                    goto reaim;
                }
                // ... and y as well.
                m_stepY = -stepY;
                m_posAccumY -= 2 * stepY;
                m_pos.y = ((m_posAccumY * 3) / 4) >> CGameSprite::EXACT_SCALE;
            } else {
                // Yes: flip y alone.
                m_stepY = -stepY;
                m_posAccumY -= 2 * stepY;
                m_pos.y = ((m_posAccumY * 3) / 4) >> CGameSprite::EXACT_SCALE;
            }
        } else {
            // Would flipping x alone clear the square?
            gridPt.x = ((m_posAccumX - 2 * stepX) >> CGameSprite::EXACT_SCALE)
                / CPathSearch::GRID_SQUARE_SIZEX;
            gridPt.y = m_pos.y / CPathSearch::GRID_SQUARE_SIZEY;
            if (m_pArea->m_search.GetLOSCost(gridPt, m_terrainTable, nTableIndex, FALSE) == CPathSearch::COST_IMPASSABLE
                || nTableIndex == 8) {
                // No: flip y first ...
                m_stepY = -stepY;
                m_posAccumY -= 2 * stepY;
                m_pos.y = ((m_posAccumY * 3) / 4) >> CGameSprite::EXACT_SCALE;
                gridPt.x = m_pos.x / CPathSearch::GRID_SQUARE_SIZEX;
                gridPt.y = m_pos.y / CPathSearch::GRID_SQUARE_SIZEY;
                if (m_pArea->m_search.GetLOSCost(gridPt, m_terrainTable, nTableIndex, FALSE) == CPathSearch::COST_IMPASSABLE) {
                    // ... and x as well.
                    m_posAccumX -= 2 * stepX;
                    m_stepX = -stepX;
                    m_pos.x = m_posAccumX >> CGameSprite::EXACT_SCALE;
                }
            } else {
                // Yes: flip x alone.
                m_posAccumX -= 2 * stepX;
                m_stepX = -stepX;
                m_pos.x = m_posAccumX >> CGameSprite::EXACT_SCALE;
            }
        }

    reaim:
        // Re-aim a few steps ahead along the reflected heading and start a
        // fresh leg.
        int aheadX = (m_posAccumX + 3 * m_stepX) >> CGameSprite::EXACT_SCALE;
        int aheadY = (((m_posAccumY + 3 * m_stepY) * 3) / 4) >> CGameSprite::EXACT_SCALE;
        m_facing = GetDirection(CPoint(aheadX, aheadY));
        m_targetX = aheadX;
        m_targetY = aheadY;
        m_nLegBudget = rand() % 50 + 50;
    }

    // Service the strike tracker, keep the looping sound on the tornado, and
    // start a new leg once the current wander point is reached (+/-32 x,
    // +/-24 y).
    m_targetMap.Service();
    m_loopSound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);
    if (m_targetX < m_pos.x + 0x20 && m_pos.x - 0x20 < m_targetX
        && m_targetY < m_pos.y + 0x18 && m_pos.y - 0x18 < m_targetY) {
        POINT nextPoint;
        PickWanderPoint(&nextPoint, FALSE);
        m_targetX = nextPoint.x;
        m_targetY = nextPoint.y;
        m_nLegBudget = rand() % 50 + 50;
    }

    IcewindCProjectileTravellingVFX::AIUpdate();
}

// 0x57FF80 (vtable slot 27)
// Launch: start the looping ambience that follows the tornado, then fire
// through the base with the target object dropped (the tornado wanders; it
// never homes on an object).
void CProjectileWhirlwind::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
    (void)target;

    m_loopSound.SetResRef(CResRef("ARE_P23"), TRUE, TRUE);
    m_loopSound.SetFireForget(TRUE);
    m_loopSound.SetChannel(15, reinterpret_cast<DWORD>(m_pArea));
    m_loopSound.Play(m_pos.x, m_pos.y, 0, FALSE);

    IcewindCProjectileTravellingVFX::Fire(pArea, source, CGameObjectArray::INVALID_INDEX, targetPos, nHeight, nType);
}

// 0x5847B0
// Free helper shared by the wander pickers: step `distance` units from `src`
// along the (dx, dy) heading -- the heading vector is rescaled to the
// requested length on the float path (sqrt + chop), zero heading copies the
// source point.
static POINT* StepAlongHeading(POINT* pResult, const POINT* pSrc, int dx, int dy, int distance)
{
    if (dx == 0 && dy == 0) {
        *pResult = *pSrc;
        return pResult;
    }

    double scale = sqrt(static_cast<double>(distance * distance)
        / static_cast<double>(dx * dx + dy * dy));
    pResult->x = static_cast<LONG>(dx * scale + pSrc->x);
    pResult->y = static_cast<LONG>(dy * scale + pSrc->y);
    return pResult;
}

// 0x5800E0
// Pick the next wander point ~100 units away. The random path reseeds the
// CRT generator from m_wanderSeed and stores the fresh roll back, so every
// pick advances the same deterministic chain on all machines (the seed is
// what CMessageFireProjectile +0x20 replicates); bReverseFacing instead
// turns the current heading around (+8 of 16 directions). The 16-entry step
// table is the isometric direction rose.
POINT* CProjectileWhirlwind::PickWanderPoint(POINT* pResult, BOOL bReverseFacing)
{
    int direction;
    if (bReverseFacing) {
        direction = (GetDirection(CPoint(m_targetX, m_targetY)) + 8) % 16;
    } else {
        srand(m_wanderSeed);
        m_wanderSeed = rand();
        direction = m_wanderSeed % 16;
    }

    int dx, dy;
    switch (direction) {
    case 0:
        dx = 20;
        dy = 0;
        break;
    case 1:
        dx = 25;
        dy = -8;
        break;
    case 2:
        dx = 19;
        dy = -14;
        break;
    case 3:
        dx = 10;
        dy = -18;
        break;
    case 4:
        dx = 0;
        dy = -27;
        break;
    case 5:
        dx = -10;
        dy = -18;
        break;
    case 6:
        dx = -19;
        dy = -14;
        break;
    case 7:
        dx = -25;
        dy = -8;
        break;
    case 8:
        dx = -20;
        dy = 0;
        break;
    case 9:
        dx = -25;
        dy = 8;
        break;
    case 10:
        dx = -19;
        dy = 14;
        break;
    case 11:
        dx = -10;
        dy = 18;
        break;
    case 12:
        dx = 0;
        dy = 27;
        break;
    case 13:
        dx = 10;
        dy = 18;
        break;
    case 14:
        dx = 19;
        dy = 14;
        break;
    case 15:
        dx = 25;
        dy = 8;
        break;
    default:
        // Unreachable: direction is always 0..15 (the binary's default arm
        // feeds the out-pointer value as the step).
        dx = 0;
        dy = 0;
        break;
    }

    POINT pos;
    pos.x = m_pos.x;
    pos.y = m_pos.y;
    return StepAlongHeading(pResult, &pos, dx, dy, 100);
}

// 0x580270 (vtable slot 28)
// A wander leg arrived: pick the next wander point and roll the next leg
// budget.
void CProjectileWhirlwind::OnArrival()
{
    POINT nextPoint;
    PickWanderPoint(&nextPoint, FALSE);
    m_targetX = nextPoint.x;
    m_targetY = nextPoint.y;
    m_nLegBudget = rand() % 50 + 50;
}

// 0x5806C0
// Build the line-beam over the family travelling-VFX base, then arm the strike
// tracker: it owns this projectile, services every 2 ticks and re-strikes each
// victim every 33 in-range passes (gather range and beam length are stamped per
// spell by DecodeProjectile).
CProjectileLightningBolt::CProjectileLightningBolt(const CResRef& resRef)
    : IcewindCProjectileTravellingVFX(resRef)
{
    m_beamRange = 400;
    m_targetMap.m_pOwner = this;
    m_targetMap.m_servicePeriod = 2;
    m_targetMap.m_strikeInterval = 33;
}

// 0x580770 (vtable slot 0)
CProjectileLightningBolt::~CProjectileLightningBolt()
{
}

// 0x5808A0 (vtable slot 3)
// Per-tick flight for the line beam: advance the subpixel integrator straight
// toward the (overshot) target, removing on a wall, the area edge or once the
// strike tracker has spent its cap, servicing the tracker each tick so it rakes
// everything it passes, and arriving when it reaches the end of the line.
void CProjectileLightningBolt::AIUpdate()
{
    // Frozen by Time Stop unless this is the freezing caster's own shot.
    CInfGame* pGame = g_pBaldurChitin->GetObjectGame();
    if (pGame->m_nTimeStop != 0 && pGame->m_nTimeStopCaster != m_id) {
        return;
    }

    // Strike tracker reached its total cap: remove and free.
    if (m_targetMap.m_bDone == 1) {
        RemoveFromArea();
        if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(
                m_id, CGameObjectArray::THREAD_ASYNCH, NULL, INFINITE)
            == CGameObjectArray::SUCCESS) {
            delete this;
        }
        return;
    }

    // Grid square before advancing (a wall check only fires on a crossing).
    CPoint oldGrid(m_pos.x / CPathSearch::GRID_SQUARE_SIZEX, m_pos.y / CPathSearch::GRID_SQUARE_SIZEY);

    // Advance the subpixel integrator (y carries the 3/4 isometric scale).
    m_posAccumX += m_stepX;
    m_posAccumY += m_stepY;
    m_pos.x = m_posAccumX >> CGameSprite::EXACT_SCALE;
    m_pos.y = ((m_posAccumY * 3) / 4) >> CGameSprite::EXACT_SCALE;

    // Off the area: remove and free.
    if (m_pos.x < 0 || m_pos.y < 0
        || m_pArea->GetInfinity()->nAreaX <= m_pos.x
        || m_pArea->GetInfinity()->nAreaY <= m_pos.y) {
        RemoveFromArea();
        if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(
                m_id, CGameObjectArray::THREAD_ASYNCH, NULL, INFINITE)
            == CGameObjectArray::SUCCESS) {
            delete this;
        }
        return;
    }

    // Into a fresh impassable square: the beam stops dead (no bounce). Remove.
    CPoint newGrid(m_pos.x / CPathSearch::GRID_SQUARE_SIZEX, m_pos.y / CPathSearch::GRID_SQUARE_SIZEY);
    if (oldGrid.x != newGrid.x || oldGrid.y != newGrid.y) {
        SHORT nTableIndex;
        if (m_pArea->m_search.GetLOSCost(newGrid, m_terrainTable, nTableIndex, FALSE)
            == CPathSearch::COST_IMPASSABLE) {
            RemoveFromArea();
            if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(
                    m_id, CGameObjectArray::THREAD_ASYNCH, NULL, INFINITE)
                == CGameObjectArray::SUCCESS) {
                delete this;
            }
            return;
        }
    }

    // Service the strike tracker and keep the loop sound on the beam head.
    m_targetMap.Service();
    m_sound.SetCoordinates(m_pos.x, m_pos.y, m_posZ);

    // Reached the line's end (+/-32 x, +/-24 y): deliver; else keep flying.
    if (m_targetX < m_pos.x + 0x20 && m_pos.x - 0x20 < m_targetX
        && m_targetY < m_pos.y + 0x18 && m_pos.y - 0x18 < m_targetY) {
        OnArrival();
    } else {
        IcewindCProjectileTravellingVFX::AIUpdate();
    }
}

// 0x580B30 (vtable slot 27)
// Push the picked target point out to m_beamRange past it along the caster->
// target heading, so the bolt overshoots its mark and rakes the full line, then
// launch through the base.
void CProjectileLightningBolt::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
    CGameObject* pSource;
    if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(
            source, CGameObjectArray::THREAD_ASYNCH,
            reinterpret_cast<CGameObject**>(&pSource), INFINITE)
        == CGameObjectArray::SUCCESS) {
        CPoint& casterPos = pSource->GetPos();
        CPoint endPoint = IcewindMisc::ScaleToCircle(targetPos,
            targetPos.x - casterPos.x, targetPos.y - casterPos.y, m_beamRange);
        g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(
            source, CGameObjectArray::THREAD_ASYNCH, INFINITE);
        IcewindCProjectileTravellingVFX::Fire(pArea, source, target, endPoint, nHeight, nType);
    }
}

// 0x579B40
// Builds the cone carrier: an invisible MMissiT cell plus the cone geometry
// constants (length 200, radii 350/25, the segment divisors 20/35/45, a 15-tick
// lifetime that pulses every 5 ticks). The cone's own BAM arrives as coneBam.
//
// The base subobject is built through the cone-specific cell-less emission at
// address 0x577E40 (per the family modelling licence), represented here as the
// ordinary base ctor, whose carrier cell is then swapped for MMissiT below.
CProjectileCone::CProjectileCone(const CResRef& cellBam, const CResRef& coneBam)
    : IcewindCProjectileTravellingVFX(cellBam)
{
    m_coneLength = 200;
    m_outerRadius = 0x15E;
    m_coneRadius = 0x19;
    field_2DE = coneBam.GetResRef()[0];

    // PARTIAL: the original copies coneBam into the refcounted m_pName buffer
    // here through an inlined string class (init 0x448D50, assign 0x44BC20) that
    // is not yet recovered. Left empty (dtor-safe); m_pName is only read once
    // the cone is rendered/pulsed, which is itself unrecovered.
    m_pName = NULL;
    m_nameLen = 0;
    m_nameCap = 0;

    field_2EE = 1;
    m_segmentStep = 0x14;
    field_306 = 0x23;
    field_30A = 0x2D;
    m_duration = 0xF;
    m_pulsePeriod = 5;
    field_316 = 0;
    field_317 = 1;
    m_segCount = 0;
    m_tickCount = 0;
    m_bFinishing = 0;
    m_nHeight = 0;
    m_nType = 0;
    m_bMirrorNorth = 0;
    m_bMirrorEast = 1;

    // Carrier cell: an empty cell name selects the invisible MMissiT carrier; a
    // real name uses that BAM. The cell the base ctor built from cellBam is
    // replaced either way (the binary's cell-less base build skips it).
    delete m_pVidCell;
    if (cellBam.GetResRef()[0] == '\0') {
        m_pVidCell = new CVidCell(CResRef("MMissiT"), FALSE);
        m_visible = 0;
    } else {
        m_pVidCell = new CVidCell(cellBam, FALSE);
        m_visible = 1;
    }
    m_bHasHeight = TRUE;
}

// 0x579DE0 (vtable slot 0; the deleting thunk 0x579DC0 wraps this)
// Frees the cone edge-point buffer and releases the refcounted name; the base
// destructor then tears down the carrier cell and visual effect.
CProjectileCone::~CProjectileCone()
{
    // m_edgePoints (std::vector) releases its buffer automatically (the original
    // frees it explicitly here). Release the refcounted cone-BAM name; the
    // inlined string class is not yet recovered and the ctor leaves m_pName NULL.
    m_pName = NULL;
    m_nameLen = 0;
    m_nameCap = 0;
}

// 0x579E50 (vtable slot 3)
// Cone lifetime tick: count up; once past m_duration a finishing cone removes
// itself and is freed; otherwise it pulses a fresh layer every m_pulsePeriod
// ticks. While not finishing it also runs the base flight tick.
void CProjectileCone::AIUpdate()
{
    m_tickCount++;
    if (m_duration < m_tickCount) {
        if (m_bFinishing == 1) {
            RemoveFromArea();
            if (g_pBaldurChitin->GetObjectGame()->GetObjectArray()->Delete(
                    m_id, CGameObjectArray::THREAD_ASYNCH, NULL, INFINITE)
                == CGameObjectArray::SUCCESS) {
                delete this;
            }
            return;
        }
    } else if (m_tickCount % m_pulsePeriod == 1) {
        Pulse();
    }
    if (m_bFinishing == 0) {
        IcewindCProjectileTravellingVFX::AIUpdate();
    }
}

// 0x579EF0 (vtable slot 27)
// Resolve the caster position, then build the cone's geometry from the caster
// toward the target point: the segment count, the four corner points and the
// arc-centre point. Two near corners sit +/- m_coneRadius perpendicular to the
// aim at the caster; the arc centre sits m_outerRadius along the aim; the two
// far corners sit +/- (m_coneRadius + m_coneLength/2) perpendicular at the
// (m_coneRadius + m_outerRadius) point along the aim. Each coordinate scales a
// direction component by radius/distance (truncated to LONG, like the original
// __ftol). Then launch the invisible carrier through the base.
void CProjectileCone::Fire(CGameArea* pArea, LONG source, LONG target, CPoint targetPos, LONG nHeight, SHORT nType)
{
    m_nHeight = nHeight;
    m_nType = nType;
    m_sourceId = source;
    m_pArea = pArea;

    // Caster must exist; GetProjectileSourcePosition re-shares it to read the
    // facing-adjusted launch point into m_casterPos.
    CGameObject* pCaster;
    BYTE rc;
    do {
        rc = g_pBaldurChitin->GetObjectGame()->GetObjectArray()->GetShare(m_sourceId,
            CGameObjectArray::THREAD_ASYNCH, &pCaster, INFINITE);
    } while (rc == CGameObjectArray::SHARED || rc == CGameObjectArray::DENIED);
    if (rc != CGameObjectArray::SUCCESS) {
        return;
    }

    GetProjectileSourcePosition(m_sourceId, m_casterPos);
    g_pBaldurChitin->GetObjectGame()->GetObjectArray()->ReleaseShare(m_sourceId,
        CGameObjectArray::THREAD_ASYNCH, INFINITE);

    m_segCount = (m_coneLength / m_segmentStep) / 2;

    int dx = targetPos.x - m_casterPos.x;
    int dy = targetPos.y - m_casterPos.y;
    double distSq = static_cast<double>(dx * dx + dy * dy);

    // Near corners: +/- m_coneRadius perpendicular to the aim, at the caster.
    double ratioNear = sqrt(static_cast<double>(m_coneRadius * m_coneRadius) / distSq);
    m_edge0.x = static_cast<LONG>(static_cast<double>(m_casterPos.x) + static_cast<double>(-dy) * ratioNear);
    m_edge0.y = static_cast<LONG>(static_cast<double>(m_casterPos.y) + static_cast<double>(dx) * ratioNear);
    m_edge1.x = static_cast<LONG>(static_cast<double>(m_casterPos.x) + static_cast<double>(dy) * ratioNear);
    m_edge1.y = static_cast<LONG>(static_cast<double>(m_casterPos.y) + static_cast<double>(-dx) * ratioNear);

    // Arc centre: m_outerRadius along the aim.
    double ratioCentre = sqrt(static_cast<double>(m_outerRadius * m_outerRadius) / distSq);
    m_center.x = static_cast<LONG>(static_cast<double>(m_casterPos.x) + static_cast<double>(dx) * ratioCentre);
    m_center.y = static_cast<LONG>(static_cast<double>(m_casterPos.y) + static_cast<double>(dy) * ratioCentre);

    // Far corners: from the (m_coneRadius + m_outerRadius) point along the aim,
    // +/- (m_coneRadius + m_coneLength/2) perpendicular.
    LONG farSum = m_coneRadius + m_outerRadius;
    double ratioFarBase = sqrt(static_cast<double>(farSum * farSum) / distSq);
    LONG farBaseX = static_cast<LONG>(static_cast<double>(m_casterPos.x) + static_cast<double>(dx) * ratioFarBase);
    LONG farBaseY = static_cast<LONG>(static_cast<double>(m_casterPos.y) + static_cast<double>(dy) * ratioFarBase);

    LONG farHalf = m_coneRadius + m_coneLength / 2;
    double ratioFar = sqrt(static_cast<double>(farHalf * farHalf) / distSq);
    m_edge2.x = static_cast<LONG>(static_cast<double>(farBaseX) + static_cast<double>(dy) * ratioFar);
    m_edge2.y = static_cast<LONG>(static_cast<double>(farBaseY) + static_cast<double>(-dx) * ratioFar);
    m_edge3.x = static_cast<LONG>(static_cast<double>(farBaseX) + static_cast<double>(-dy) * ratioFar);
    m_edge3.y = static_cast<LONG>(static_cast<double>(farBaseY) + static_cast<double>(dx) * ratioFar);

    // Fan the cone arc: sweep m_segCount segments either side of the centre,
    // each offset m_segmentStep*|i| perpendicular to the aim (the edge0 side for
    // i < 0, the edge1 side for i > 0), then append the centre point itself.
    for (int i = -m_segCount; i < m_segCount; ++i) {
        CPoint pt;
        if (i < 0) {
            int off = -(m_segmentStep * i);
            double ratio = sqrt(static_cast<double>(off * off) / distSq);
            pt.x = static_cast<LONG>(static_cast<double>(m_center.x) + static_cast<double>(-dy) * ratio);
            pt.y = static_cast<LONG>(static_cast<double>(m_center.y) + static_cast<double>(dx) * ratio);
        } else if (i == 0) {
            pt = m_center;
        } else {
            int off = m_segmentStep * i;
            double ratio = sqrt(static_cast<double>(off * off) / distSq);
            pt.x = static_cast<LONG>(static_cast<double>(m_center.x) + static_cast<double>(dy) * ratio);
            pt.y = static_cast<LONG>(static_cast<double>(m_center.y) + static_cast<double>(-dx) * ratio);
        }
        m_edgePoints.insert(m_edgePoints.end(), 1, pt);
    }
    m_edgePoints.insert(m_edgePoints.end(), 1, m_center);

    IcewindCProjectileTravellingVFX::Fire(pArea, source, target, targetPos, nHeight, nType);
}

// 0x57A530 (vtable slot 28)
// UNIMPLEMENTED: the cone's arrival handling. Left a no-op until recovered.
void CProjectileCone::OnArrival()
{
}

// 0x57A670 (vtable slot 30)
// UNIMPLEMENTED: the cone hit-test that delivers effects to everything inside
// the swept arc. Left a no-op until recovered.
void CProjectileCone::DeliverEffects()
{
}

// 0x57A970 (vtable slot 34, new virtual; the BG2 CProjectileConeOfCold::DoLayers)
// UNIMPLEMENTED: emits one cone layer/pulse (the per-tick cone paint + strike).
// Left a no-op until recovered.
void CProjectileCone::Pulse()
{
}

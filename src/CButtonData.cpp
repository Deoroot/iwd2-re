#include "CButtonData.h"

// 0x504630
CAbilityId::CAbilityId()
{
    m_itemType = -1;
    m_itemNum = -1;
    m_abilityNum = -1;
    m_res = "";
    m_targetType = 0;
    m_strDescription = -1;
    m_strTooltipName = -1;
    m_strTooltipDesc = -1;
    m_nClass = 0;
    m_bCanUse = 0;
    m_nTooltip = 0;
}

// 0x4B9270
CAbilityId& CAbilityId::operator=(const CAbilityId& other)
{
    m_itemType = other.m_itemType;
    m_itemNum = other.m_itemNum;
    m_abilityNum = other.m_abilityNum;
    m_res = other.m_res;
    m_targetType = other.m_targetType;
    m_strDescription = other.m_strDescription;
    m_strTooltipName = other.m_strTooltipName;
    m_strTooltipDesc = other.m_strTooltipDesc;
    m_nClass = other.m_nClass;
    m_bCanUse = other.m_bCanUse;
    m_nTooltip = other.m_nTooltip;
    return *this;
}

// -----------------------------------------------------------------------------

// 0x593F80
CButtonData::CButtonData()
{
    m_launcherIcon = "";
    m_icon = "";
    m_launcherName = -1;
    m_name = -1;
    m_count = 0;
    m_bDisabled = FALSE;
    m_bDisplayCount = TRUE;
}

// 0x504690
CButtonData& CButtonData::operator=(const CButtonData& other)
{
    m_icon = other.m_icon;
    m_name = other.m_name;
    m_launcherIcon = other.m_launcherIcon;
    m_launcherName = other.m_launcherName;
    m_abilityId = other.m_abilityId;
    m_count = other.m_count;
    m_bDisabled = other.m_bDisabled;
    m_bDisplayCount = other.m_bDisplayCount;
    return *this;
}

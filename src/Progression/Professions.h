// ============================================================================
// RPFramework - Progression / Métiers (GDD §38, phase 13, dette n°2)
// ============================================================================
#pragma once

#include "Security/Types.h"

#include <string>
#include <string_view>

namespace rpframework::progression
{
    using PlayerId = rpframework::security::PlayerId;

    struct XpResult
    {
        bool        ok = false;
        int         level = 1;
        int         previousLevel = 1;
        int         xp = 0;
        int         skillPointsGained = 0;
        std::string message;

        bool LeveledUp() const { return level > previousLevel; }
    };

    XpResult AddProfessionXp(PlayerId player, std::string_view professionId,
                             int amount, std::string_view reason);
    int GetProfessionLevel(PlayerId player, std::string_view professionId);
    int GetProfessionXp(PlayerId player, std::string_view professionId);
}

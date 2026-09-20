// ============================================================================
// RPFramework - Progression / Compétences (GDD §43, phase 17)
// Dépense de skillPoints, prérequis, recettes liées. Data-driven.
// ============================================================================
#pragma once

#include "Security/Types.h"

#include "json.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rpframework::progression
{
    using PlayerId = rpframework::security::PlayerId;

    struct SkillDef
    {
        std::string              id;
        std::string              name;
        std::string              profession;
        int                      cost = 1;
        int                      minLevel = 1;
        std::vector<std::string> requiredSkills;
        std::vector<std::string> unlockRecipes;
        std::string              effectId;
    };

    struct SkillResult
    {
        bool        ok = false;
        std::string message;
        int         remainingPoints = 0;
    };

    void LoadSkills();
    void LoadSkillsFromSection(const nlohmann::json* section);
    void ResetSkillsForTests();

    std::vector<SkillDef> ListSkills();
    std::vector<SkillDef> ListSkillsForProfession(std::string_view professionId);
    std::optional<SkillDef> GetSkill(std::string_view skillId);

    SkillResult UnlockSkill(PlayerId player, std::string_view skillId);
}

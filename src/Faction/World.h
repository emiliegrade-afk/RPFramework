// ============================================================================
// RPFramework - Monde : crimes (E2) et accès / hostilité (E3)
//
// Zéro lore baked-in. Le plugin répond ; le DevKit applique porte / aggro.
// Pas de tick C++. GDD §13.4.
// ============================================================================
#pragma once

#include "Security/Types.h"

#include "json.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rpframework::faction
{
    using PlayerId = rpframework::security::PlayerId;

    struct CrimeDef
    {
        std::string id;
        std::string faction;
        int         delta = 0;
        bool        requireWitness = true;
        std::string unseenProfession;
        int         unseenXp = 0;
    };

    struct LocationDef
    {
        std::string id;
        std::string faction;
        std::string minStanding;
        std::vector<std::string> deniedTiers;
    };

    struct CrimeResult
    {
        bool        ok = false;
        bool        reputationApplied = false;
        std::string message;
    };

    struct AccessResult
    {
        bool        allowed = false;
        std::string standingId;
        std::string message;
    };

    void Initialize();
    void Shutdown();
    void LoadFromConfig();
    void ResetForTests();
    void LoadCrimesFromSection(const nlohmann::json* section);
    void LoadLocationsFromSection(const nlohmann::json* section);

    CrimeResult ReportCrime(PlayerId player, std::string_view crimeId, bool witnessed);
    AccessResult CanEnter(PlayerId player, std::string_view locationId);
    bool IsHostileTo(PlayerId player, std::string_view factionId);
}

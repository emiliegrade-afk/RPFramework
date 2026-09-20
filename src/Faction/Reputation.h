// ============================================================================
// RPFramework - Faction / Reputation
//
// API de gestion de la réputation joueur <-> faction.
//
// La réputation est stockée dans `PlayerData.reputation` (map<faction_id, int>)
// qui survit aux reboots via PlayerStore. Toute mutation passe par
// ApplyReputationDelta (in-place) ou ModifyReputation / SetReputation
// (load / save / audit). Les quêtes utilisent l'in-place, comme
// economy::CreditInPlace.
//
// Standing (palier) et spillover (un saut) sont data-driven : voir
// config.reputation et Faction.relations. GDD §13.
// ============================================================================
#pragma once

#include "Faction/Definitions.h"
#include "Security/Types.h"  // PlayerId

#include "json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rpframework::data { struct PlayerData; }

namespace rpframework::faction
{
    using PlayerId = rpframework::security::PlayerId;

    struct ReputationChange
    {
        std::string factionId;
        int         before = 0;
        int         after = 0;
        int         delta = 0;
        bool        primary = true;
        std::string fromTier;
        std::string toTier;
    };

    struct ReputationResult
    {
        bool        ok = true;
        std::string message;
        std::vector<ReputationChange> changes;

        static ReputationResult Fail(std::string msg)
        {
            return {false, std::move(msg), {}};
        }
    };

    // Mutate `data` only. Pas de lock / save / audit.
    // `propagate` : un seul saut via Faction.relations (jamais récursif).
    ReputationResult ApplyReputationDelta(data::PlayerData& data,
                                          std::string_view factionId,
                                          int delta,
                                          bool propagate);

    void AuditReputationChanges(PlayerId player,
                                std::string_view reason,
                                const std::vector<ReputationChange>& changes);

    // Modifie la réputation d'un joueur avec une faction.
    // `delta` peut être négatif. Renvoie la nouvelle valeur après
    // modification (valeur primaire). Échoue (retourne la valeur actuelle
    // sans modifier) si le profil joueur est indisponible ou overflow.
    int ModifyReputation(PlayerId player,
                         std::string_view factionId,
                         int delta,
                         std::string_view reason = "");

    // Set absolu (utilisé par les actions scriptées, quêtes admin, etc.).
    // Pas de propagation. Renvoie la valeur après set, ou valeur actuelle
    // si pas de profil.
    int SetReputation(PlayerId player,
                      std::string_view factionId,
                      int value,
                      std::string_view reason = "");

    // Lecture. 0 si le joueur n'a jamais eu de reputation avec cette faction.
    int GetReputation(PlayerId player, std::string_view factionId);

    std::optional<Standing> ResolveStanding(int value);
    std::optional<Standing> GetStanding(PlayerId player, std::string_view factionId);
    std::optional<std::string> GetRelation(std::string_view from, std::string_view to);

    // Rang actuel d'un joueur dans une faction (le rang le plus élevé dont
    // le min_reputation est satisfait). Renvoie nullptr si le joueur n'est
    // pas dans la faction ou si la faction n'existe pas.
    struct CurrentRank
    {
        std::string rankId;
        std::string rankName;
        int         minReputation = 0;
        nlohmann::json benefits = nlohmann::json::object();
    };
    std::optional<CurrentRank> GetCurrentRank(PlayerId player, std::string_view factionId);
}

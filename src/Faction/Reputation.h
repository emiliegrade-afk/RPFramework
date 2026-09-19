// ============================================================================
// RPFramework - Faction / Reputation
//
// API de gestion de la réputation joueur <-> faction.
//
// La réputation est stockée dans `PlayerData.reputation` (map<faction_id, int>)
// qui survit aux reboots via PlayerStore. Phase 5 n'introduit pas de
// schéma de persistance supplémentaire.
//
// Toutes les mutations passent par cette API pour être auditées.
// ============================================================================
#pragma once

#include "Security/Types.h"  // PlayerId

#include "json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rpframework::faction
{
    using PlayerId = rpframework::security::PlayerId;

    // Modifie la réputation d'un joueur avec une faction.
    // `delta` peut être négatif. Renvoie la nouvelle valeur après
    // modification. Échoue (retourne la valeur actuelle sans modifier)
    // si le profil joueur est indisponible.
    int ModifyReputation(PlayerId player,
                         std::string_view factionId,
                         int delta,
                         std::string_view reason = "");

    // Set absolu (utilisé par les actions scriptées, quêtes, etc.).
    // Renvoie la valeur après set, ou valeur actuelle si pas de profil.
    int SetReputation(PlayerId player,
                      std::string_view factionId,
                      int value,
                      std::string_view reason = "");

    // Lecture. 0 si le joueur n'a jamais eu de reputation avec cette faction.
    int GetReputation(PlayerId player, std::string_view factionId);

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

// ============================================================================
// RPFramework - Faction / Join
//
// Fait adhérer ou quitter un joueur à une faction. Vérifie :
//   1. permission (Permissions::Check)
//   2. rate limit (RateLimiter::Allow)
//   3. conditions de la faction (Character::SelectionCondition, peut
//      dépendre de la race/profession/class/réputation du joueur)
//   4. pas déjà membre d'une autre faction (one-faction-at-a-time,
//      modèle simple Phase 5 ; un modèle multi-faction pourra être
//      ajouté Phase 5b si besoin)
//
//   5. factions autorisées (pas dans excluded_*)
//
// Le modèle Phase 5 est "one faction at a time" : un joueur ne peut
// appartenir qu'à une seule faction à la fois. PlayerData.faction est
// la chaîne unique (vide = aucune).
// ============================================================================
#pragma once

#include "Faction/Definitions.h"
#include "Data/PlayerData.h"
#include "Security/Types.h"

#include <optional>
#include <string>
#include <string_view>

namespace rpframework::faction
{
    // PlayerId vit dans rpframework::security ; on l'élève pour permettre
    // l'usage de `PlayerId` au niveau des signatures.
    using PlayerId = rpframework::security::PlayerId;

    enum class JoinStatus
    {
        Success,
        AlreadyInFaction,        // déjà dans CETTE faction
        AlreadyInAnotherFaction, // déjà dans une autre → must Leave d'abord
        UnknownFaction,
        ConditionNotMet,         // joinCondition non satisfaite
        RaceExcluded,            // race du joueur dans excludedRaces (etc.)
        ProfessionExcluded,
        ClassExcluded,
        RateLimited,
        PermissionDenied,
        PlayerDataUnavailable,
    };

    struct JoinResult
    {
        JoinStatus  status = JoinStatus::Success;
        std::string message;

        static JoinResult MakeSuccess(std::string msg = "ok")
        {
            return {JoinStatus::Success, std::move(msg)};
        }
        static JoinResult Make(JoinStatus s, std::string msg)
        {
            return {s, std::move(msg)};
        }
    };

    JoinResult Join (PlayerId player, const std::string& factionId);
    JoinResult Leave(PlayerId player);

    // Restrictions excluded_races / professions / classes. nullopt = OK.
    std::optional<JoinStatus> CheckRestrictions(const Faction& faction,
                                                const data::PlayerData& data);

    // Démarre les quêtes de faction et donne le journal (idempotent).
    // Appelé après Join et après une sélection de race qui impose une faction.
    void OnJoined(PlayerId player, const std::string& factionId);

    // Relivre les journaux `journal-pending:*` (login / catch-up).
    void RetryPendingJournals(PlayerId player);
}

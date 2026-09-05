// ============================================================================
// RPFramework - Character / Select
//
// API publique pour qu'un joueur choisisse sa race / métier / classe.
//
// Pipeline (GDD §16, §17, §18, §22) :
//   1. Vérifier la permission (Permissions::Check)
//   2. Vérifier le rate limit (RateLimiter::Allow)
//   3. Charger le PlayerData (LoadDetailed)
//   4. Vérifier que le champ n'est pas déjà rempli (race/profession/class
//      sont en général one-shot — un changement impose /reset)
//   5. Vérifier que la définition existe (Registry::GetRace/...)
//   6. Vérifier les conditions de sélection (SelectionCondition)
//   7. Modifier PlayerData, sauvegarder (PlayerStore::Save)
//   8. Tracer l'audit (AuditLog::Log "race.select" / "profession.select" /
//      "class.select")
//
// Tout refus (permission, rate limit, validation, etc.) écrit une entrée
// d'audit "denied" avec contexte.
// ============================================================================
#pragma once

#include "Security/Types.h"  // PlayerId

#include <string>
#include <string_view>

namespace rpframework::character
{
    using PlayerId = rpframework::security::PlayerId;

    struct SelectResult
    {
        enum class Status
        {
            Success,
            AlreadySet,            // le joueur a déjà choisi (race/prof/class one-shot)
            UnknownId,             // ID non trouvé dans le Registry
            ClassesDisabled,       // classes_enabled = false mais on tente SelectClass
            ConditionNotMet,       // SelectionCondition non satisfaite
            RateLimited,           // RateLimiter a refusé
            PermissionDenied,      // Permissions a refusé
            PlayerDataUnavailable, // LoadDetailed a renvoyé Corrupt / Unavailable
        };

        Status      status = Status::Success;
        std::string message;     // lisible, destinée aux logs / UI

        static SelectResult MakeSuccess(std::string msg = "ok")
        {
            return {Status::Success, std::move(msg)};
        }
        static SelectResult Make(Status s, std::string msg)
        {
            return {s, std::move(msg)};
        }
    };

    // Sélectionne la race. Le rate limit et la permission sont vérifiés.
    SelectResult SelectRace     (PlayerId player, const std::string& raceId);
    SelectResult SelectProfession(PlayerId player, const std::string& professionId);
    SelectResult SelectClass    (PlayerId player, const std::string& classId);

    // Admin / debug : vide les trois champs d'un joueur. Audité.
    // Renvoie true si quelque chose a été modifié.
    bool ResetSelections(PlayerId player);
}

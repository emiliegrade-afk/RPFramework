// ============================================================================
// RPFramework - Character / Select - implémentation
// ============================================================================
#include "Character/Select.h"

#include "Character/Registry.h"
#include "Data/PlayerStore.h"
#include "Core/Logger.h"

#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include "json.hpp"

#include <fmt/format.h>

namespace rpframework::character
{
    using PlayerId = rpframework::security::PlayerId;

    namespace
    {
        // Pipeline partagé entre Race / Profession / Class. La logique
        // est identique : on a juste un accesseur différent sur PlayerData,
        // une clé de permission différente, et un nom d'action différent.
        //
        // Le lambda `getCondition` retourne un SelectionCondition par
        // VALEUR (copié) plutôt qu'un pointeur : le `optional<Race>` de
        // Registry::GetRace() est local au lambda et serait détruit
        // avant l'usage du pointeur, UB garantie.
        SelectResult DoSelect(PlayerId player,
                              const std::string& requestedId,
                              const std::string& permissionKey,
                              const std::string& actionKey,
                              const std::function<void(rpframework::data::PlayerData&,
                                                       const std::string&)>& apply,
                              const std::function<bool(const rpframework::data::PlayerData&)>& isAlreadySet,
                              const std::function<std::optional<SelectionCondition>(const std::string& id)>& getCondition)
        {
            using namespace rpframework::security;

            // 1. Permission
            if (!Permissions::Check(Level::PLAYER, permissionKey))
            {
                AuditLog::LogDenied(actionKey, player, "permission");
                return SelectResult::Make(SelectResult::Status::PermissionDenied,
                    "permission refusée pour " + permissionKey);
            }

            // 2. Rate limit
            if (!RateLimiter::Allow(player, actionKey))
            {
                AuditLog::LogDenied(actionKey, player, "rate_limit", {
                    {"retry_in_sec", RateLimiter::SecondsUntilNext(player, actionKey)},
                });
                return SelectResult::Make(SelectResult::Status::RateLimited,
                    "rate limit atteint pour " + actionKey);
            }

            // 3. Charge le profil. Si le fichier n'existe pas (nouveau
            //    joueur, jamais connecté), on crée un PlayerData vide
            //    plutôt que de bloquer la sélection. Le Save() final
            //    créera le fichier. Corrupt/Unavailable restent des
            //    échecs durs (fichier présent mais inutilisable).
            auto load = rpframework::data::PlayerStore::LoadDetailed(player);
            rpframework::data::PlayerData data;
            if (load.status == rpframework::data::PlayerLoadStatus::Missing)
            {
                data.id        = player;
                data.createdAt = std::chrono::system_clock::now();
                data.updatedAt = data.createdAt;
            }
            else if (!load.HasData())
            {
                AuditLog::LogDenied(actionKey, player, "player_data_unavailable", {
                    {"status", static_cast<int>(load.status)},
                });
                return SelectResult::Make(SelectResult::Status::PlayerDataUnavailable,
                    "impossible de charger le profil joueur (status=" + std::to_string(static_cast<int>(load.status)) + ")");
            }
            else
            {
                data = std::move(*load.data);
            }

            // 4. Vérifier que l'ID existe et récupérer la condition (par valeur)
            auto cond = getCondition(requestedId);
            if (!cond.has_value())
            {
                AuditLog::LogDenied(actionKey, player, "unknown_id", {
                    {"id", requestedId},
                });
                return SelectResult::Make(SelectResult::Status::UnknownId,
                    "id inconnu : " + requestedId);
            }

            // 5. Vérifier que le champ n'est pas déjà rempli (one-shot)
            if (isAlreadySet(data))
            {
                return SelectResult::Make(SelectResult::Status::AlreadySet,
                    "selection deja effectuee pour ce joueur");
            }

            // 6. Vérifier les conditions de sélection par rapport à
            //    l'état actuel du joueur.
            const int level = data.level;
            if (!cond->IsSatisfiedBy(data.race, data.profession, data.playerClass, level, data.reputation))
            {
                const std::string reason = cond->DescribeViolation(
                    data.race, data.profession, data.playerClass, level, data.reputation);
                AuditLog::LogDenied(actionKey, player, "condition_not_met", {
                    {"id",     requestedId},
                    {"reason", reason},
                });
                return SelectResult::Make(SelectResult::Status::ConditionNotMet, reason);
            }

            // 7. Applique la modification
            apply(data, requestedId);

            // 8. Persiste
            if (!rpframework::data::PlayerStore::Save(data))
            {
                AuditLog::Log("character.save_failed", player, {
                    {"action", actionKey},
                    {"id",     requestedId},
                }, audit_severity::kError);
                return SelectResult::Make(SelectResult::Status::PlayerDataUnavailable,
                    "sauvegarde du profil échouée");
            }

            // 9. Audit OK
            AuditLog::Log(actionKey, player, {
                {"id", requestedId},
            });
            return SelectResult::MakeSuccess("selection enregistree : " + requestedId);
        }
    }

    SelectResult SelectRace(PlayerId player, const std::string& raceId)
    {
        using namespace rpframework::security;
        return DoSelect(
            player, raceId,
            /*permissionKey*/ "race.select",
            /*actionKey*/     "character.race.select",
            /*apply*/         [](rpframework::data::PlayerData& d, const std::string& id) { d.race = id; },
            /*isAlreadySet*/  [](const rpframework::data::PlayerData& d) { return !d.race.empty(); },
            /*getCondition*/  [](const std::string& id) -> std::optional<SelectionCondition> {
                auto r = Registry::GetRace(id);
                if (!r) return std::nullopt;
                return r->selectionCondition;
            }
        );
    }

    SelectResult SelectProfession(PlayerId player, const std::string& professionId)
    {
        return DoSelect(
            player, professionId,
            /*permissionKey*/ "profession.select",
            /*actionKey*/     "character.profession.select",
            /*apply*/         [](rpframework::data::PlayerData& d, const std::string& id) { d.profession = id; },
            /*isAlreadySet*/  [](const rpframework::data::PlayerData& d) { return !d.profession.empty(); },
            /*getCondition*/  [](const std::string& id) -> std::optional<SelectionCondition> {
                auto p = Registry::GetProfession(id);
                if (!p) return std::nullopt;
                return p->selectionCondition;
            }
        );
    }

    SelectResult SelectClass(PlayerId player, const std::string& classId)
    {
        if (!Registry::ClassesEnabled())
        {
            security::AuditLog::LogDenied("character.class.select", player, "classes_disabled");
            return SelectResult::Make(SelectResult::Status::ClassesDisabled,
                "le système de classes est désactivé sur ce serveur");
        }
        return DoSelect(
            player, classId,
            /*permissionKey*/ "class.select",
            /*actionKey*/     "character.class.select",
            /*apply*/         [](rpframework::data::PlayerData& d, const std::string& id) { d.playerClass = id; },
            /*isAlreadySet*/  [](const rpframework::data::PlayerData& d) { return !d.playerClass.empty(); },
            /*getCondition*/  [](const std::string& id) -> std::optional<SelectionCondition> {
                auto c = Registry::GetClass(id);
                if (!c) return std::nullopt;
                return c->selectionCondition;
            }
        );
    }

    bool ResetSelections(PlayerId player)
    {
        using namespace rpframework::security;
        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return false;

        auto& data = *load.data;
        const bool wasAny = !data.race.empty() || !data.profession.empty() || !data.playerClass.empty();
        if (!wasAny) return false;

        data.race.clear();
        data.profession.clear();
        data.playerClass.clear();
        const bool ok = rpframework::data::PlayerStore::Save(data);
        if (ok)
        {
            AuditLog::Log("character.selections.reset", player);
        }
        return ok;
    }
}

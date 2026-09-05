// ============================================================================
// RPFramework - Faction / Join - implémentation
// ============================================================================
#include "Faction/Join.h"

#include "Faction/Registry.h"
#include "Faction/Reputation.h"

#include "Data/PlayerStore.h"
#include "Core/Logger.h"

#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <algorithm>

namespace rpframework::faction
{
    using PlayerId = rpframework::security::PlayerId;

    namespace
    {
        // Vérifie les restrictions "excluded_*" (race/prof/class).
        // Renvoie le statut d'échec approprié, ou nullopt si OK.
        std::optional<JoinStatus> CheckExcluded(const Faction& f,
                                               const rpframework::data::PlayerData& data)
        {
            if (std::find(f.excludedRaces.begin(), f.excludedRaces.end(), data.race) != f.excludedRaces.end())
            {
                return JoinStatus::RaceExcluded;
            }
            if (std::find(f.excludedProfessions.begin(), f.excludedProfessions.end(), data.profession)
                != f.excludedProfessions.end())
            {
                return JoinStatus::ProfessionExcluded;
            }
            if (std::find(f.excludedClasses.begin(), f.excludedClasses.end(), data.playerClass)
                != f.excludedClasses.end())
            {
                return JoinStatus::ClassExcluded;
            }
            return std::nullopt;
        }
    }

    JoinResult Join(PlayerId player, const std::string& factionId)
    {
        using namespace rpframework::security;
        using namespace rpframework;

        // 1. Permission
        if (!Permissions::Check(Level::PLAYER, "faction.join"))
        {
            AuditLog::LogDenied("faction.join", player, "permission");
            return JoinResult::Make(JoinStatus::PermissionDenied, "permission refusée pour faction.join");
        }

        // 2. Rate limit
        if (!RateLimiter::Allow(player, "faction.join"))
        {
            AuditLog::LogDenied("faction.join", player, "rate_limit");
            return JoinResult::Make(JoinStatus::RateLimited, "rate limit atteint pour faction.join");
        }

        // 3. Faction existe ?
        auto f = Registry::GetFaction(factionId);
        if (!f)
        {
            AuditLog::LogDenied("faction.join", player, "unknown_faction", {{"id", factionId}});
            return JoinResult::Make(JoinStatus::UnknownFaction, "faction inconnue : " + factionId);
        }

        // 4. Profil joueur
        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            AuditLog::LogDenied("faction.join", player, "player_data_unavailable");
            return JoinResult::Make(JoinStatus::PlayerDataUnavailable, "profil joueur indisponible");
        }
        auto data = *load.data;

        // 5. Déjà dans une faction ?
        if (!data.faction.empty())
        {
            if (data.faction == factionId)
            {
                return JoinResult::Make(JoinStatus::AlreadyInFaction, "déjà membre de cette faction");
            }
            return JoinResult::Make(JoinStatus::AlreadyInAnotherFaction,
                "déjà membre de " + data.faction + " ; quittez-la d'abord");
        }

        // 6. Excluded race/prof/class
        if (auto excl = CheckExcluded(*f, data))
        {
            AuditLog::LogDenied("faction.join", player,
                (excl.value() == JoinStatus::RaceExcluded)        ? "race_excluded" :
                (excl.value() == JoinStatus::ProfessionExcluded) ? "profession_excluded" :
                                                                   "class_excluded",
                {{"faction", factionId}});
            return JoinResult::Make(excl.value(), "restriction de faction");
        }

        // 7. Conditions (level + reputation croisée)
        if (!f->joinCondition.IsSatisfiedBy(data.race, data.profession, data.playerClass,
                                            data.level, data.reputation))
        {
            const std::string reason = f->joinCondition.DescribeViolation(
                data.race, data.profession, data.playerClass, data.level, data.reputation);
            AuditLog::LogDenied("faction.join", player, "condition_not_met",
                {{"faction", factionId}, {"reason", reason}});
            return JoinResult::Make(JoinStatus::ConditionNotMet, reason);
        }

        // 8. Adhésion : set PlayerData.faction + réputation initiale.
        data.faction = factionId;
        if (f->initialReputation != 0)
        {
            data.reputation[factionId] = f->initialReputation;
        }

        if (!data::PlayerStore::Save(data))
        {
            return JoinResult::Make(JoinStatus::PlayerDataUnavailable, "sauvegarde échouée");
        }

        AuditLog::Log("faction.join", player, {
            {"faction",             factionId},
            {"initial_reputation",  f->initialReputation},
        });
        return JoinResult::MakeSuccess("adhésion enregistrée : " + factionId);
    }

    JoinResult Leave(PlayerId player)
    {
        using namespace rpframework::security;
        using namespace rpframework;

        if (!Permissions::Check(Level::PLAYER, "faction.leave"))
        {
            AuditLog::LogDenied("faction.leave", player, "permission");
            return JoinResult::Make(JoinStatus::PermissionDenied, "permission refusée pour faction.leave");
        }
        if (!RateLimiter::Allow(player, "faction.leave"))
        {
            AuditLog::LogDenied("faction.leave", player, "rate_limit");
            return JoinResult::Make(JoinStatus::RateLimited, "rate limit atteint pour faction.leave");
        }

        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            return JoinResult::Make(JoinStatus::PlayerDataUnavailable, "profil joueur indisponible");
        }
        auto data = *load.data;
        if (data.faction.empty())
        {
            return JoinResult::Make(JoinStatus::AlreadyInFaction,  // "déjà hors faction"
                "n'appartient à aucune faction");
        }

        const std::string oldFaction = data.faction;
        data.faction.clear();
        if (!data::PlayerStore::Save(data))
        {
            return JoinResult::Make(JoinStatus::PlayerDataUnavailable, "sauvegarde échouée");
        }

        AuditLog::Log("faction.leave", player, {{"faction", oldFaction}});
        return JoinResult::MakeSuccess("quitté " + oldFaction);
    }
}

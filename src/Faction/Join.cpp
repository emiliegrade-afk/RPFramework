// ============================================================================
// RPFramework - Faction / Join - implémentation
// ============================================================================
#include "Faction/Join.h"

#include "Faction/Registry.h"
#include "Faction/Reputation.h"
#include "Asa/PawnEffects.h"
#include "Loadout/AsaDeliver.h"
#include "Loadout/Item.h"
#include "Quest/Engine.h"

#include "Data/PlayerStore.h"
#include "Core/Logger.h"

#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <algorithm>

namespace rpframework::faction
{
    using PlayerId = rpframework::security::PlayerId;

    std::optional<JoinStatus> CheckRestrictions(const Faction& f,
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

    JoinResult Join(PlayerId player, const std::string& factionId)
    {
        using namespace rpframework::security;
        using namespace rpframework;

        // 1. Permission (niveau réel du joueur, résolu depuis la config)
        if (!Permissions::CheckFor(player, "faction.join"))
        {
            AuditLog::LogDenied("faction.join", player, "permission");
            return JoinResult::Make(JoinStatus::PermissionDenied, "permission refusée pour faction.join");
        }

        data::PlayerStore::ExclusiveLock storeLock;

        auto f = Registry::GetFaction(factionId);
        if (!f)
        {
            AuditLog::LogDenied("faction.join", player, "unknown_faction", {{"id", factionId}});
            return JoinResult::Make(JoinStatus::UnknownFaction, "faction inconnue : " + factionId);
        }

        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            AuditLog::LogDenied("faction.join", player, "player_data_unavailable");
            return JoinResult::Make(JoinStatus::PlayerDataUnavailable, "profil joueur indisponible");
        }
        auto data = *load.data;

        if (!data.faction.empty())
        {
            if (data.faction == factionId)
            {
                return JoinResult::Make(JoinStatus::AlreadyInFaction, "déjà membre de cette faction");
            }
            return JoinResult::Make(JoinStatus::AlreadyInAnotherFaction,
                "déjà membre de " + data.faction + " ; quittez-la d'abord");
        }

        if (auto excl = CheckRestrictions(*f, data))
        {
            AuditLog::LogDenied("faction.join", player,
                (excl.value() == JoinStatus::RaceExcluded)        ? "race_excluded" :
                (excl.value() == JoinStatus::ProfessionExcluded) ? "profession_excluded" :
                                                                   "class_excluded",
                {{"faction", factionId}});
            return JoinResult::Make(excl.value(), "restriction de faction");
        }

        if (!f->joinCondition.IsSatisfiedBy(data.race, data.profession, data.playerClass,
                                            data.level, data.reputation))
        {
            const std::string reason = f->joinCondition.DescribeViolation(
                data.race, data.profession, data.playerClass, data.level, data.reputation);
            AuditLog::LogDenied("faction.join", player, "condition_not_met",
                {{"faction", factionId}, {"reason", reason}});
            return JoinResult::Make(JoinStatus::ConditionNotMet, reason);
        }

        if (!RateLimiter::Allow(player, "faction.join"))
        {
            AuditLog::LogDenied("faction.join", player, "rate_limit");
            return JoinResult::Make(JoinStatus::RateLimited, "rate limit atteint pour faction.join");
        }

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
        OnJoined(player, factionId);
        rpframework::asa::ApplyWorldEffects(player, rpframework::asa::WorldApply::Stats);
        std::string extra;
        if (!f->starterQuests.empty())
        {
            extra += " ; quetes: ";
            for (std::size_t i = 0; i < f->starterQuests.size(); ++i)
            {
                if (i > 0) extra += ", ";
                extra += f->starterQuests[i];
            }
        }
        if (f->journal.is_object() && !f->journal.empty())
            extra += " ; journal donne";
        return JoinResult::MakeSuccess("adhesion enregistree : " + factionId + extra);
    }

    void OnJoined(PlayerId player, const std::string& factionId)
    {
        auto f = Registry::GetFaction(factionId);
        if (!f) return;

        for (const auto& questId : f->starterQuests)
        {
            if (questId.empty()) continue;
            const auto started = quest::Start(player, questId);
            if (started.success())
            {
                core::LogInfo("Quete de faction '{}' demarree pour {}.", questId, player);
            }
        }

        if (!f->journal.is_object() || f->journal.empty()) return;
        const std::string flag = "journal:" + factionId;
        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return;
        auto data = *load.data;
        if (std::find(data.unlocks.begin(), data.unlocks.end(), flag) != data.unlocks.end())
            return;
        data.unlocks.push_back(flag);
        if (!data::PlayerStore::Save(data)) return;

        loadout::Item item = loadout::Item::FromJson(f->journal);
        if (item.id.empty()) item.id = "quest_journal";
        if (item.quantity < 1) item.quantity = 1;
        loadout::TryGiveItems(player, {item});
        security::AuditLog::Log("faction.journal.given", player, {
            {"faction", factionId}, {"item", item.id},
        });
    }

    JoinResult Leave(PlayerId player)
    {
        using namespace rpframework::security;
        using namespace rpframework;

        if (!Permissions::CheckFor(player, "faction.leave"))
        {
            AuditLog::LogDenied("faction.leave", player, "permission");
            return JoinResult::Make(JoinStatus::PermissionDenied, "permission refusée pour faction.leave");
        }

        data::PlayerStore::ExclusiveLock storeLock;

        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            return JoinResult::Make(JoinStatus::PlayerDataUnavailable, "profil joueur indisponible");
        }
        auto data = *load.data;
        if (data.faction.empty())
        {
            return JoinResult::Make(JoinStatus::AlreadyInFaction,
                "n'appartient à aucune faction");
        }

        if (!RateLimiter::Allow(player, "faction.leave"))
        {
            AuditLog::LogDenied("faction.leave", player, "rate_limit");
            return JoinResult::Make(JoinStatus::RateLimited, "rate limit atteint pour faction.leave");
        }

        const std::string oldFaction = data.faction;
        data.faction.clear();
        if (!data::PlayerStore::Save(data))
        {
            return JoinResult::Make(JoinStatus::PlayerDataUnavailable, "sauvegarde échouée");
        }

        AuditLog::Log("faction.leave", player, {{"faction", oldFaction}});
        rpframework::asa::ApplyWorldEffects(player, rpframework::asa::WorldApply::Stats);
        return JoinResult::MakeSuccess("quitté " + oldFaction);
    }
}

// ============================================================================
// RPFramework - Quest / Gameplay events - implementation
// ============================================================================
#include "Quest/Events.h"

#include "Data/PlayerStore.h"
#include "Quest/Engine.h"
#include "Quest/Match.h"
#include "Quest/Registry.h"

namespace rpframework::quest
{
    namespace
    {
        // AddProgress re-teste ObjectiveMatches de son côté : il faut lui
        // transmettre l'alias qui a réellement matché, pas le chemin blueprint
        // (Match.h ne découpe que sur '_', donc "boar" disparaît du path).
        std::string FirstMatchingAlias(std::string_view type,
                                       const std::vector<std::string>& aliases,
                                       const Quest& quest)
        {
            for (const auto& objective : quest.objectives)
            {
                for (const auto& alias : aliases)
                {
                    if (alias.empty()) continue;
                    if (ObjectiveMatches(type, alias, objective))
                        return alias;
                }
            }
            return {};
        }
    }

    int ReportGameplay(PlayerId player, std::string_view type,
                       std::string_view entity, int amount,
                       std::vector<EventNotice>* notices)
    {
        return ReportGameplay(player, type,
            std::vector<std::string>{std::string(entity)}, amount, notices);
    }

    int ReportGameplay(PlayerId player, std::string_view type,
                       const std::vector<std::string>& aliases, int amount,
                       std::vector<EventNotice>* notices)
    {
        if (amount <= 0) return 0;

        const auto loaded = data::PlayerStore::LoadDetailed(player);
        if (!loaded.HasData()) return 0;

        int updated = 0;
        for (const auto& quest : Registry::ListQuests())
        {
            const auto progress = loaded.data->quests.find(quest.id);
            if (progress == loaded.data->quests.end()
                || progress->second.status != data::QuestProgress::Status::Active)
                continue;

            const auto matched = FirstMatchingAlias(type, aliases, quest);
            if (matched.empty()) continue;
            const auto result = AddProgress(player, quest.id, type, matched, amount);
            if (!result.success()) continue;
            ++updated;
            if (notices != nullptr)
            {
                EventNotice notice;
                notice.completed = result.message.find("terminee") != std::string::npos;
                const auto name = quest.name.empty() ? quest.id : quest.name;
                notice.message = notice.completed
                    ? (name + " terminee")
                    : DescribeProgress(player, quest.id);
                notices->push_back(std::move(notice));
            }
        }
        return updated;
    }

    namespace
    {
        int ReportTyped(PlayerId player, std::string_view type,
                        std::string_view entity, int amount)
        {
            return ReportGameplay(player, type, entity, amount);
        }
    }

    int ReportKill(PlayerId player, std::string_view entity, int amount)
    { return ReportTyped(player, "kill", entity, amount); }

    int ReportCollection(PlayerId player, std::string_view entity, int amount)
    { return ReportTyped(player, "collect", entity, amount); }

    int ReportTame(PlayerId player, std::string_view entity, int amount)
    { return ReportTyped(player, "tame", entity, amount); }

    int ReportCraft(PlayerId player, std::string_view entity, int amount)
    { return ReportTyped(player, "craft", entity, amount); }

    int ReportDelivery(PlayerId player, std::string_view entity, int amount)
    { return ReportTyped(player, "deliver", entity, amount); }

    int ReportExploration(PlayerId player, std::string_view zone)
    { return ReportTyped(player, "explore", zone, 1); }

    int ReportInteraction(PlayerId player, std::string_view entity)
    { return ReportTyped(player, "interact", entity, 1); }

    int ReportProfessionProgress(PlayerId player, std::string_view profession, int amount)
    { return ReportTyped(player, "profession", profession, amount); }

    int ReportReputationProgress(PlayerId player, std::string_view faction, int amount)
    { return ReportTyped(player, "reputation", faction, amount); }

    int ReportQuestCompleted(PlayerId player, std::string_view questId)
    { return ReportTyped(player, "quest", questId, 1); }
}

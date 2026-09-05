// ============================================================================
// RPFramework - Quest / Gameplay events - implementation
// ============================================================================
#include "Quest/Events.h"

#include "Data/PlayerStore.h"
#include "Quest/Engine.h"
#include "Quest/Registry.h"

namespace rpframework::quest
{
    int ReportEvent(PlayerId player, std::string_view type,
                    std::string_view entity, int amount)
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

            bool matches = false;
            for (const auto& objective : quest.objectives)
            {
                if (objective.type == type && objective.entity == entity)
                {
                    matches = true;
                    break;
                }
            }
            if (matches && AddProgress(player, quest.id, type, entity, amount).success())
                ++updated;
        }
        return updated;
    }

    namespace
    {
        int ReportTyped(PlayerId player, std::string_view type,
                        std::string_view entity, int amount)
        {
            return ReportEvent(player, type, entity, amount);
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

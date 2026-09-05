// ============================================================================
// RPFramework - Quest / Gameplay events
// ============================================================================
#pragma once

#include "Security/Types.h"

#include <string_view>

namespace rpframework::quest
{
    using PlayerId = rpframework::security::PlayerId;

    // Point d'entree pour les hooks ASA. Un evenement peut faire progresser
    // toutes les quetes actives du joueur dont un objectif correspond.
    int ReportEvent(PlayerId player, std::string_view type,
                    std::string_view entity, int amount = 1);

    int ReportKill(PlayerId player, std::string_view entity, int amount = 1);
    int ReportCollection(PlayerId player, std::string_view entity, int amount = 1);
    int ReportTame(PlayerId player, std::string_view entity, int amount = 1);
    int ReportCraft(PlayerId player, std::string_view entity, int amount = 1);
    int ReportDelivery(PlayerId player, std::string_view entity, int amount = 1);
    int ReportExploration(PlayerId player, std::string_view zone);
    int ReportInteraction(PlayerId player, std::string_view entity);
    int ReportProfessionProgress(PlayerId player, std::string_view profession,
                                 int amount = 1);
    int ReportReputationProgress(PlayerId player, std::string_view faction,
                                 int amount = 1);
    int ReportQuestCompleted(PlayerId player, std::string_view questId);
}

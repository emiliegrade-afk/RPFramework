// ============================================================================
// RPFramework - Quest / Engine
// ============================================================================
#pragma once

#include "Quest/Definitions.h"
#include "Security/Types.h"

#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"

namespace rpframework::quest
{
    using PlayerId = rpframework::security::PlayerId;

    enum class Status
    {
        Success,
        UnknownQuest,
        AlreadyActive,
        AlreadyCompleted,
        PrerequisiteNotMet,
        ConditionNotMet,
        PlayerDataUnavailable,
        InvalidObjective,
        NotActive,
        NotComplete,
        RewardFailed,
    };

    struct Result
    {
        Status status = Status::Success;
        std::string message;
        bool success() const { return status == Status::Success; }
    };

    Result Start(PlayerId player, std::string_view questId);
    Result Abandon(PlayerId player, std::string_view questId);
    Result AddProgress(PlayerId player, std::string_view questId,
                       std::string_view objectiveType, std::string_view entity,
                       int amount = 1);
    Result Complete(PlayerId player, std::string_view questId);

    // Retourne une vue textuelle stable pour l'interface chat/console.
    std::string DescribeProgress(PlayerId player, std::string_view questId);
    std::vector<std::string> ListAvailableQuests(PlayerId player);

    // L'adaptateur ASA lit les objets puis confirme uniquement apres
    // attribution effective dans l'inventaire du joueur.
    std::vector<nlohmann::json> PendingItemRewards(PlayerId player,
                                                    std::string_view questId);
    // Confirme la livraison de TOUS les items en attente (drain complet).
    // Conservé pour les tests / cas simples. Pour les livraisons partielles
    // (un item sur N livré par ASA), préférer ConfirmItemReward ci-dessous.
    bool ConfirmItemRewardsDelivered(PlayerId player, std::string_view questId);
    // Confirme la livraison d'un seul reward par son id. Renvoie false si
    // aucun reward correspondant n'est en attente. Permet de ne pas perdre
    // les items non livrés si l'adaptateur ASA échoue partiellement.
    bool ConfirmItemReward(PlayerId player, std::string_view questId,
                            const std::string& rewardId);
}

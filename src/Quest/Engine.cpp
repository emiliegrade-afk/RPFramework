// ============================================================================
// RPFramework - Quest / Engine - implementation
// ============================================================================
#include "Quest/Engine.h"

#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Economy/Registry.h"
#include "Economy/Wallet.h"
#include "Faction/Reputation.h"
#include "Quest/Match.h"
#include "Quest/Registry.h"
#include "Loadout/AsaDeliver.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rpframework::quest
{
    namespace
    {
        std::mutex& EngineMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        Result Make(Status status, std::string message)
        {
            return {status, std::move(message)};
        }

        std::optional<data::PlayerData> Load(PlayerId player)
        {
            auto loaded = data::PlayerStore::LoadDetailed(player);
            if (!loaded.HasData()) return std::nullopt;
            return std::move(loaded.data);
        }

        bool IsCompleted(const data::PlayerData& player, const std::string& id)
        {
            auto it = player.quests.find(id);
            return it != player.quests.end()
                && it->second.status == data::QuestProgress::Status::Completed;
        }

        bool ObjectivesComplete(const Quest& quest, const data::QuestProgress& progress)
        {
            bool hasRequired = false;
            bool hasComplete = false;
            for (const auto& objective : quest.objectives)
            {
                if (!objective.required) continue;
                hasRequired = true;
                auto it = progress.objectives.find(objective.id);
                const bool complete = it != progress.objectives.end() && it->second >= objective.target;
                hasComplete = hasComplete || complete;
                if (quest.objectiveMode == "all" && !complete)
                    return false;
            }
            return !hasRequired || quest.objectiveMode == "all" || hasComplete;
        }

        bool IsExpired(const Quest& quest, const data::QuestProgress& progress)
        {
            if (progress.startedAt <= 0) return false;
            const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            for (const auto& objective : quest.objectives)
                if (objective.timeLimitSeconds > 0
                    && now - progress.startedAt > objective.timeLimitSeconds)
                    return true;
            return false;
        }

        Result ValidateRewards(const Quest& quest)
        {
            for (const auto& reward : quest.rewards)
            {
                // `amount` est ignoré pour title/unlock (seul l'`id` compte),
                // donc on ne l'exige > 0 que pour les autres types.
                if (reward.amount <= 0
                    && reward.type != "title" && reward.type != "unlock")
                    return Make(Status::RewardFailed, "recompense invalide");
                if (reward.type == "currency" && !economy::Registry::HasCurrency(reward.id))
                    return Make(Status::RewardFailed, "monnaie de recompense inconnue");
                if (reward.type != "currency" && reward.type != "xp"
                    && reward.type != "reputation" && reward.type != "title"
                    && reward.type != "item" && reward.type != "unlock")
                    return Make(Status::RewardFailed, "type de recompense inconnu");
            }
            return {Status::Success, "ok"};
        }

        // Mutate `data` only. Caller persist + marks Completed in the same Save.
        Result ApplyRewardsInPlace(data::PlayerData& data, const Quest& quest,
                                   std::vector<faction::ReputationChange>& repChanges)
        {
            auto& progress = data.quests[quest.id];
            for (const auto& reward : quest.rewards)
            {
                if (reward.type == "currency")
                {
                    auto tx = economy::CreditInPlace(data, reward.id, reward.amount);
                    if (tx.status != economy::TxStatus::Success)
                        return Make(Status::RewardFailed, tx.message);
                }
                else if (reward.type == "reputation")
                {
                    auto applied = faction::ApplyReputationDelta(
                        data, reward.id, reward.amount, true);
                    if (!applied.ok)
                        return Make(Status::RewardFailed, applied.message);
                    repChanges.insert(repChanges.end(),
                        applied.changes.begin(), applied.changes.end());
                }
                else if (reward.type == "xp")
                {
                    if (reward.amount > 0
                        && data.xp > (std::numeric_limits<int>::max() - reward.amount))
                        return Make(Status::RewardFailed, "xp overflow");
                    data.xp += reward.amount;

                    // Courbe globale, indépendante du métier (dette n°2).
                    // La progression métier vit dans PlayerData.professions.
                    constexpr int kGlobalXpPerLevel = 1000;
                    const int derived = 1 + (data.xp / kGlobalXpPerLevel);
                    if (derived > data.level)
                        data.level = derived;
                }
                else if (reward.type == "title")
                {
                    if (std::find(data.titles.begin(), data.titles.end(), reward.id)
                        == data.titles.end())
                        data.titles.push_back(reward.id);
                }
                else if (reward.type == "item")
                {
                    progress.pendingItemRewards.push_back({
                        {"id", reward.id},
                        {"amount", reward.amount},
                        {"payload", reward.payload},
                    });
                }
                else if (reward.type == "unlock")
                {
                    if (std::find(data.unlocks.begin(), data.unlocks.end(), reward.id)
                        == data.unlocks.end())
                        data.unlocks.push_back(reward.id);
                }
            }
            return {Status::Success, "ok"};
        }

        void AuditGrantedRewards(PlayerId player, const Quest& quest,
                                 const data::PlayerData& data,
                                 const std::vector<faction::ReputationChange>& repChanges)
        {
            for (const auto& reward : quest.rewards)
            {
                if (reward.type == "currency")
                {
                    const int64_t after = data.wallets.count(reward.id)
                        ? data.wallets.at(reward.id) : 0;
                    security::AuditLog::Log("economy.reward", player, {
                        {"currency", reward.id},
                        {"amount", static_cast<int64_t>(reward.amount)},
                        {"after", after},
                        {"reason", "quest:" + quest.id},
                        {"source", "quest"},
                    }, security::audit_severity::kReward);
                }
            }
            faction::AuditReputationChanges(player, "quest:" + quest.id, repChanges);
        }
    }

    Result Start(PlayerId player, std::string_view questId)
    {
        data::PlayerStore::ExclusiveLock storeLock;
        std::lock_guard<std::mutex> guard(EngineMutex());
        if (!security::Permissions::CheckFor(player, "quest.accept"))
            return Make(Status::ConditionNotMet, "permission refusee");
        if (!security::RateLimiter::Allow(player, "quest.accept"))
            return Make(Status::ConditionNotMet, "trop de demandes");
        const auto quest = Registry::GetQuest(std::string(questId));
        if (!quest) return Make(Status::UnknownQuest, "quete inconnue");
        auto data = Load(player);
        if (!data) return Make(Status::PlayerDataUnavailable, "profil indisponible");

        auto existing = data->quests.find(quest->id);
        if (existing != data->quests.end())
        {
            if (existing->second.status == data::QuestProgress::Status::Completed
                && !quest->repeatable)
                return Make(Status::AlreadyCompleted, "quete deja terminee");
            if (existing->second.status == data::QuestProgress::Status::Active)
                return Make(Status::AlreadyActive, "quete deja active");
        }
        if (data->level < quest->minLevel)
            return Make(Status::ConditionNotMet, "niveau insuffisant");
        if (!quest->condition.IsSatisfiedBy(data->race, data->profession,
                                           data->playerClass, data->level,
                                           data->reputation))
            return Make(Status::ConditionNotMet, quest->condition.DescribeViolation(
                data->race, data->profession, data->playerClass, data->level,
                data->reputation));
        for (const auto& prerequisite : quest->prerequisites)
            if (!IsCompleted(*data, prerequisite))
                return Make(Status::PrerequisiteNotMet, "prerequis non rempli");

        data::QuestProgress progress;
        progress.startedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        data->quests[quest->id] = progress;
        if (!data::PlayerStore::Save(*data))
            return Make(Status::PlayerDataUnavailable, "sauvegarde echouee");
        security::AuditLog::Log("quest.start", player, {{"quest", quest->id}});
        return {Status::Success, "quete demarree"};
    }

    Result Abandon(PlayerId player, std::string_view questId)
    {
        data::PlayerStore::ExclusiveLock storeLock;
        std::lock_guard<std::mutex> guard(EngineMutex());
        if (!security::Permissions::CheckFor(player, "quest.accept"))
            return Make(Status::ConditionNotMet, "permission refusee");
        auto data = Load(player);
        if (!data) return Make(Status::PlayerDataUnavailable, "profil indisponible");
        auto it = data->quests.find(std::string(questId));
        if (it == data->quests.end() || it->second.status != data::QuestProgress::Status::Active)
            return Make(Status::NotActive, "quete non active");
        data->quests.erase(it);
        if (!data::PlayerStore::Save(*data))
            return Make(Status::PlayerDataUnavailable, "sauvegarde echouee");
        security::AuditLog::Log("quest.abandon", player, {{"quest", std::string(questId)}});
        return {Status::Success, "quete abandonnee"};
    }

    Result AddProgress(PlayerId player, std::string_view questId,
                       std::string_view objectiveType, std::string_view entity,
                       int amount)
    {
        bool deliver = false;
        Result result;
        {
            data::PlayerStore::ExclusiveLock storeLock;
            std::lock_guard<std::mutex> guard(EngineMutex());
            if (amount <= 0) return Make(Status::InvalidObjective, "progression invalide");
            const auto quest = Registry::GetQuest(std::string(questId));
            if (!quest) return Make(Status::UnknownQuest, "quete inconnue");
            auto data = Load(player);
            if (!data) return Make(Status::PlayerDataUnavailable, "profil indisponible");
            auto it = data->quests.find(quest->id);
            if (it != data->quests.end()
                && it->second.status == data::QuestProgress::Status::Completed)
                return Make(Status::AlreadyCompleted, "quete deja terminee");
            if (it == data->quests.end() || it->second.status != data::QuestProgress::Status::Active)
                return Make(Status::NotActive, "quete non active");

            bool matched = false;
            for (const auto& objective : quest->objectives)
            {
                if (ObjectiveMatches(objectiveType, entity, objective))
                {
                    matched = true;
                    it->second.objectives[objective.id] = std::min(
                        objective.target,
                        it->second.objectives[objective.id] + amount);
                }
            }
            if (!matched) return Make(Status::InvalidObjective, "objectif non correspondant");
            if (!data::PlayerStore::Save(*data))
                return Make(Status::PlayerDataUnavailable, "sauvegarde echouee");
            security::AuditLog::Log("quest.progress", player, {
                {"quest", quest->id}, {"type", std::string(objectiveType)},
                {"entity", std::string(entity)}, {"amount", amount}
            });

            if (quest->autoComplete
                && !IsExpired(*quest, it->second)
                && ObjectivesComplete(*quest, it->second)
                && !it->second.rewardsGranted)
            {
                const auto validation = ValidateRewards(*quest);
                if (!validation.success()) return validation;
                std::vector<faction::ReputationChange> repChanges;
                auto rewards = ApplyRewardsInPlace(*data, *quest, repChanges);
                if (!rewards.success()) return rewards;
                it->second.status = data::QuestProgress::Status::Completed;
                it->second.rewardsGranted = true;
                if (!data::PlayerStore::Save(*data))
                    return Make(Status::PlayerDataUnavailable, "sauvegarde echouee");
                AuditGrantedRewards(player, *quest, *data, repChanges);
                security::AuditLog::Log("quest.complete", player, {
                    {"quest", quest->id}, {"auto", true}
                });
                deliver = true;
                result = {Status::Success, "quete terminee"};
            }
            else
            {
                result = {Status::Success, "progression ajoutee"};
            }
        }
        if (deliver)
            loadout::TryGivePendingQuestItems(player);
        return result;
    }

    Result Complete(PlayerId player, std::string_view questId)
    {
        bool deliver = false;
        Result result;
        {
            data::PlayerStore::ExclusiveLock storeLock;
            std::lock_guard<std::mutex> guard(EngineMutex());
            if (!security::Permissions::CheckFor(player, "quest.complete"))
                return Make(Status::ConditionNotMet, "permission refusee");
            if (!security::RateLimiter::Allow(player, "quest.complete"))
                return Make(Status::ConditionNotMet, "trop de demandes");
            const auto quest = Registry::GetQuest(std::string(questId));
            if (!quest) return Make(Status::UnknownQuest, "quete inconnue");
            auto data = Load(player);
            if (!data) return Make(Status::PlayerDataUnavailable, "profil indisponible");
            auto it = data->quests.find(quest->id);
            if (it != data->quests.end()
                && it->second.status == data::QuestProgress::Status::Completed)
                return Make(Status::AlreadyCompleted, "quete deja terminee");
            if (it == data->quests.end() || it->second.status != data::QuestProgress::Status::Active)
                return Make(Status::NotActive, "quete non active");
            if (IsExpired(*quest, it->second))
                return Make(Status::NotComplete, "delai de quete depasse");
            if (!ObjectivesComplete(*quest, it->second))
                return Make(Status::NotComplete, "objectifs incomplets");
            const auto validation = ValidateRewards(*quest);
            if (!validation.success()) return validation;

            auto& progress = it->second;
            if (progress.rewardsGranted)
            {
                progress.status = data::QuestProgress::Status::Completed;
                if (!data::PlayerStore::Save(*data))
                    return Make(Status::PlayerDataUnavailable, "sauvegarde echouee");
                return Make(Status::AlreadyCompleted, "quete deja terminee");
            }

            std::vector<faction::ReputationChange> repChanges;
            auto rewards = ApplyRewardsInPlace(*data, *quest, repChanges);
            if (!rewards.success()) return rewards;
            progress.status = data::QuestProgress::Status::Completed;
            progress.rewardsGranted = true;
            if (!data::PlayerStore::Save(*data))
                return Make(Status::PlayerDataUnavailable, "sauvegarde echouee");
            AuditGrantedRewards(player, *quest, *data, repChanges);
            security::AuditLog::Log("quest.complete", player, {{"quest", quest->id}});
            deliver = true;
            result = {Status::Success, "quete terminee"};
        }
        if (deliver)
            loadout::TryGivePendingQuestItems(player);
        return result;
    }

    std::string DescribeProgress(PlayerId player, std::string_view questId)
    {
        const auto quest = Registry::GetQuest(std::string(questId));
        if (!quest) return "quete inconnue";
        const auto data = Load(player);
        if (!data) return "profil indisponible";
        const auto it = data->quests.find(quest->id);
        if (it == data->quests.end()) return "quete non demarree";

        const auto& progress = it->second;
        std::string result = progress.status == data::QuestProgress::Status::Completed
            ? "terminee" : "active";
        result += " " + quest->id + " [";
        bool first = true;
        for (const auto& objective : quest->objectives)
        {
            if (!first) result += ", ";
            const auto value = progress.objectives.count(objective.id)
                ? progress.objectives.at(objective.id) : 0;
            result += objective.id + "=" + std::to_string(value)
                + "/" + std::to_string(objective.target);
            first = false;
        }
        result += "]";
        return result;
    }

    std::vector<std::string> ListAvailableQuests(PlayerId player)
    {
        const auto data = Load(player);
        if (!data) return {};
        std::vector<std::string> available;
        for (const auto& quest : Registry::ListQuests())
        {
            if (IsCompleted(*data, quest.id) && !quest.repeatable) continue;
            const auto existing = data->quests.find(quest.id);
            if (existing != data->quests.end()
                && existing->second.status == data::QuestProgress::Status::Active)
                continue;
            if (data->level < quest.minLevel
                || !quest.condition.IsSatisfiedBy(data->race, data->profession,
                                                  data->playerClass, data->level,
                                                  data->reputation))
                continue;
            bool prerequisitesMet = true;
            for (const auto& prerequisite : quest.prerequisites)
                if (!IsCompleted(*data, prerequisite)) prerequisitesMet = false;
            if (prerequisitesMet) available.push_back(quest.id);
        }
        return available;
    }

    std::vector<std::string> ListActiveQuests(PlayerId player)
    {
        const auto data = Load(player);
        if (!data) return {};
        std::vector<std::string> active;
        for (const auto& quest : Registry::ListQuests())
        {
            const auto it = data->quests.find(quest.id);
            if (it == data->quests.end()
                || it->second.status != data::QuestProgress::Status::Active)
                continue;
            active.push_back(DescribeProgress(player, quest.id));
        }
        return active;
    }

    std::vector<nlohmann::json> PendingItemRewards(PlayerId player,
                                                    std::string_view questId)
    {
        const auto data = Load(player);
        if (!data) return {};
        const auto it = data->quests.find(std::string(questId));
        if (it == data->quests.end()) return {};
        return it->second.pendingItemRewards;
    }

    bool ConfirmItemRewardsDelivered(PlayerId player, std::string_view questId)
    {
        data::PlayerStore::ExclusiveLock storeLock;
        auto data = Load(player);
        if (!data) return false;
        const auto it = data->quests.find(std::string(questId));
        if (it == data->quests.end() || it->second.pendingItemRewards.empty()) return false;
        it->second.pendingItemRewards.clear();
        if (!data::PlayerStore::Save(*data)) return false;
        security::AuditLog::Log("quest.item_rewards.delivered", player,
            {{"quest", std::string(questId)}});
        return true;
    }

    bool ConfirmItemReward(PlayerId player, std::string_view questId,
                            const std::string& rewardId)
    {
        data::PlayerStore::ExclusiveLock storeLock;
        auto data = Load(player);
        if (!data) return false;
        const auto it = data->quests.find(std::string(questId));
        if (it == data->quests.end() || it->second.pendingItemRewards.empty()) return false;

        const auto before = it->second.pendingItemRewards.size();
        it->second.pendingItemRewards.erase(
            std::remove_if(it->second.pendingItemRewards.begin(),
                           it->second.pendingItemRewards.end(),
                [&](const nlohmann::json& entry) {
                    return entry.is_object()
                        && entry.value("id", std::string{}) == rewardId;
                }),
            it->second.pendingItemRewards.end());

        if (it->second.pendingItemRewards.size() == before) return false;
        if (!data::PlayerStore::Save(*data)) return false;
        security::AuditLog::Log("quest.item_reward.delivered", player,
            {{"quest", std::string(questId)}, {"reward", rewardId}});
        return true;
    }
}

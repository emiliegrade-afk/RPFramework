#include "Loadout/AsaDeliver.h"

#include "Asa/Blueprints.h"
#include "Asa/Identity.h"
#include "Core/Logger.h"
#include "Data/PlayerStore.h"
#include "Quest/Engine.h"
#include "Security/AuditLog.h"

#include "API/ARK/Ark.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rpframework::loadout
{
    namespace
    {
        std::string BlueprintOf(const Item& item)
        {
            if (item.extras.contains("blueprint") && item.extras["blueprint"].is_string())
            {
                return item.extras["blueprint"].get<std::string>();
            }
            if (item.id.find('/') != std::string::npos) return item.id;
            return {};
        }

        float ParseQuality(const nlohmann::json& value)
        {
            if (value.is_number()) return value.get<float>();
            if (value.is_string())
            {
                try { return std::stof(value.get<std::string>()); }
                catch (...) {}
            }
            return 0.0f;
        }

        float QualityOf(const Item& item)
        {
            if (item.extras.contains("quality"))
                return ParseQuality(item.extras["quality"]);
            if (!item.quality.empty())
            {
                try { return std::stof(item.quality); }
                catch (...) {}
            }
            return 0.0f;
        }

        bool GiveOne(AShooterPlayerController* pc, const Item& item)
        {
            const auto blueprint = BlueprintOf(item);
            if (blueprint.empty() || pc == nullptr) return false;

            FString path = asa::Utf8ToFString(blueprint);
            if (path.IsEmpty()) return false;
            return pc->GiveItem(&path, item.quantity, QualityOf(item), false, false, 0.0f);
        }
    }

    int TryGiveItems(security::PlayerId player, const std::vector<Item>& items)
    {
        auto* pc = asa::FindController(player);
        if (pc == nullptr) return 0;

        int given = 0;
        for (const auto& item : items)
        {
            if (GiveOne(pc, item)) ++given;
        }
        if (given > 0)
        {
            security::AuditLog::Log("loadout.asa.given", player, {{"count", given}});
        }
        return given;
    }

    void TryGivePendingQuestItems(security::PlayerId player)
    {
        ItemDeliveryGuard guard(player);
        if (!guard.acquired()) return;

        auto* pc = asa::FindController(player);
        if (pc == nullptr) return;

        struct Job
        {
            std::string questId;
            Item item;
        };
        std::vector<Job> jobs;
        {
            auto load = data::PlayerStore::LoadDetailed(player);
            if (!load.HasData()) return;
            for (const auto& [questId, progress] : load.data->quests)
            {
                for (const auto& pending : progress.pendingItemRewards)
                {
                    auto item = ItemFromPendingReward(pending);
                    if (item.id.empty()) continue;
                    jobs.push_back({questId, std::move(item)});
                }
            }
        }

        for (const auto& job : jobs)
        {
            if (GiveOne(pc, job.item))
                quest::ConfirmItemReward(player, job.questId, job.item.id);
        }
    }

    void TryUnlockEngrams(security::PlayerId player, const std::vector<std::string>& blueprints)
    {
        auto* pc = asa::FindController(player);
        if (pc == nullptr || blueprints.empty()) return;

        auto* state = static_cast<AShooterPlayerState*>(pc->PlayerStateField().Get());
        if (state == nullptr) return;

        int unlocked = 0;
        for (const auto& blueprint : blueprints)
        {
            if (blueprint.empty()) continue;
            FString path = asa::Utf8ToFString(blueprint);
            if (path.IsEmpty()) continue;
            UClass* cls = UVictoryCore::BPLoadClass(path);
            if (cls == nullptr) continue;
            try
            {
                state->ServerUnlockEngram(cls, true, true);
                ++unlocked;
            }
            catch (...)
            {
                core::LogWarn("Unlock engram echoue: {}", blueprint);
            }
        }
        if (unlocked > 0)
        {
            security::AuditLog::Log("loadout.asa.engrams", player, {{"count", unlocked}});
        }
    }

    bool TryTakeItems(security::PlayerId player, std::string_view blueprint, int quantity)
    {
        if (quantity <= 0) return false;
        const auto want = asa::BlueprintKey(blueprint);
        if (want.empty()) return false;

        auto* pc = asa::FindController(player);
        if (pc == nullptr) return false;
        auto* character = pc->GetPlayerCharacter();
        if (character == nullptr) return false;
        UPrimalInventoryComponent* inventory = character->MyInventoryComponentField();
        if (inventory == nullptr) return false;

        struct Slot
        {
            UPrimalItem* item = nullptr;
            int quantity = 0;
        };
        std::vector<Slot> matches;
        int total = 0;
        for (UPrimalItem* item : inventory->InventoryItemsField())
        {
            if (item == nullptr) continue;
            if (asa::BlueprintKey(asa::BlueprintPathOf(item)) != want) continue;
            const int have = item->GetItemQuantity();
            if (have <= 0) continue;
            matches.push_back({item, have});
            total += have;
        }
        if (total < quantity) return false;

        int remaining = quantity;
        for (const auto& slot : matches)
        {
            if (remaining <= 0) break;
            const int take = std::min(slot.quantity, remaining);
            if (take >= slot.quantity)
            {
                slot.item->RemoveItemFromInventory(true, false);
            }
            else
            {
                slot.item->IncrementItemQuantity(-take, true, false, false, false, true, false);
            }
            remaining -= take;
        }
        return remaining == 0;
    }

    void TryGiveBuff(security::PlayerId player, const std::string& blueprint)
    {
        if (blueprint.empty()) return;
        auto* pc = asa::FindController(player);
        if (pc == nullptr) return;
        auto* character = pc->GetPlayerCharacter();
        if (character == nullptr) return;

        FString path = asa::Utf8ToFString(blueprint);
        if (path.IsEmpty()) return;
        UClass* cls = UVictoryCore::BPLoadClass(path);
        if (cls == nullptr)
        {
            core::LogWarn("Buff introuvable: {}", blueprint);
            return;
        }
        try
        {
            TSubclassOf<APrimalBuff> buffClass(cls);
            APrimalBuff::StaticAddBuff(buffClass, character, nullptr, nullptr, true);
            security::AuditLog::Log("loadout.asa.buff", player, {{"blueprint", blueprint}});
        }
        catch (...)
        {
            core::LogWarn("GiveBuff echoue: {}", blueprint);
        }
    }
}

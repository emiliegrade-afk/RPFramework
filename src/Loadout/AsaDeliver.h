// ============================================================================
// RPFramework - Traduction Item.id → GiveItem ASA
// ============================================================================
#pragma once

#include "Loadout/Item.h"
#include "Security/Types.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifdef RPFRAMEWORK_TESTS
#include "Asa/BlueprintPath.h"
#include "Data/PlayerStore.h"
#include "Quest/Engine.h"

#include <functional>
#include <unordered_map>
#endif

namespace rpframework::loadout
{
    namespace detail
    {
        inline std::mutex& DeliveryGuardMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        inline std::unordered_set<security::PlayerId>& InFlightDeliveries()
        {
            static std::unordered_set<security::PlayerId> inflight;
            return inflight;
        }
    }

    // Empêche un GiveItem (ou un stub de test) de ré-entrer GiveStarterKit /
    // TryGivePendingQuestItems pour le même joueur pendant une livraison.
    struct ItemDeliveryGuard
    {
        security::PlayerId player = 0;
        bool ok = false;

        explicit ItemDeliveryGuard(security::PlayerId p) : player(p)
        {
            if (p == 0) return;
            std::lock_guard<std::mutex> lock(detail::DeliveryGuardMutex());
            ok = detail::InFlightDeliveries().insert(p).second;
        }

        ~ItemDeliveryGuard()
        {
            if (!ok) return;
            std::lock_guard<std::mutex> lock(detail::DeliveryGuardMutex());
            detail::InFlightDeliveries().erase(player);
        }

        ItemDeliveryGuard(const ItemDeliveryGuard&) = delete;
        ItemDeliveryGuard& operator=(const ItemDeliveryGuard&) = delete;

        bool acquired() const { return ok; }
    };

    inline Item ItemFromPendingReward(const nlohmann::json& pending)
    {
        Item item;
        item.id = pending.value("id", std::string{});
        item.quantity = pending.value("amount", 1);
        if (pending.contains("payload") && pending["payload"].is_object())
            item.extras = pending["payload"];
        return item;
    }

#ifdef RPFRAMEWORK_TESTS
    inline std::mutex& TestInventoryMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    inline std::unordered_map<security::PlayerId, std::unordered_map<std::string, int>>&
    TestInventoryBags()
    {
        static std::unordered_map<security::PlayerId, std::unordered_map<std::string, int>> bags;
        return bags;
    }

    inline std::unordered_set<std::string>& BlockedGiveKeys()
    {
        static std::unordered_set<std::string> keys;
        return keys;
    }

    inline std::function<void(security::PlayerId, const Item&)>& OnTestGiveItems()
    {
        static std::function<void(security::PlayerId, const Item&)> cb;
        return cb;
    }

    inline void ClearTestInventory(security::PlayerId player = 0)
    {
        std::lock_guard<std::mutex> lock(TestInventoryMutex());
        if (player == 0)
        {
            TestInventoryBags().clear();
            BlockedGiveKeys().clear();
            OnTestGiveItems() = {};
        }
        else
        {
            TestInventoryBags().erase(player);
        }
    }

    inline void SeedTestInventory(security::PlayerId player, std::string_view blueprint, int quantity)
    {
        if (quantity <= 0) return;
        const auto key = asa::BlueprintKey(blueprint);
        if (key.empty()) return;
        std::lock_guard<std::mutex> lock(TestInventoryMutex());
        TestInventoryBags()[player][key] += quantity;
    }

    inline int CountTestInventory(security::PlayerId player, std::string_view blueprint)
    {
        const auto key = asa::BlueprintKey(blueprint);
        std::lock_guard<std::mutex> lock(TestInventoryMutex());
        const auto it = TestInventoryBags().find(player);
        if (it == TestInventoryBags().end()) return 0;
        const auto jt = it->second.find(key);
        return jt == it->second.end() ? 0 : jt->second;
    }

    inline std::string TestItemBlueprint(const Item& item)
    {
        if (item.extras.contains("blueprint") && item.extras["blueprint"].is_string())
            return item.extras["blueprint"].get<std::string>();
        if (item.id.find('/') != std::string::npos) return item.id;
        return {};
    }

    inline int TryGiveItems(security::PlayerId player, const std::vector<Item>& items)
    {
        int given = 0;
        for (const auto& item : items)
        {
            const auto bp = TestItemBlueprint(item);
            if (bp.empty()) continue;
            const auto key = asa::BlueprintKey(bp);
            if (key.empty()) continue;
            if (BlockedGiveKeys().count(key) != 0) continue;
            const int qty = item.quantity > 0 ? item.quantity : 1;
            {
                std::lock_guard<std::mutex> lock(TestInventoryMutex());
                TestInventoryBags()[player][key] += qty;
            }
            ++given;
            auto cb = OnTestGiveItems();
            if (cb) cb(player, item);
        }
        return given;
    }

    inline bool TryTakeItems(security::PlayerId player, std::string_view blueprint, int quantity)
    {
        if (quantity <= 0) return false;
        const auto key = asa::BlueprintKey(blueprint);
        if (key.empty()) return false;
        std::lock_guard<std::mutex> lock(TestInventoryMutex());
        auto& bag = TestInventoryBags()[player];
        const auto it = bag.find(key);
        if (it == bag.end() || it->second < quantity) return false;
        it->second -= quantity;
        return true;
    }

    inline void TryGivePendingQuestItems(security::PlayerId player)
    {
        ItemDeliveryGuard guard(player);
        if (!guard.acquired()) return;

        struct Job
        {
            std::string questId;
            Item item;
        };
        std::vector<Job> jobs;
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
        for (const auto& job : jobs)
        {
            if (TryGiveItems(player, {job.item}) > 0)
                quest::ConfirmItemReward(player, job.questId, job.item.id);
        }
    }

    inline void TryUnlockEngrams(security::PlayerId, const std::vector<std::string>&) {}
    inline std::string g_lastBuffBlueprint;
    inline void TryGiveBuff(security::PlayerId, const std::string& blueprint)
    {
        g_lastBuffBlueprint = blueprint;
    }
#else
    // Donne les items dont extras.blueprint (ou id chemin) est renseigné.
    // Les ids sans blueprint sont ignorés (kit RP-only).
    int TryGiveItems(security::PlayerId player, const std::vector<Item>& items);
    bool TryTakeItems(security::PlayerId player, std::string_view blueprint, int quantity);
    void TryGivePendingQuestItems(security::PlayerId player);
    void TryUnlockEngrams(security::PlayerId player, const std::vector<std::string>& blueprints);
    void TryGiveBuff(security::PlayerId player, const std::string& blueprint);
#endif
}

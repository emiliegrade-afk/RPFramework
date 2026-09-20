// ============================================================================
// RPFramework - Traduction Item.id → GiveItem ASA
// ============================================================================
#pragma once

#include "Loadout/Item.h"
#include "Security/Types.h"

#include <string_view>
#include <vector>

#ifdef RPFRAMEWORK_TESTS
#include "Asa/BlueprintPath.h"

#include <mutex>
#include <string>
#include <unordered_map>
#endif

namespace rpframework::loadout
{
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

    inline void ClearTestInventory(security::PlayerId player = 0)
    {
        std::lock_guard<std::mutex> lock(TestInventoryMutex());
        if (player == 0) TestInventoryBags().clear();
        else TestInventoryBags().erase(player);
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
        std::lock_guard<std::mutex> lock(TestInventoryMutex());
        for (const auto& item : items)
        {
            const auto bp = TestItemBlueprint(item);
            if (bp.empty()) continue;
            const auto key = asa::BlueprintKey(bp);
            if (key.empty()) continue;
            const int qty = item.quantity > 0 ? item.quantity : 1;
            TestInventoryBags()[player][key] += qty;
            ++given;
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

    inline void TryGivePendingQuestItems(security::PlayerId) {}
    inline void TryUnlockEngrams(security::PlayerId, const std::vector<std::string>&) {}
#else
    // Donne les items dont extras.blueprint (ou id chemin) est renseigné.
    // Les ids sans blueprint sont ignorés (kit RP-only).
    int TryGiveItems(security::PlayerId player, const std::vector<Item>& items);
    bool TryTakeItems(security::PlayerId player, std::string_view blueprint, int quantity);
    void TryGivePendingQuestItems(security::PlayerId player);
    void TryUnlockEngrams(security::PlayerId player, const std::vector<std::string>& blueprints);
#endif
}

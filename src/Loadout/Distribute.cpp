// ============================================================================
// RPFramework - Loadout / Distribute - implémentation
// ============================================================================
#include "Loadout/Distribute.h"
#include "Loadout/AsaDeliver.h"
#include "Loadout/Compose.h"

#include "Character/Registry.h"
#include "Data/PlayerStore.h"
#include "Security/AuditLog.h"
#include "Core/Logger.h"

#include <unordered_set>

namespace rpframework::loadout
{
    using PlayerId = rpframework::security::PlayerId;

    namespace
    {
        // GDD §9 : le kit final combine commun + race + métier + classe.
        // On attend chaque axe dès qu'il existe au moins une définition
        // dans le registre (un serveur sans métiers n'a pas à en exiger).
        bool IsStarterKitReady(const rpframework::data::PlayerData& data)
        {
            if (!character::Registry::ListRaces().empty() && data.race.empty())
                return false;
            if (!character::Registry::ListProfessions().empty() && data.profession.empty())
                return false;
            if (character::Registry::ClassesEnabled()
                && !character::Registry::ListClasses().empty()
                && data.playerClass.empty())
                return false;
            return true;
        }

        std::vector<Item> ItemsFromPending(const std::vector<nlohmann::json>& pending)
        {
            std::vector<Item> out;
            out.reserve(pending.size());
            for (const auto& entry : pending)
            {
                Item item = Item::FromJson(entry);
                if (!item.id.empty())
                    out.push_back(std::move(item));
            }
            return out;
        }

        bool ConfirmPendingDeliveries(rpframework::data::PlayerData& data,
                                      const std::vector<Item>& deliveredAsa)
        {
            std::unordered_multiset<std::string> givenIds;
            givenIds.reserve(deliveredAsa.size());
            for (const auto& item : deliveredAsa)
                givenIds.insert(item.id);

            std::vector<nlohmann::json> remaining;
            for (const auto& entry : data.pendingStarterKit)
            {
                Item item = Item::FromJson(entry);
                if (item.id.empty() || !item.HasAsaBlueprint())
                    continue;
                const auto it = givenIds.find(item.id);
                if (it != givenIds.end())
                {
                    givenIds.erase(it);
                    continue;
                }
                remaining.push_back(entry);
            }
            data.pendingStarterKit = std::move(remaining);
            if (data.pendingStarterKit.empty())
                data.starterKitDelivered = true;
            return rpframework::data::PlayerStore::Save(data);
        }
    }

    Distributor::Result Distributor::GiveStarterKit(PlayerId player)
    {
        Result r;
        std::vector<Item> asaItems;

        {
            rpframework::data::PlayerStore::ExclusiveLock storeLock;
            auto load = rpframework::data::PlayerStore::LoadDetailed(player);
            if (!load.HasData())
            {
                r.status = DistributionStatus::NoProfile;
                rpframework::security::AuditLog::Log("loadout.starter.skipped", player, {
                    {"reason", "no_profile"},
                });
                return r;
            }

            auto data = *load.data;
            if (data.starterKitDelivered && data.pendingStarterKit.empty())
            {
                r.status = DistributionStatus::AlreadyGiven;
                return r;
            }

            if (data.pendingStarterKit.empty())
            {
                if (!IsStarterKitReady(data))
                {
                    r.status = DistributionStatus::NotReady;
                    return r;
                }

                r.items = Composer::ComposeStarterKit(player);
                for (const auto& item : r.items)
                    data.pendingStarterKit.push_back(item.ToJson());

                nlohmann::json itemsJson = nlohmann::json::array();
                for (const auto& it : r.items)
                    itemsJson.push_back(it.ToJson());
                rpframework::security::AuditLog::Log("loadout.starter.distributed", player, {
                    {"item_count",  r.items.size()},
                    {"items",       itemsJson},
                });

                if (!rpframework::data::PlayerStore::Save(data))
                {
                    rpframework::core::LogError(
                        "Loadout: sauvegarde outbox starter echouee pour joueur {}.", player);
                    r.status = DistributionStatus::NoProfile;
                    r.items.clear();
                    return r;
                }
            }
            else
            {
                r.items = ItemsFromPending(data.pendingStarterKit);
            }

            r.status = DistributionStatus::Delivered;
            for (const auto& item : r.items)
            {
                if (item.HasAsaBlueprint())
                    asaItems.push_back(item);
            }
        }

        std::vector<Item> givenAsa;
        givenAsa.reserve(asaItems.size());
        for (const auto& item : asaItems)
        {
            if (TryGiveItems(player, {item}) > 0)
                givenAsa.push_back(item);
        }

        {
            rpframework::data::PlayerStore::ExclusiveLock storeLock;
            auto load = rpframework::data::PlayerStore::LoadDetailed(player);
            if (!load.HasData())
                return r;
            auto data = *load.data;
            if (!ConfirmPendingDeliveries(data, givenAsa))
            {
                rpframework::core::LogError(
                    "Loadout: confirmation outbox starter echouee pour joueur {}.", player);
            }
        }
        return r;
    }

    Distributor::Result Distributor::ForceGiveStarterKit(PlayerId player)
    {
        Result r;
        r.items  = Composer::ComposeStarterKit(player);
        r.status = DistributionStatus::Delivered;

        nlohmann::json itemsJson = nlohmann::json::array();
        for (const auto& it : r.items)
            itemsJson.push_back(it.ToJson());
        rpframework::security::AuditLog::Log("loadout.starter.forced", player, {
            {"item_count",  r.items.size()},
            {"items",       itemsJson},
        });

        {
            rpframework::data::PlayerStore::ExclusiveLock storeLock;
            auto load = rpframework::data::PlayerStore::LoadDetailed(player);
            if (load.HasData())
            {
                auto data = *load.data;
                data.pendingStarterKit.clear();
                for (const auto& item : r.items)
                    data.pendingStarterKit.push_back(item.ToJson());
                if (!rpframework::data::PlayerStore::Save(data))
                {
                    rpframework::core::LogError(
                        "Loadout: sauvegarde outbox forcee echouee pour joueur {}.", player);
                    return r;
                }
            }
        }

        std::vector<Item> givenAsa;
        for (const auto& item : r.items)
        {
            if (item.HasAsaBlueprint() && TryGiveItems(player, {item}) > 0)
                givenAsa.push_back(item);
        }

        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (load.HasData())
        {
            auto data = *load.data;
            ConfirmPendingDeliveries(data, givenAsa);
        }
        return r;
    }
}

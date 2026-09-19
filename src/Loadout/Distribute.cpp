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
    }

    Distributor::Result Distributor::GiveStarterKit(PlayerId player)
    {
        Result r;

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
            if (data.starterKitDelivered)
            {
                r.status = DistributionStatus::AlreadyGiven;
                return r;
            }

            if (!IsStarterKitReady(data))
            {
                r.status = DistributionStatus::NotReady;
                return r;
            }

            r.items  = Composer::ComposeStarterKit(player);
            r.status = DistributionStatus::Delivered;

            nlohmann::json itemsJson = nlohmann::json::array();
            for (const auto& it : r.items)
            {
                itemsJson.push_back(it.ToJson());
            }
            rpframework::security::AuditLog::Log("loadout.starter.distributed", player, {
                {"item_count",  r.items.size()},
                {"items",       itemsJson},
            });

            data.starterKitDelivered = true;
            if (!rpframework::data::PlayerStore::Save(data))
            {
                rpframework::core::LogError("Loadout: sauvegarde flag starterKitDelivered echouee pour joueur {}.", player);
            }
        }

        if (r.status == DistributionStatus::Delivered)
        {
            TryGiveItems(player, r.items);
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
        {
            itemsJson.push_back(it.ToJson());
        }
        rpframework::security::AuditLog::Log("loadout.starter.forced", player, {
            {"item_count",  r.items.size()},
            {"items",       itemsJson},
        });

        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (load.HasData())
        {
            auto data = *load.data;
            data.starterKitDelivered = true;
            rpframework::data::PlayerStore::Save(data);
        }
        TryGiveItems(player, r.items);
        return r;
    }
}

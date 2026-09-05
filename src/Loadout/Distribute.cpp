// ============================================================================
// RPFramework - Loadout / Distribute - implémentation
// ============================================================================
#include "Loadout/Distribute.h"
#include "Loadout/Compose.h"

#include "Data/PlayerStore.h"
#include "Security/AuditLog.h"
#include "Core/Logger.h"

namespace rpframework::loadout
{
    using PlayerId = rpframework::security::PlayerId;
    Distributor::Result Distributor::GiveStarterKit(PlayerId player)
    {
        Result r;

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

        r.items  = Composer::ComposeStarterKit(player);
        r.status = DistributionStatus::Delivered;

        // Sérialise les items pour l'audit (ne pas logguer bcp d'items
        // individuellement : un seul événement avec un array).
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

        // Met à jour le flag (idempotent : déjà à true reste à true).
        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (load.HasData())
        {
            auto data = *load.data;
            data.starterKitDelivered = true;
            rpframework::data::PlayerStore::Save(data);
        }
        return r;
    }
}

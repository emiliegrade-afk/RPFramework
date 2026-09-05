// ============================================================================
// RPFramework - Faction / Reputation - implémentation
// ============================================================================
#include "Faction/Reputation.h"

#include "Faction/Registry.h"

#include "Data/PlayerStore.h"
#include "Core/Logger.h"
#include "Security/AuditLog.h"

#include <algorithm>

namespace rpframework::faction
{
    using PlayerId = rpframework::security::PlayerId;

    // Helper interne : load + save le profil, en loggant les erreurs.
    // Renvoie true si OK.
    static bool LoadAndSave(PlayerId player, rpframework::data::PlayerData& out)
    {
        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            rpframework::core::LogWarn("Faction: profil joueur {} indisponible.", player);
            return false;
        }
        out = *load.data;
        return true;
    }

    int GetReputation(PlayerId player, std::string_view factionId)
    {
        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return 0;
        const auto it = load.data->reputation.find(std::string(factionId));
        if (it == load.data->reputation.end()) return 0;
        return it->second;
    }

    int SetReputation(PlayerId player, std::string_view factionId,
                      int value, std::string_view reason)
    {
        const std::string fid(factionId);
        rpframework::data::PlayerData data;
        if (!LoadAndSave(player, data)) return 0;

        const int before = data.reputation.count(fid) ? data.reputation[fid] : 0;
        data.reputation[fid] = value;
        if (!rpframework::data::PlayerStore::Save(data))
        {
            rpframework::core::LogError("Faction: save profil {} échoué.", player);
            return before;
        }

        const int delta = value - before;
        rpframework::security::AuditLog::Log("faction.reputation.set", player, {
            {"faction", fid},
            {"value",   value},
            {"delta",   delta},
            {"reason",  std::string(reason)},
        });
        return value;
    }

    int ModifyReputation(PlayerId player, std::string_view factionId,
                         int delta, std::string_view reason)
    {
        const std::string fid(factionId);
        rpframework::data::PlayerData data;
        if (!LoadAndSave(player, data)) return 0;

        const int before = data.reputation.count(fid) ? data.reputation[fid] : 0;
        const int after  = before + delta;
        data.reputation[fid] = after;
        if (!rpframework::data::PlayerStore::Save(data))
        {
            rpframework::core::LogError("Faction: save profil {} échoué.", player);
            return before;
        }

        rpframework::security::AuditLog::Log("faction.reputation.modify", player, {
            {"faction", fid},
            {"delta",   delta},
            {"before",  before},
            {"after",   after},
            {"reason",  std::string(reason)},
        });
        return after;
    }

    std::optional<CurrentRank> GetCurrentRank(PlayerId player, std::string_view factionId)
    {
        const std::string fid(factionId);
        const auto f = Registry::GetFaction(fid);
        if (!f) return std::nullopt;

        const int rep = GetReputation(player, fid);

        // Trouve le rang le plus élevé dont min_reputation <= rep.
        // Les rangs sont déjà triés du plus bas au plus haut dans
        // Faction::FromJson (on les ajoute dans l'ordre du JSON).
        const Rank* best = nullptr;
        for (const auto& r : f->ranks)
        {
            if (rep >= r.minReputation)
            {
                best = &r;
            }
        }
        if (!best) return std::nullopt;
        return CurrentRank{ best->id, best->name, best->minReputation };
    }
}

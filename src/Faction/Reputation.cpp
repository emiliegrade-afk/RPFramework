// ============================================================================
// RPFramework - Faction / Reputation - implémentation
// ============================================================================
#include "Faction/Reputation.h"

#include "Faction/Registry.h"

#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Core/Logger.h"
#include "Security/AuditLog.h"

#include <limits>
#include <utility>

namespace rpframework::faction
{
    using PlayerId = rpframework::security::PlayerId;

    namespace
    {
        int CurrentRep(const rpframework::data::PlayerData& data, const std::string& fid)
        {
            const auto it = data.reputation.find(fid);
            return it == data.reputation.end() ? 0 : it->second;
        }

        bool WouldOverflow(int before, int delta)
        {
            if (delta > 0 && before > std::numeric_limits<int>::max() - delta) return true;
            if (delta < 0 && before < std::numeric_limits<int>::min() - delta) return true;
            return false;
        }

        ReputationChange MakeChange(const std::string& fid, int before, int after,
                                    bool primary)
        {
            ReputationChange c;
            c.factionId = fid;
            c.before = before;
            c.after = after;
            c.delta = after - before;
            c.primary = primary;
            const auto from = Registry::ResolveStanding(before);
            const auto to = Registry::ResolveStanding(after);
            if (from) c.fromTier = from->id;
            if (to) c.toTier = to->id;
            return c;
        }

        // Helper interne : load le profil. Renvoie true si OK.
        bool LoadProfile(PlayerId player, rpframework::data::PlayerData& out)
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
    }

    ReputationResult ApplyReputationDelta(data::PlayerData& data,
                                          std::string_view factionId,
                                          int delta,
                                          bool propagate)
    {
        const std::string fid(factionId);
        if (delta == 0)
        {
            ReputationResult r;
            r.message = "ok";
            return r;
        }

        const int before = CurrentRep(data, fid);
        if (WouldOverflow(before, delta))
            return ReputationResult::Fail("reputation overflow");

        struct Planned
        {
            std::string id;
            int before = 0;
            int after = 0;
            bool primary = false;
        };
        std::vector<Planned> planned;
        planned.push_back({fid, before, before + delta, true});

        if (propagate)
        {
            const auto source = Registry::GetFaction(fid);
            if (source)
            {
                for (const auto& [target, type] : source->relations)
                {
                    const auto share = Registry::GetSharePercent(type);
                    if (!share) continue;
                    const int secondary = delta * (*share) / 100;
                    if (secondary == 0) continue;
                    const int tBefore = CurrentRep(data, target);
                    if (WouldOverflow(tBefore, secondary))
                        return ReputationResult::Fail("reputation overflow");
                    planned.push_back({target, tBefore, tBefore + secondary, false});
                }
            }
        }

        ReputationResult result;
        result.message = "ok";
        for (const auto& p : planned)
        {
            data.reputation[p.id] = p.after;
            result.changes.push_back(MakeChange(p.id, p.before, p.after, p.primary));
        }
        return result;
    }

    void AuditReputationChanges(PlayerId player,
                                std::string_view reason,
                                const std::vector<ReputationChange>& changes)
    {
        for (const auto& c : changes)
        {
            rpframework::security::AuditLog::Log("faction.reputation.modify", player, {
                {"faction", c.factionId},
                {"delta",   c.delta},
                {"before",  c.before},
                {"after",   c.after},
                {"primary", c.primary},
                {"reason",  std::string(reason)},
            });
            if (c.fromTier != c.toTier)
            {
                rpframework::security::AuditLog::Log("faction.reputation.tier_changed", player, {
                    {"faction", c.factionId},
                    {"from",    c.fromTier},
                    {"to",      c.toTier},
                    {"after",   c.after},
                    {"reason",  std::string(reason)},
                });
            }
        }
    }

    int GetReputation(PlayerId player, std::string_view factionId)
    {
        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return 0;
        const auto it = load.data->reputation.find(std::string(factionId));
        if (it == load.data->reputation.end()) return 0;
        return it->second;
    }

    std::optional<Standing> ResolveStanding(int value)
    {
        return Registry::ResolveStanding(value);
    }

    std::optional<Standing> GetStanding(PlayerId player, std::string_view factionId)
    {
        return Registry::ResolveStanding(GetReputation(player, factionId));
    }

    std::optional<std::string> GetRelation(std::string_view from, std::string_view to)
    {
        const auto f = Registry::GetFaction(std::string(from));
        if (!f) return std::nullopt;
        const auto it = f->relations.find(std::string(to));
        if (it == f->relations.end()) return std::nullopt;
        return it->second;
    }

    int SetReputation(PlayerId player, std::string_view factionId,
                      int value, std::string_view reason)
    {
        const std::string fid(factionId);
        rpframework::data::PlayerStore::ExclusiveLock storeLock;
        rpframework::data::PlayerData data;
        if (!LoadProfile(player, data)) return 0;

        const int before = CurrentRep(data, fid);
        data.reputation[fid] = value;
        if (!rpframework::data::PlayerStore::Save(data))
        {
            rpframework::core::LogError("Faction: save profil {} échoué.", player);
            return before;
        }

        const auto change = MakeChange(fid, before, value, true);
        rpframework::security::AuditLog::Log("faction.reputation.set", player, {
            {"faction", fid},
            {"value",   value},
            {"delta",   change.delta},
            {"reason",  std::string(reason)},
        });
        if (change.fromTier != change.toTier)
        {
            rpframework::security::AuditLog::Log("faction.reputation.tier_changed", player, {
                {"faction", fid},
                {"from",    change.fromTier},
                {"to",      change.toTier},
                {"after",   value},
                {"reason",  std::string(reason)},
            });
        }
        return value;
    }

    int ModifyReputation(PlayerId player, std::string_view factionId,
                         int delta, std::string_view reason)
    {
        const std::string fid(factionId);
        rpframework::data::PlayerStore::ExclusiveLock storeLock;
        rpframework::data::PlayerData data;
        if (!LoadProfile(player, data)) return 0;

        const int before = CurrentRep(data, fid);
        auto applied = ApplyReputationDelta(data, fid, delta, true);
        if (!applied.ok)
        {
            rpframework::core::LogWarn("Faction: ModifyReputation {} / {} : {}.",
                player, fid, applied.message);
            return before;
        }
        if (!rpframework::data::PlayerStore::Save(data))
        {
            rpframework::core::LogError("Faction: save profil {} échoué.", player);
            return before;
        }
        AuditReputationChanges(player, reason, applied.changes);
        return CurrentRep(data, fid);
    }

    std::optional<CurrentRank> GetCurrentRank(PlayerId player, std::string_view factionId)
    {
        const std::string fid(factionId);
        const auto f = Registry::GetFaction(fid);
        if (!f) return std::nullopt;

        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (!load.HasData() || load.data->faction != fid) return std::nullopt;

        const auto it = load.data->reputation.find(fid);
        const int rep = (it == load.data->reputation.end()) ? 0 : it->second;

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
        return CurrentRank{ best->id, best->name, best->minReputation, best->benefits };
    }
}

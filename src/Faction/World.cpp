// ============================================================================
// RPFramework - Monde : crimes + lieux (E2 / E3)
// ============================================================================
#include "Faction/World.h"

#include "Core/Config.h"
#include "Core/Logger.h"
#include "Faction/Registry.h"
#include "Faction/Reputation.h"
#include "Progression/Professions.h"
#include "Quest/Events.h"
#include "Security/AuditLog.h"

#include <cctype>
#include <mutex>
#include <unordered_map>

namespace rpframework::faction
{
    namespace
    {
        std::mutex g_mutex;
        std::unordered_map<std::string, CrimeDef> g_crimes;
        std::unordered_map<std::string, LocationDef> g_locations;
        bool g_ready = false;

        bool ValidId(const std::string& id)
        {
            if (id.empty() || id.size() > 64) return false;
            for (unsigned char c : id)
            {
                if (!std::isalnum(c) && c != '_' && c != '-') return false;
            }
            return true;
        }

        void LoadCrimesLocked(const nlohmann::json* section)
        {
            g_crimes.clear();
            if (section == nullptr || !section->is_object()) return;
            for (auto it = section->begin(); it != section->end(); ++it)
            {
                if (!ValidId(it.key()) || !it->is_object())
                {
                    rpframework::core::LogWarn("Crime '{}' ignore (id ou objet invalide).", it.key());
                    continue;
                }
                CrimeDef crime;
                crime.id = it.key();
                crime.faction = it->value("faction", std::string{});
                crime.delta = it->value("delta", 0);
                crime.requireWitness = it->value("require_witness", true);
                crime.unseenProfession = it->value("unseen_profession", std::string{});
                crime.unseenXp = it->value("unseen_xp", 0);
                if (crime.faction.empty() && crime.delta != 0)
                {
                    rpframework::core::LogWarn("Crime '{}' ignore (faction vide).", crime.id);
                    continue;
                }
                g_crimes[crime.id] = std::move(crime);
            }
        }

        void LoadLocationsLocked(const nlohmann::json* section)
        {
            g_locations.clear();
            if (section == nullptr || !section->is_object()) return;
            for (auto it = section->begin(); it != section->end(); ++it)
            {
                if (!ValidId(it.key()) || !it->is_object())
                {
                    rpframework::core::LogWarn("Lieu '{}' ignore (id ou objet invalide).", it.key());
                    continue;
                }
                LocationDef loc;
                loc.id = it.key();
                loc.faction = it->value("faction", std::string{});
                loc.minStanding = it->value("min_standing", std::string{});
                if (it->contains("denied_tiers") && (*it)["denied_tiers"].is_array())
                {
                    for (const auto& tier : (*it)["denied_tiers"])
                    {
                        if (tier.is_string() && !tier.get<std::string>().empty())
                            loc.deniedTiers.push_back(tier.get<std::string>());
                    }
                }
                g_locations[loc.id] = std::move(loc);
            }
        }
    }

    void Initialize()
    {
        LoadFromConfig();
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_crimes.clear();
        g_locations.clear();
        g_ready = false;
    }

    void LoadFromConfig()
    {
        const auto& cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* crimes = nullptr;
        const nlohmann::json* locations = nullptr;
        if (cfg.is_object())
        {
            if (cfg.contains("crimes") && cfg["crimes"].is_object())
                crimes = &cfg["crimes"];
            if (cfg.contains("locations") && cfg["locations"].is_object())
                locations = &cfg["locations"];
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        LoadCrimesLocked(crimes);
        LoadLocationsLocked(locations);
        g_ready = true;
    }

    void ResetForTests()
    {
        Shutdown();
    }

    void LoadCrimesFromSection(const nlohmann::json* section)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        LoadCrimesLocked(section);
        g_ready = true;
    }

    void LoadLocationsFromSection(const nlohmann::json* section)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        LoadLocationsLocked(section);
        g_ready = true;
    }

    CrimeResult ReportCrime(PlayerId player, std::string_view crimeId, bool witnessed)
    {
        CrimeResult result;
        CrimeDef crime;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            const auto it = g_crimes.find(std::string(crimeId));
            if (it == g_crimes.end())
            {
                result.message = "crime inconnu";
                return result;
            }
            crime = it->second;
        }

        quest::ReportGameplay(player, "crime", crime.id);

        if (!witnessed && crime.unseenXp > 0 && !crime.unseenProfession.empty())
        {
            progression::AddProfessionXp(player, crime.unseenProfession, crime.unseenXp,
                "crime:" + crime.id);
        }

        const bool applyRep = witnessed || !crime.requireWitness;
        if (applyRep && crime.delta != 0 && !crime.faction.empty())
        {
            ModifyReputation(player, crime.faction, crime.delta, "crime:" + crime.id);
            result.reputationApplied = true;
        }

        if (witnessed)
        {
            security::AuditLog::Log("faction.crime.seen", player, {
                {"crime", crime.id}, {"faction", crime.faction}, {"delta", crime.delta},
            });
            result.message = "crime vu";
        }
        else
        {
            security::AuditLog::Log("faction.crime.unseen", player, {
                {"crime", crime.id}, {"faction", crime.faction},
            });
            result.message = "crime sans temoin";
        }
        result.ok = true;
        return result;
    }

    AccessResult CanEnter(PlayerId player, std::string_view locationId)
    {
        AccessResult result;
        result.allowed = true;
        result.message = "ok";
        LocationDef loc;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            const auto it = g_locations.find(std::string(locationId));
            if (it == g_locations.end())
            {
                result.message = "lieu inconnu";
                return result;
            }
            loc = it->second;
        }

        if (loc.faction.empty())
            return result;

        const auto standing = GetStanding(player, loc.faction);
        if (standing)
            result.standingId = standing->id;

        for (const auto& denied : loc.deniedTiers)
        {
            if (standing && standing->id == denied)
            {
                result.allowed = false;
                result.message = "acces refuse";
                return result;
            }
        }

        if (!loc.minStanding.empty())
        {
            const int rep = GetReputation(player, loc.faction);
            bool found = false;
            for (const auto& tier : Registry::ListStandings())
            {
                if (tier.id != loc.minStanding) continue;
                found = true;
                if (rep < tier.min)
                {
                    result.allowed = false;
                    result.message = "standing insuffisant";
                    return result;
                }
            }
            if (!found)
            {
                result.allowed = false;
                result.message = "palier inconnu";
                return result;
            }
        }
        return result;
    }

    bool IsHostileTo(PlayerId player, std::string_view factionId)
    {
        const auto standing = GetStanding(player, factionId);
        return standing && standing->attackOnSight;
    }
}

// ============================================================================
// RPFramework - Faction / Registry - implémentation
// ============================================================================
#include "Faction/Registry.h"

#include "Core/Config.h"
#include "Core/Logger.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace rpframework::faction
{
    namespace
    {
        bool Overlaps(const Standing& a, const Standing& b)
        {
            return a.min <= b.max && b.min <= a.max;
        }

        std::optional<int> ReadIntField(const nlohmann::json& j, const char* key)
        {
            if (!j.contains(key) || !j[key].is_number_integer()) return std::nullopt;
            const auto v = j[key].get<std::int64_t>();
            if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
                return std::nullopt;
            return static_cast<int>(v);
        }
    }

    Registry& Registry::Instance()
    {
        static Registry inst;
        return inst;
    }

    void Registry::Initialize()
    {
        Instance().LoadFromConfigImpl();
    }

    void Registry::Shutdown()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.factions_.clear();
        self.standings_.clear();
        self.relationEffects_.clear();
        self.initialized_ = false;
    }

    void Registry::LoadFromConfig()
    {
        Instance().LoadFromConfigImpl();
    }

    void Registry::ResetForTests()
    {
        Shutdown();
    }

    void Registry::LoadFromConfigImpl()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        self.factions_.clear();
        self.standings_.clear();
        self.relationEffects_.clear();

        const auto& cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* reputation = nullptr;
        if (cfg.is_object() && cfg.contains("reputation"))
            reputation = &cfg["reputation"];
        self.LoadReputationFromSectionLocked(reputation);

        const nlohmann::json* section = nullptr;
        if (cfg.is_object() && cfg.contains("factions"))
            section = &cfg["factions"];
        LoadDefinitionsFromSection(section);
        self.initialized_ = true;
    }

    void Registry::LoadReputationFromSection(const nlohmann::json* section)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.standings_.clear();
        self.relationEffects_.clear();
        self.LoadReputationFromSectionLocked(section);
        self.SanitizeRelationsLocked();
    }

    void Registry::LoadReputationFromSectionLocked(const nlohmann::json* section)
    {
        auto& self = Instance();
        if (section == nullptr || !section->is_object())
        {
            rpframework::core::LogInfo("Registry(Faction): aucune section 'reputation' ; paliers vides.");
            return;
        }

        if (section->contains("tiers") && (*section)["tiers"].is_array())
        {
            std::unordered_set<std::string> seenIds;
            for (const auto& entry : (*section)["tiers"])
            {
                if (!entry.is_object())
                {
                    rpframework::core::LogError("Registry(Faction): palier invalide (pas un objet).");
                    continue;
                }
                Standing s;
                s.id = entry.value("id", std::string{});
                if (s.id.empty() || seenIds.count(s.id))
                {
                    rpframework::core::LogError("Registry(Faction): palier '{}' rejete (id vide ou duplique).", s.id);
                    continue;
                }
                const auto minV = ReadIntField(entry, "min");
                const auto maxV = ReadIntField(entry, "max");
                if (!minV || !maxV)
                {
                    rpframework::core::LogError("Registry(Faction): palier '{}' rejete (min/max invalides).", s.id);
                    continue;
                }
                s.min = *minV;
                s.max = *maxV;
                if (s.min > s.max)
                {
                    rpframework::core::LogError("Registry(Faction): palier '{}' rejete (min > max).", s.id);
                    continue;
                }
                bool overlap = false;
                for (const auto& existing : self.standings_)
                {
                    if (Overlaps(existing, s))
                    {
                        overlap = true;
                        break;
                    }
                }
                if (overlap)
                {
                    rpframework::core::LogError("Registry(Faction): palier '{}' rejete (chevauchement).", s.id);
                    continue;
                }
                s.name = entry.value("name", s.id);
                s.buyMult = entry.value("buy_mult", 1.0);
                s.sellMult = entry.value("sell_mult", 1.0);
                s.canTrade = entry.value("can_trade", true);
                s.attackOnSight = entry.value("attack_on_sight", false);
                seenIds.insert(s.id);
                self.standings_.push_back(std::move(s));
            }
            std::sort(self.standings_.begin(), self.standings_.end(),
                      [](const Standing& a, const Standing& b) { return a.min < b.min; });
        }

        if (section->contains("relation_effects") && (*section)["relation_effects"].is_object())
        {
            for (auto it = (*section)["relation_effects"].begin();
                 it != (*section)["relation_effects"].end(); ++it)
            {
                if (!it->is_object())
                {
                    rpframework::core::LogError("Registry(Faction): relation_effect '{}' rejete.", it.key());
                    continue;
                }
                const auto share = ReadIntField(*it, "share_percent");
                if (!share || *share < -100 || *share > 100)
                {
                    rpframework::core::LogError(
                        "Registry(Faction): relation_effect '{}' rejete (share_percent).", it.key());
                    continue;
                }
                self.relationEffects_[it.key()] = *share;
            }
        }

        rpframework::core::LogInfo("Registry(Faction): {} paliers, {} types de relation.",
            self.standings_.size(), self.relationEffects_.size());
    }

    void Registry::SanitizeRelationsLocked()
    {
        auto& self = Instance();
        for (auto& [id, faction] : self.factions_)
        {
            std::vector<std::string> drop;
            for (const auto& [target, type] : faction.relations)
            {
                if (target == id)
                {
                    rpframework::core::LogError(
                        "Registry(Faction): relation '{}' -> soi-meme ignoree.", id);
                    drop.push_back(target);
                    continue;
                }
                if (self.factions_.find(target) == self.factions_.end())
                {
                    rpframework::core::LogError(
                        "Registry(Faction): relation '{}' -> '{}' ignoree (cible inconnue).",
                        id, target);
                    drop.push_back(target);
                    continue;
                }
                if (self.relationEffects_.find(type) == self.relationEffects_.end())
                {
                    if (self.relationEffects_.empty())
                        continue;
                    rpframework::core::LogError(
                        "Registry(Faction): relation '{}' -> '{}' ignoree (type '{}').",
                        id, target, type);
                    drop.push_back(target);
                }
            }
            for (const auto& key : drop) faction.relations.erase(key);
        }
    }

    void Registry::LoadDefinitionsFromSection(const nlohmann::json* section)
    {
        auto& self = Instance();
        // mutex_ doit être tenu par LoadFromConfigImpl ; on ne relocke pas ici
        // pour éviter un deadlock sur l'appel d'initialisation.

        if (section == nullptr || !section->is_object())
        {
            rpframework::core::LogInfo("Registry(Faction): aucune section 'factions' dans la config ; registres vides.");
            self.SanitizeRelationsLocked();
            return;
        }

        for (auto it = section->begin(); it != section->end(); ++it)
        {
            try
            {
                Faction f = Faction::FromJson(it.key(), it.value());
                self.factions_.emplace(f.id, std::move(f));
            }
            catch (const std::exception& ex)
            {
                rpframework::core::LogError("Registry(Faction): faction '{}' invalide: {}", it.key(), ex.what());
            }
        }

        self.SanitizeRelationsLocked();
        rpframework::core::LogInfo("Registry(Faction): {} factions chargees.", self.factions_.size());
    }

    // -------------------------------------------------------------------------
    // Accesseurs
    // -------------------------------------------------------------------------

    bool Registry::HasFaction(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.factions_.find(id) != self.factions_.end();
    }

    std::optional<Faction> Registry::GetFaction(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.factions_.find(id);
        if (it == self.factions_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<Faction> Registry::ListFactions()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Faction> out;
        out.reserve(self.factions_.size());
        for (const auto& [_, v] : self.factions_) out.push_back(v);
        return out;
    }

    std::vector<std::string> Registry::ListFactionIds()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<std::string> out;
        out.reserve(self.factions_.size());
        for (const auto& [k, _] : self.factions_) out.push_back(k);
        return out;
    }

    std::optional<Standing> Registry::ResolveStanding(int value)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        const Standing* best = nullptr;
        for (const auto& s : self.standings_)
        {
            if (value >= s.min && value <= s.max)
            {
                if (!best || s.min > best->min) best = &s;
            }
        }
        if (!best) return std::nullopt;
        return *best;
    }

    std::vector<Standing> Registry::ListStandings()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.standings_;
    }

    std::optional<int> Registry::GetSharePercent(std::string_view relationType)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        const auto it = self.relationEffects_.find(std::string(relationType));
        if (it == self.relationEffects_.end()) return std::nullopt;
        return it->second;
    }
}

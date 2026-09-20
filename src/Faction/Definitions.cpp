// ============================================================================
// RPFramework - Faction / Definitions - implémentation
// ============================================================================
#include "Faction/Definitions.h"

#include <algorithm>

namespace rpframework::faction
{
    // -------------------------------------------------------------------------
    // Helpers (dupliqués minimaux depuis Character pour ne pas coupler
    // Faction à l'init-list JSON de Character).
    // -------------------------------------------------------------------------
    namespace
    {
        std::vector<std::string> ReadStringArray(const nlohmann::json& j, const std::string& key)
        {
            std::vector<std::string> out;
            if (!j.is_object() || !j.contains(key)) return out;
            const auto& arr = j[key];
            if (!arr.is_array()) return out;
            for (const auto& s : arr)
            {
                if (s.is_string()) out.push_back(s.get<std::string>());
            }
            return out;
        }

        // `character::` est un alias défini dans Definitions.h.
        character::SelectionCondition ReadJoinCondition(const nlohmann::json& j)
        {
            character::SelectionCondition c;
            if (!j.is_object()) return c;
            c.minLevel = j.value("min_level", 1);
            c.maxLevel = j.value("max_level", std::numeric_limits<int>::max());
            c.requiredRaces        = ReadStringArray(j, "required_races");
            c.excludedRaces        = ReadStringArray(j, "excluded_races");
            c.requiredProfessions  = ReadStringArray(j, "required_professions");
            c.excludedProfessions  = ReadStringArray(j, "excluded_professions");
            c.requiredClasses      = ReadStringArray(j, "required_classes");
            c.excludedClasses      = ReadStringArray(j, "excluded_classes");
            if (j.contains("min_reputation") && j["min_reputation"].is_object())
            {
                for (auto it = j["min_reputation"].begin(); it != j["min_reputation"].end(); ++it)
                {
                    if (it->is_number_integer())
                    {
                        c.minReputation[it.key()] = it->get<int>();
                    }
                }
            }
            return c;
        }
    }

    // -------------------------------------------------------------------------
    // Rank
    // -------------------------------------------------------------------------
    nlohmann::json Rank::ToJson() const
    {
        nlohmann::json j;
        j["id"]             = id;
        j["name"]           = name;
        j["min_reputation"] = minReputation;
        if (!benefits.is_null() && !benefits.empty()) j["benefits"] = benefits;
        return j;
    }

    Rank Rank::FromJson(const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("rank : payload racine n'est pas un objet");
        }
        Rank r;
        r.id            = j.value("id",   std::string{});
        r.name          = j.value("name", r.id);
        r.minReputation = j.value("min_reputation", 0);
        if (j.contains("benefits")) r.benefits = j["benefits"];
        if (r.id.empty())
        {
            throw std::runtime_error("rank : champ 'id' manquant");
        }
        return r;
    }

    // -------------------------------------------------------------------------
    // Faction
    // -------------------------------------------------------------------------
    nlohmann::json Faction::ToJson() const
    {
        nlohmann::json j;
        j["id"]                = id;
        j["name"]              = name;
        j["description"]       = description;
        j["lore"]              = lore;

        nlohmann::json ranksJson = nlohmann::json::array();
        for (const auto& r : ranks) ranksJson.push_back(r.ToJson());
        j["ranks"] = ranksJson;

        j["initial_reputation"]   = initialReputation;
        j["excluded_races"]       = excludedRaces;
        j["excluded_professions"] = excludedProfessions;
        j["excluded_classes"]     = excludedClasses;
        j["starter_quests"]       = starterQuests;
        if (!journal.is_null() && !journal.empty()) j["journal"] = journal;
        if (!relations.empty())
        {
            nlohmann::json rel = nlohmann::json::object();
            for (const auto& [target, type] : relations) rel[target] = type;
            j["relations"] = std::move(rel);
        }
        return j;
    }

    Faction Faction::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("faction '" + idIn + "' : payload racine n'est pas un objet");
        }
        Faction f;
        f.id              = idIn;
        f.name            = j.value("name",        idIn);
        f.description     = j.value("description", std::string{});
        f.lore            = j.value("lore",        std::string{});
        f.initialReputation = j.value("initial_reputation", 0);

        if (j.contains("ranks") && j["ranks"].is_array())
        {
            for (const auto& rj : j["ranks"])
            {
                try
                {
                    f.ranks.push_back(Rank::FromJson(rj));
                }
                catch (const std::exception& ex)
                {
                    // Skip rang invalide mais continue.
                    (void)ex;
                }
            }
        }

        f.excludedRaces        = ReadStringArray(j, "excluded_races");
        f.excludedProfessions  = ReadStringArray(j, "excluded_professions");
        f.excludedClasses      = ReadStringArray(j, "excluded_classes");
        f.starterQuests        = ReadStringArray(j, "starter_quests");
        if (j.contains("journal") && j["journal"].is_object())
            f.journal = j["journal"];
        f.joinCondition        = ReadJoinCondition(j.value("join_condition", nlohmann::json::object()));
        if (j.contains("relations") && j["relations"].is_object())
        {
            for (auto it = j["relations"].begin(); it != j["relations"].end(); ++it)
            {
                if (it->is_string()) f.relations[it.key()] = it->get<std::string>();
            }
        }

        return f;
    }
}

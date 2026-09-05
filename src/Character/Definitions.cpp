// ============================================================================
// RPFramework - Character / Definitions - implémentation
// ============================================================================
#include "Character/Definitions.h"

#include <algorithm>
#include <stdexcept>

namespace rpframework::character
{
    // -------------------------------------------------------------------------
    // StatModifier
    // -------------------------------------------------------------------------

    StatModifier::Op StatModifier::OpFromString(std::string_view s, Op fallback)
    {
        if (s == "add")      return Op::Add;
        if (s == "multiply") return Op::Multiply;
        if (s == "set")      return Op::Set;
        return fallback;
    }

    // -------------------------------------------------------------------------
    // SelectionCondition
    // -------------------------------------------------------------------------

    static bool Contains(const std::vector<std::string>& v, const std::string& s)
    {
        return std::find(v.begin(), v.end(), s) != v.end();
    }

    bool SelectionCondition::IsSatisfiedBy(
        const std::string& currentRace,
        const std::string& currentProfession,
        const std::string& currentClass,
        int                currentLevel,
        const std::unordered_map<std::string, int>& currentReputation) const
    {
        if (currentLevel < minLevel) return false;
        if (currentLevel > maxLevel) return false;

        if (!requiredRaces.empty() && !Contains(requiredRaces, currentRace))       return false;
        if (!excludedRaces.empty() && Contains(excludedRaces, currentRace))         return false;
        if (!requiredProfessions.empty() && !Contains(requiredProfessions, currentProfession)) return false;
        if (!excludedProfessions.empty() && Contains(excludedProfessions, currentProfession))     return false;
        if (!requiredClasses.empty() && !Contains(requiredClasses, currentClass))   return false;
        if (!excludedClasses.empty() && Contains(excludedClasses, currentClass))    return false;

        for (const auto& [faction, minRep] : minReputation)
        {
            const auto it = currentReputation.find(faction);
            const int curRep = (it != currentReputation.end()) ? it->second : 0;
            if (curRep < minRep) return false;
        }
        return true;
    }

    std::string SelectionCondition::DescribeViolation(
        const std::string& currentRace,
        const std::string& currentProfession,
        const std::string& currentClass,
        int                currentLevel,
        const std::unordered_map<std::string, int>& currentReputation) const
    {
        if (currentLevel < minLevel)
            return "niveau trop bas (min=" + std::to_string(minLevel) + ", actuel=" + std::to_string(currentLevel) + ")";
        if (currentLevel > maxLevel)
            return "niveau trop haut (max=" + std::to_string(maxLevel) + ", actuel=" + std::to_string(currentLevel) + ")";

        if (!requiredRaces.empty() && !Contains(requiredRaces, currentRace))
            return "race requise : " + std::string(currentRace.empty() ? "<aucune>" : currentRace) + " non dans la liste autorisée";
        if (!excludedRaces.empty() && Contains(excludedRaces, currentRace))
            return "race " + currentRace + " interdite";
        if (!requiredProfessions.empty() && !Contains(requiredProfessions, currentProfession))
            return "métier requis";
        if (!excludedProfessions.empty() && Contains(excludedProfessions, currentProfession))
            return "métier " + currentProfession + " interdit";
        if (!requiredClasses.empty() && !Contains(requiredClasses, currentClass))
            return "classe requise";
        if (!excludedClasses.empty() && Contains(excludedClasses, currentClass))
            return "classe " + currentClass + " interdite";

        for (const auto& [faction, minRep] : minReputation)
        {
            const auto it = currentReputation.find(faction);
            const int curRep = (it != currentReputation.end()) ? it->second : 0;
            if (curRep < minRep)
                return "réputation insuffisante avec " + faction + " (min=" + std::to_string(minRep) + ", actuel=" + std::to_string(curRep) + ")";
        }
        return "condition non remplie";
    }

    // -------------------------------------------------------------------------
    // Helpers JSON
    // -------------------------------------------------------------------------

    namespace
    {
        std::vector<StatModifier> ReadModifiers(const nlohmann::json& j, const std::string& key)
        {
            std::vector<StatModifier> out;
            if (!j.is_object() || !j.contains(key)) return out;
            const auto& arr = j[key];
            if (!arr.is_array()) return out;
            for (const auto& m : arr)
            {
                if (!m.is_object()) continue;
                StatModifier sm;
                sm.target = m.value("target", std::string{});
                if (m.contains("value") && m["value"].is_number())
                {
                    sm.value = m["value"].get<float>();
                }
                else
                {
                    sm.value = 0.0f;
                }
                sm.op     = StatModifier::OpFromString(
                               m.value("op", std::string{"add"}),
                               StatModifier::Op::Add);
                if (!sm.target.empty())
                {
                    out.push_back(std::move(sm));
                }
            }
            return out;
        }

        nlohmann::json WriteModifiers(const std::vector<StatModifier>& mods)
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& m : mods)
            {
                nlohmann::json entry;
                entry["target"] = m.target;
                entry["value"]  = m.value;
                switch (m.op)
                {
                    case StatModifier::Op::Add:      entry["op"] = "add";      break;
                    case StatModifier::Op::Multiply: entry["op"] = "multiply"; break;
                    case StatModifier::Op::Set:      entry["op"] = "set";      break;
                }
                arr.push_back(entry);
            }
            return arr;
        }

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

        std::unordered_map<std::string, int> ReadReputationMap(const nlohmann::json& j, const std::string& key)
        {
            std::unordered_map<std::string, int> out;
            if (!j.is_object() || !j.contains(key)) return out;
            const auto& m = j[key];
            if (!m.is_object()) return out;
            for (auto it = m.begin(); it != m.end(); ++it)
            {
                if (it->is_number_integer())
                {
                    out[it.key()] = it->get<int>();
                }
            }
            return out;
        }

        SelectionCondition ReadSelectionCondition(const nlohmann::json& j)
        {
            SelectionCondition c;
            if (!j.is_object()) return c;
            c.minLevel = j.value("min_level", 1);
            c.maxLevel = j.value("max_level", std::numeric_limits<int>::max());
            c.requiredRaces        = ReadStringArray(j, "required_races");
            c.excludedRaces        = ReadStringArray(j, "excluded_races");
            c.requiredProfessions  = ReadStringArray(j, "required_professions");
            c.excludedProfessions  = ReadStringArray(j, "excluded_professions");
            c.requiredClasses      = ReadStringArray(j, "required_classes");
            c.excludedClasses      = ReadStringArray(j, "excluded_classes");
            c.minReputation        = ReadReputationMap(j, "min_reputation");
            return c;
        }
    }

    // -------------------------------------------------------------------------
    // Race
    // -------------------------------------------------------------------------

    nlohmann::json Race::ToJson() const
    {
        nlohmann::json j;
        j["id"]                 = id;
        j["name"]               = name;
        j["description"]        = description;
        j["lore"]               = lore;
        j["bonuses"]            = WriteModifiers(bonuses);
        j["maluses"]            = WriteModifiers(maluses);
        j["skills"]             = skills;
        j["restrictions"]       = restrictions;
        j["spawn_zone"]         = spawnZone;
        j["faction"]            = faction;
        j["initial_reputation"] = nlohmann::json::object();
        for (const auto& [k, v] : initialReputation) j["initial_reputation"][k] = v;
        j["starter_equipment"]  = starterEquipment;
        // Note : selectionCondition est reconstruite par le Registry à partir
        // d'un sous-objet, mais on ne l'écrit pas ici pour éviter le cycle.
        return j;
    }

    Race Race::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("race '" + idIn + "' : payload racine n'est pas un objet");
        }
        Race r;
        r.id          = idIn;
        r.name        = j.value("name",        idIn);
        r.description = j.value("description", std::string{});
        r.lore        = j.value("lore",        std::string{});
        r.bonuses     = ReadModifiers(j, "bonuses");
        r.maluses     = ReadModifiers(j, "maluses");
        r.skills      = ReadStringArray(j, "skills");
        r.restrictions = ReadStringArray(j, "restrictions");
        r.spawnZone   = j.value("spawn_zone",  std::string{});
        r.faction     = j.value("faction",     std::string{});
        r.initialReputation = ReadReputationMap(j, "initial_reputation");
        if (j.contains("starter_equipment"))
        {
            r.starterEquipment = j["starter_equipment"];
        }
        r.selectionCondition = ReadSelectionCondition(j.value("selection_condition", nlohmann::json::object()));
        return r;
    }

    // -------------------------------------------------------------------------
    // Profession
    // -------------------------------------------------------------------------

    nlohmann::json Profession::ToJson() const
    {
        nlohmann::json j;
        j["id"]          = id;
        j["name"]        = name;
        j["description"] = description;
        j["lore"]        = lore;
        j["max_level"]   = maxLevel;
        j["xp_per_level"] = xpPerLevel;
        j["bonuses"]     = WriteModifiers(bonuses);
        j["skills"]      = skills;
        j["specializations"] = specializations;
        j["restrictions"]    = restrictions;
        j["starter_equipment"] = starterEquipment;
        j["rewards"]     = rewards;
        return j;
    }

    Profession Profession::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("profession '" + idIn + "' : payload racine n'est pas un objet");
        }
        Profession p;
        p.id          = idIn;
        p.name        = j.value("name",        idIn);
        p.description = j.value("description", std::string{});
        p.lore        = j.value("lore",        std::string{});
        p.maxLevel    = j.value("max_level",   100);
        p.xpPerLevel  = j.value("xp_per_level", 1000);
        if (p.maxLevel < 1) p.maxLevel = 1;
        if (p.xpPerLevel < 1) p.xpPerLevel = 1;
        p.bonuses        = ReadModifiers(j, "bonuses");
        p.skills         = ReadStringArray(j, "skills");
        p.specializations = ReadStringArray(j, "specializations");
        p.restrictions    = ReadStringArray(j, "restrictions");
        if (j.contains("starter_equipment")) p.starterEquipment = j["starter_equipment"];
        if (j.contains("rewards"))          p.rewards          = j["rewards"];
        p.selectionCondition = ReadSelectionCondition(j.value("selection_condition", nlohmann::json::object()));
        return p;
    }

    // -------------------------------------------------------------------------
    // CharClass
    // -------------------------------------------------------------------------

    nlohmann::json CharClass::ToJson() const
    {
        nlohmann::json j;
        j["id"]          = id;
        j["name"]        = name;
        j["description"] = description;
        j["lore"]        = lore;
        j["bonuses"]     = WriteModifiers(bonuses);
        j["maluses"]     = WriteModifiers(maluses);
        j["restrictions"] = restrictions;
        j["starter_equipment"] = starterEquipment;
        return j;
    }

    CharClass CharClass::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("class '" + idIn + "' : payload racine n'est pas un objet");
        }
        CharClass c;
        c.id          = idIn;
        c.name        = j.value("name",        idIn);
        c.description = j.value("description", std::string{});
        c.lore        = j.value("lore",        std::string{});
        c.bonuses     = ReadModifiers(j, "bonuses");
        c.maluses     = ReadModifiers(j, "maluses");
        c.restrictions = ReadStringArray(j, "restrictions");
        if (j.contains("starter_equipment")) c.starterEquipment = j["starter_equipment"];
        c.selectionCondition = ReadSelectionCondition(j.value("selection_condition", nlohmann::json::object()));
        return c;
    }
}

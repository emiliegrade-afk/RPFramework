// ============================================================================
// RPFramework - Effects / Definitions - implémentation
// ============================================================================
#include "Effects/Definitions.h"

#include <limits>
#include <stdexcept>

namespace rpframework::effects
{
    bool EffectTypeFromString(std::string_view s, EffectType& out)
    {
        if (s == "buff")   { out = EffectType::Buff;   return true; }
        if (s == "debuff") { out = EffectType::Debuff; return true; }
        return false;
    }

    bool StackingModeFromString(std::string_view s, StackingMode& out)
    {
        if (s == "none")    { out = StackingMode::None;    return true; }
        if (s == "refresh") { out = StackingMode::Refresh; return true; }
        if (s == "stack")   { out = StackingMode::Stack;   return true; }
        return false;
    }

    const char* ToString(EffectType t)
    {
        switch (t)
        {
            case EffectType::Buff:   return "buff";
            case EffectType::Debuff: return "debuff";
        }
        return "buff";
    }

    const char* ToString(StackingMode m)
    {
        switch (m)
        {
            case StackingMode::None:    return "none";
            case StackingMode::Refresh: return "refresh";
            case StackingMode::Stack:   return "stack";
        }
        return "refresh";
    }

    // -------------------------------------------------------------------------
    // Helpers JSON (dupliqués depuis Character : on ne couple pas Effects
    // aux helpers internes de Character/Definitions.cpp).
    // -------------------------------------------------------------------------
    namespace
    {
        std::vector<character::StatModifier> ReadModifiers(const nlohmann::json& j,
                                                           const std::string& key)
        {
            std::vector<character::StatModifier> out;
            if (!j.is_object() || !j.contains(key)) return out;
            const auto& arr = j[key];
            if (!arr.is_array()) return out;
            for (const auto& m : arr)
            {
                if (!m.is_object()) continue;
                character::StatModifier sm;
                sm.target = m.value("target", std::string{});
                if (m.contains("value") && m["value"].is_number())
                {
                    sm.value = m["value"].get<float>();
                }
                else
                {
                    sm.value = 0.0f;
                }
                sm.op = character::StatModifier::OpFromString(
                            m.value("op", std::string{"add"}),
                            character::StatModifier::Op::Add);
                if (!sm.target.empty())
                {
                    out.push_back(std::move(sm));
                }
            }
            return out;
        }

        nlohmann::json WriteModifiers(const std::vector<character::StatModifier>& mods)
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& m : mods)
            {
                nlohmann::json entry;
                entry["target"] = m.target;
                entry["value"]  = m.value;
                switch (m.op)
                {
                    case character::StatModifier::Op::Add:      entry["op"] = "add";      break;
                    case character::StatModifier::Op::Multiply: entry["op"] = "multiply"; break;
                    case character::StatModifier::Op::Set:      entry["op"] = "set";      break;
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

        character::SelectionCondition ReadConditions(const nlohmann::json& j)
        {
            character::SelectionCondition c;
            if (!j.is_object()) return c;
            c.minLevel = j.value("min_level", 1);
            c.maxLevel = j.value("max_level", std::numeric_limits<int>::max());
            c.requiredRaces       = ReadStringArray(j, "required_races");
            c.excludedRaces       = ReadStringArray(j, "excluded_races");
            c.requiredProfessions = ReadStringArray(j, "required_professions");
            c.excludedProfessions = ReadStringArray(j, "excluded_professions");
            c.requiredClasses     = ReadStringArray(j, "required_classes");
            c.excludedClasses     = ReadStringArray(j, "excluded_classes");
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

        nlohmann::json WriteConditions(const character::SelectionCondition& c)
        {
            nlohmann::json j = nlohmann::json::object();
            j["min_level"] = c.minLevel;
            if (c.maxLevel != std::numeric_limits<int>::max())
            {
                j["max_level"] = c.maxLevel;
            }
            if (!c.requiredRaces.empty())       j["required_races"] = c.requiredRaces;
            if (!c.excludedRaces.empty())       j["excluded_races"] = c.excludedRaces;
            if (!c.requiredProfessions.empty()) j["required_professions"] = c.requiredProfessions;
            if (!c.excludedProfessions.empty()) j["excluded_professions"] = c.excludedProfessions;
            if (!c.requiredClasses.empty())     j["required_classes"] = c.requiredClasses;
            if (!c.excludedClasses.empty())     j["excluded_classes"] = c.excludedClasses;
            if (!c.minReputation.empty())
            {
                nlohmann::json rep = nlohmann::json::object();
                for (const auto& [k, v] : c.minReputation) rep[k] = v;
                j["min_reputation"] = std::move(rep);
            }
            return j;
        }
    }

    nlohmann::json Effect::ToJson() const
    {
        nlohmann::json j;
        j["id"]             = id;
        j["name"]           = name;
        j["type"]           = ToString(type);
        j["buff_blueprint"] = buffBlueprint;
        j["duration_sec"]   = durationSeconds;
        j["stacking"]       = ToString(stacking);
        j["max_stacks"]     = maxStacks;
        j["cooldown_sec"]   = cooldownSeconds;
        j["modifiers"]      = WriteModifiers(modifiers);
        j["conditions"]     = WriteConditions(conditions);
        return j;
    }

    Effect Effect::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("effet '" + idIn + "' : payload racine n'est pas un objet");
        }
        if (idIn.empty())
        {
            throw std::runtime_error("effet : id vide");
        }

        Effect e;
        e.id   = idIn;
        e.name = j.value("name", idIn);

        if (!j.contains("type") || !j["type"].is_string()
            || !EffectTypeFromString(j["type"].get<std::string>(), e.type))
        {
            throw std::runtime_error("effet '" + idIn + "' : type invalide (attendu buff|debuff)");
        }

        e.buffBlueprint = j.value("buff_blueprint", std::string{});

        e.durationSeconds = j.value("duration_sec", 0);
        if (e.durationSeconds < 0)
        {
            throw std::runtime_error("effet '" + idIn + "' : duration_sec < 0");
        }

        if (j.contains("stacking"))
        {
            if (!j["stacking"].is_string()
                || !StackingModeFromString(j["stacking"].get<std::string>(), e.stacking))
            {
                throw std::runtime_error(
                    "effet '" + idIn + "' : stacking invalide (attendu none|refresh|stack)");
            }
        }

        e.maxStacks = j.value("max_stacks", 1);
        if (e.maxStacks < 1)
        {
            throw std::runtime_error("effet '" + idIn + "' : max_stacks < 1");
        }

        e.cooldownSeconds = j.value("cooldown_sec", 0);
        if (e.cooldownSeconds < 0)
        {
            throw std::runtime_error("effet '" + idIn + "' : cooldown_sec < 0");
        }

        e.modifiers  = ReadModifiers(j, "modifiers");
        e.conditions = ReadConditions(j.value("conditions", nlohmann::json::object()));

        if (e.modifiers.empty() && e.buffBlueprint.empty())
        {
            throw std::runtime_error(
                "effet '" + idIn + "' : aucun modifier ni buff_blueprint");
        }

        return e;
    }
}

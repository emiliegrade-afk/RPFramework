// ============================================================================
// RPFramework - Quest / Definitions - implémentation
// ============================================================================
#include "Quest/Definitions.h"

#include <stdexcept>
#include <limits>
#include <unordered_set>

namespace rpframework::quest
{
    namespace
    {
        std::vector<std::string> ReadStrings(const nlohmann::json& j, const char* key)
        {
            std::vector<std::string> values;
            if (!j.is_object() || !j.contains(key) || !j[key].is_array()) return values;
            for (const auto& item : j[key])
                if (item.is_string()) values.push_back(item.get<std::string>());
            return values;
        }

        character::SelectionCondition ReadCondition(const nlohmann::json& j)
        {
            character::SelectionCondition condition;
            if (!j.is_object()) return condition;
            condition.minLevel = j.value("min_level", 1);
            condition.maxLevel = j.value("max_level", std::numeric_limits<int>::max());
            condition.requiredRaces = ReadStrings(j, "required_races");
            condition.excludedRaces = ReadStrings(j, "excluded_races");
            condition.requiredProfessions = ReadStrings(j, "required_professions");
            condition.excludedProfessions = ReadStrings(j, "excluded_professions");
            condition.requiredClasses = ReadStrings(j, "required_classes");
            condition.excludedClasses = ReadStrings(j, "excluded_classes");
            if (j.contains("min_reputation") && j["min_reputation"].is_object())
                for (auto it = j["min_reputation"].begin(); it != j["min_reputation"].end(); ++it)
                    if (it->is_number_integer()) condition.minReputation[it.key()] = it->get<int>();
            return condition;
        }
    }

    nlohmann::json Objective::ToJson() const
    {
        nlohmann::json j = nlohmann::json::object();
        j["id"] = id;
        j["type"] = type;
        j["target"] = target;
        j["entity"] = entity;
        j["required"] = required;
        j["time_limit_sec"] = timeLimitSeconds;
        return j;
    }

    Objective Objective::FromJson(const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("objective invalide : payload non objet");
        }

        Objective out;
        out.id = j.value("id", std::string{});
        out.type = j.value("type", std::string{});
        out.target = j.value("target", 0);
        out.entity = j.value("entity", std::string{});
        out.required = j.value("required", true);
        out.timeLimitSeconds = j.value("time_limit_sec", 0);
        if (out.target <= 0 || out.timeLimitSeconds < 0)
            throw std::runtime_error("objectif invalide : target/time_limit_sec");
        return out;
    }

    nlohmann::json Reward::ToJson() const
    {
        nlohmann::json j = nlohmann::json::object();
        j["type"] = type;
        j["id"] = id;
        j["amount"] = amount;
        if (!payload.is_null())
        {
            j["payload"] = payload;
        }
        return j;
    }

    Reward Reward::FromJson(const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("reward invalide : payload non objet");
        }

        Reward out;
        out.type = j.value("type", std::string{});
        out.id = j.value("id", std::string{});
        out.amount = j.value("amount", 0);
        if (j.contains("payload"))
        {
            out.payload = j["payload"];
        }
        return out;
    }

    nlohmann::json Quest::ToJson() const
    {
        nlohmann::json j = nlohmann::json::object();
        j["id"] = id;
        j["name"] = name;
        j["description"] = description;
        j["lore"] = lore;
        j["source"] = source;
        j["min_level"] = minLevel;
        j["prerequisites"] = prerequisites;
        j["category"] = category;
        j["repeatable"] = repeatable;
        j["objective_mode"] = objectiveMode;

        nlohmann::json objs = nlohmann::json::array();
        for (const auto& o : objectives)
            objs.push_back(o.ToJson());
        j["objectives"] = objs;

        nlohmann::json rews = nlohmann::json::array();
        for (const auto& r : rewards)
            rews.push_back(r.ToJson());
        j["rewards"] = rews;
        return j;
    }

    Quest Quest::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("quest '" + idIn + "' : payload non objet");
        }

        Quest q;
        q.id = idIn;
        q.name = j.value("name", idIn);
        q.description = j.value("description", std::string{});
        q.lore = j.value("lore", std::string{});
        q.source = j.value("source", std::string{});
        q.minLevel = j.value("min_level", 1);
        if (j.contains("prerequisites") && j["prerequisites"].is_array())
        {
            for (const auto& item : j["prerequisites"])
                if (item.is_string()) q.prerequisites.push_back(item.get<std::string>());
        }
        q.category = j.value("category", std::string{});
        q.repeatable = j.value("repeatable", false);
        q.objectiveMode = j.value("objective_mode", std::string("all"));
        if (q.objectiveMode != "all" && q.objectiveMode != "any")
            throw std::runtime_error("quest '" + idIn + "' : objective_mode invalide");
        q.condition = ReadCondition(j.value("selection_condition", nlohmann::json::object()));

        if (j.contains("objectives") && j["objectives"].is_array())
        {
            for (const auto& item : j["objectives"])
            q.objectives.push_back(Objective::FromJson(item));
        }

        std::unordered_set<std::string> objectiveIds;
        for (const auto& objective : q.objectives)
        {
            if (objective.id.empty() || objective.type.empty() || objective.entity.empty()
                || !objectiveIds.insert(objective.id).second)
                throw std::runtime_error("quest '" + idIn + "' : objectif invalide ou duplique");
        }

        if (j.contains("rewards") && j["rewards"].is_array())
        {
            for (const auto& item : j["rewards"])
                q.rewards.push_back(Reward::FromJson(item));
        }

        return q;
    }
}

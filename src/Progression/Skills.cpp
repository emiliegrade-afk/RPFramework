// ============================================================================
// RPFramework - Progression / Compétences - implémentation
// ============================================================================
#include "Progression/Skills.h"

#include "Character/Registry.h"
#include "Core/Config.h"
#include "Core/Logger.h"
#include "Crafting/Pipeline.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Effects/Apply.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace rpframework::progression
{
    namespace
    {
        std::mutex g_mutex;
        std::unordered_map<std::string, SkillDef> g_skills;

        std::vector<std::string> ReadStringArray(const nlohmann::json& j, const char* key)
        {
            std::vector<std::string> out;
            if (!j.contains(key) || !j[key].is_array()) return out;
            for (const auto& v : j[key])
            {
                if (v.is_string()) out.push_back(v.get<std::string>());
            }
            return out;
        }

        SkillDef SkillFromJson(const std::string& id, const nlohmann::json& j)
        {
            SkillDef def;
            def.id = id;
            def.name = j.value("name", id);
            def.profession = j.value("profession", std::string{});
            def.cost = j.value("cost", 1);
            def.minLevel = j.value("min_level", 1);
            def.requiredSkills = ReadStringArray(j, "requires");
            def.unlockRecipes = ReadStringArray(j, "unlock_recipes");
            def.effectId = j.value("effect", std::string{});
            if (def.effectId.empty())
                def.effectId = j.value("effect_id", std::string{});
            if (def.id.empty() || def.profession.empty() || def.cost < 1 || def.minLevel < 1)
                throw std::runtime_error("competence invalide : " + id);
            return def;
        }

        void LoadSectionLocked(const nlohmann::json* section)
        {
            g_skills.clear();
            if (section == nullptr || !section->is_object()) return;
            for (auto it = section->begin(); it != section->end(); ++it)
            {
                try
                {
                    auto def = SkillFromJson(it.key(), it.value());
                    g_skills.emplace(def.id, std::move(def));
                }
                catch (const std::exception& ex)
                {
                    rpframework::core::LogError("Skills: {}", ex.what());
                }
            }
        }
    }

    void LoadSkills()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto cfg = core::Config::Get().Root();
        const nlohmann::json* section = nullptr;
        if (cfg.is_object() && cfg.contains("skills") && cfg["skills"].is_object())
            section = &cfg["skills"];
        LoadSectionLocked(section);
    }

    void LoadSkillsFromSection(const nlohmann::json* section)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        LoadSectionLocked(section);
    }

    void ResetSkillsForTests()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_skills.clear();
    }

    std::vector<SkillDef> ListSkills()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<SkillDef> out;
        out.reserve(g_skills.size());
        for (const auto& [_, def] : g_skills) out.push_back(def);
        std::sort(out.begin(), out.end(),
                  [](const SkillDef& a, const SkillDef& b) { return a.id < b.id; });
        return out;
    }

    std::vector<SkillDef> ListSkillsForProfession(std::string_view professionId)
    {
        std::vector<SkillDef> out;
        for (const auto& def : ListSkills())
        {
            if (def.profession == professionId) out.push_back(def);
        }
        return out;
    }

    std::optional<SkillDef> GetSkill(std::string_view skillId)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_skills.find(std::string(skillId));
        if (it == g_skills.end()) return std::nullopt;
        return it->second;
    }

    SkillResult UnlockSkill(PlayerId player, std::string_view skillId)
    {
        using namespace rpframework::security;
        SkillResult result;
        const std::string id{skillId};

        if (!Permissions::CheckFor(player, "skill.unlock"))
        {
            AuditLog::LogDenied("skill.unlock", player, "permission", {{"skill", id}});
            result.message = "permission refusee";
            return result;
        }
        if (!RateLimiter::Allow(player, "skill.unlock"))
        {
            AuditLog::LogDenied("skill.unlock", player, "rate_limit", {{"skill", id}});
            result.message = "rate limit atteint";
            return result;
        }

        const auto def = GetSkill(id);
        if (!def)
        {
            result.message = "competence inconnue";
            return result;
        }

        data::PlayerStore::ExclusiveLock storeLock;
        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            result.message = "profil indisponible";
            return result;
        }
        auto data = *load.data;
        if (data.profession != def->profession)
        {
            result.message = "mauvais metier";
            return result;
        }

        auto& prog = data.professions[def->profession];
        if (prog.professionId.empty()) prog.professionId = def->profession;
        if (prog.level < def->minLevel)
        {
            result.message = "niveau insuffisant";
            return result;
        }
        if (std::find(prog.unlockedSkills.begin(), prog.unlockedSkills.end(), id)
            != prog.unlockedSkills.end())
        {
            result.message = "deja debloquee";
            return result;
        }
        for (const auto& req : def->requiredSkills)
        {
            if (std::find(prog.unlockedSkills.begin(), prog.unlockedSkills.end(), req)
                == prog.unlockedSkills.end())
            {
                result.message = "prerequis manquant : " + req;
                return result;
            }
        }
        if (prog.skillPoints < def->cost)
        {
            result.message = "points insuffisants";
            result.remainingPoints = prog.skillPoints;
            return result;
        }

        prog.skillPoints -= def->cost;
        prog.unlockedSkills.push_back(id);
        for (const auto& recipe : def->unlockRecipes)
        {
            if (std::find(prog.unlockedRecipes.begin(), prog.unlockedRecipes.end(), recipe)
                == prog.unlockedRecipes.end())
            {
                prog.unlockedRecipes.push_back(recipe);
            }
        }

        if (!data::PlayerStore::Save(data))
        {
            result.message = "sauvegarde echouee";
            return result;
        }

        crafting::GrantAccessibleEngrams(player);
        if (!def->effectId.empty())
            effects::Apply(player, def->effectId);

        AuditLog::Log("skill.unlock", player, {
            {"skill", id},
            {"profession", def->profession},
            {"remaining", prog.skillPoints},
        });

        result.ok = true;
        result.remainingPoints = prog.skillPoints;
        result.message = "competence debloquee : " + def->name;
        return result;
    }
}

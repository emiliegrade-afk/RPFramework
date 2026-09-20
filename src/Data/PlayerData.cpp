// ============================================================================
// RPFramework - Data / PlayerData - implémentation
// ============================================================================
#include "Data/PlayerData.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace rpframework::data
{
    std::string PlayerData::FormatTime(std::chrono::system_clock::time_point tp)
    {
        const auto t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_utc{};
#if defined(_WIN32)
        gmtime_s(&tm_utc, &t);
#else
        gmtime_r(&t, &tm_utc);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    std::chrono::system_clock::time_point PlayerData::ParseTime(const std::string& iso)
    {
        std::tm tm_utc{};
        std::istringstream iss(iso);
        iss >> std::get_time(&tm_utc, "%Y-%m-%dT%H:%M:%S");
        if (iss.fail())
        {
            return std::chrono::system_clock::time_point{};
        }
#if defined(_WIN32)
        return std::chrono::system_clock::from_time_t(_mkgmtime(&tm_utc));
#else
        return std::chrono::system_clock::from_time_t(timegm(&tm_utc));
#endif
    }

    nlohmann::json PlayerData::ToJson() const
    {
        nlohmann::json j;

        // meta
        j["meta"] = {
            {"schema_version", schemaVersion},
            {"created_at",     FormatTime(createdAt)},
            {"updated_at",     FormatTime(updatedAt)},
        };

        // identity
        nlohmann::json identity = {
            {"id", std::to_string(id)},
        };
        if (!name.empty()) identity["name"] = name;
        j["identity"] = identity;

        // character
        nlohmann::json character = nlohmann::json::object();
        if (!race.empty())         character["race"]      = race;
        if (!profession.empty())   character["profession"] = profession;
        if (!playerClass.empty())  character["class"]     = playerClass;
        if (!faction.empty())      character["faction"]   = faction;
        if (spawnApplied)          character["spawn_applied"] = true;
        j["character"] = character;

        // progression
        j["progression"] = {
            {"level", level},
            {"xp",    xp},
        };

        // professions (schéma v4, GDD §38). Omise si vide, comme
        // reputation / titles.
        if (!professions.empty())
        {
            nlohmann::json profs = nlohmann::json::object();
            for (const auto& [id, prog] : professions)
            {
                profs[id] = prog.ToJson();
            }
            j["professions"] = std::move(profs);
        }

        // reputation
        if (!reputation.empty())
        {
            nlohmann::json rep = nlohmann::json::object();
            for (const auto& [k, v] : reputation)
            {
                rep[k] = v;
            }
            j["reputation"] = rep;
        }

        // economy (Phase 6)
        if (!wallets.empty())
        {
            nlohmann::json wall = nlohmann::json::object();
            for (const auto& [k, v] : wallets)
            {
                wall[k] = v;
            }
            j["economy"] = wall;
        }

        if (!quests.empty())
        {
            nlohmann::json questData = nlohmann::json::object();
            for (const auto& [questId, progress] : quests)
            {
                nlohmann::json entry = {
                    {"status", static_cast<int>(progress.status)},
                    {"rewards_granted", progress.rewardsGranted},
                    {"started_at", progress.startedAt},
                    {"objectives", progress.objectives},
                };
                if (!progress.pendingItemRewards.empty())
                    entry["pending_item_rewards"] = progress.pendingItemRewards;
                questData[questId] = std::move(entry);
            }
            j["quests"] = std::move(questData);
        }

        // titles
        if (!titles.empty())
        {
            j["titles"] = titles;
        }

        // unlocks (Phase 7). Récompenses de quête de type "unlock" (recettes,
        // zones, fonctionnalités). Distinct des titres purement affichés.
        if (!unlocks.empty())
        {
            j["unlocks"] = unlocks;
        }

        // loadout
        nlohmann::json loadout = {
            {"starter_kit_delivered", starterKitDelivered},
        };
        if (!pendingStarterKit.empty())
            loadout["pending_starter_kit"] = pendingStarterKit;
        j["loadout"] = std::move(loadout);

        if (!effectCooldowns.empty() || !activeEffects.empty())
        {
            nlohmann::json fx = nlohmann::json::object();
            if (!effectCooldowns.empty())
            {
                nlohmann::json cds = nlohmann::json::object();
                for (const auto& [k, v] : effectCooldowns) cds[k] = v;
                fx["cooldowns"] = std::move(cds);
            }
            if (!activeEffects.empty())
                fx["active"] = activeEffects;
            j["effects"] = std::move(fx);
        }

        return j;
    }

    PlayerData PlayerData::FromJson(const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("racine JSON joueur invalide");
        }

        PlayerData d;

        // meta (optionnel pour rétro-compat)
        if (j.contains("meta") && j["meta"].is_object())
        {
            const auto& meta = j["meta"];
            d.schemaVersion = meta.value("schema_version", kPlayerDataSchemaVersion);
            if (meta.contains("created_at") && meta["created_at"].is_string())
            {
                d.createdAt = ParseTime(meta["created_at"].get<std::string>());
            }
            if (meta.contains("updated_at") && meta["updated_at"].is_string())
            {
                d.updatedAt = ParseTime(meta["updated_at"].get<std::string>());
            }
        }

        // identity : indispensable. Un fichier dont l'identité est invalide
        // ne doit jamais être interprété comme le profil du joueur 0.
        if (!j.contains("identity") || !j["identity"].is_object())
        {
            throw std::runtime_error("identity manquante ou invalide");
        }
        const auto& id = j["identity"];
        if (!id.contains("id") || !id["id"].is_string())
        {
            throw std::runtime_error("identity.id manquant ou invalide");
        }
        const std::string idText = id["id"].get<std::string>();
        std::size_t consumed = 0;
        unsigned long long parsedId = 0;
        try
        {
            parsedId = std::stoull(idText, &consumed);
        }
        catch (...)
        {
            throw std::runtime_error("identity.id non numerique");
        }
        if (consumed != idText.size() || parsedId == 0
            || parsedId > std::numeric_limits<PlayerId>::max())
        {
            throw std::runtime_error("identity.id hors limites");
        }
        d.id = static_cast<PlayerId>(parsedId);
        d.name = id.value("name", std::string{});

        // character
        if (j.contains("character") && j["character"].is_object())
        {
            const auto& c = j["character"];
            d.race        = c.value("race",       std::string{});
            d.profession  = c.value("profession", std::string{});
            d.playerClass = c.value("class",      std::string{});
            d.faction     = c.value("faction",    std::string{});
            d.spawnApplied = c.value("spawn_applied", false);
        }

        // progression
        if (j.contains("progression") && j["progression"].is_object())
        {
            const auto& p = j["progression"];
            d.level = p.value("level", 1);
            d.xp    = p.value("xp",    0);
            if (d.level < 1 || d.xp < 0)
            {
                throw std::runtime_error("progression invalide");
            }
        }

        // professions (schéma v4). Absente = map vide (fichier v3 ou
        // joueur sans métier). Une entrée corrompue ou des valeurs
        // négatives rejettent tout le profil, comme `progression`.
        if (j.contains("professions") && j["professions"].is_object())
        {
            for (auto it = j["professions"].begin(); it != j["professions"].end(); ++it)
            {
                if (it.key().empty())
                {
                    continue;
                }
                d.professions[it.key()] = ProfessionProgression::FromJson(it.key(), *it);
            }
        }

        // reputation
        if (j.contains("reputation") && j["reputation"].is_object())
        {
            for (auto it = j["reputation"].begin(); it != j["reputation"].end(); ++it)
            {
                if (it->is_number_integer())
                {
                    d.reputation[it.key()] = it->get<int>();
                }
            }
        }

        // economy (Phase 6). Si la section manque, wallets reste vide.
        if (j.contains("economy") && j["economy"].is_object())
        {
            for (auto it = j["economy"].begin(); it != j["economy"].end(); ++it)
            {
                if (it->is_number_integer())
                {
                    const auto value = it->get<std::int64_t>();
                    if (value < 0)
                    {
                        throw std::runtime_error("solde negatif");
                    }
                    d.wallets[it.key()] = value;
                }
                else if (it->is_number_unsigned())
                {
                    const auto value = it->get<std::uint64_t>();
                    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                    {
                        throw std::runtime_error("solde hors limites");
                    }
                    d.wallets[it.key()] = static_cast<std::int64_t>(value);
                }
            }
        }

        if (j.contains("quests") && j["quests"].is_object())
        {
            for (auto it = j["quests"].begin(); it != j["quests"].end(); ++it)
            {
                if (!it->is_object()) continue;
                QuestProgress progress;
                const auto& entry = *it;
                const int status = entry.value("status", 0);
                if (status == static_cast<int>(QuestProgress::Status::Completed))
                    progress.status = QuestProgress::Status::Completed;
                progress.rewardsGranted = entry.value("rewards_granted", false);
                progress.startedAt = entry.value("started_at", static_cast<std::int64_t>(0));
                if (entry.contains("pending_item_rewards") && entry["pending_item_rewards"].is_array())
                    for (const auto& item : entry["pending_item_rewards"])
                        progress.pendingItemRewards.push_back(item);
                if (entry.contains("objectives") && entry["objectives"].is_object())
                {
                    for (auto objective = entry["objectives"].begin();
                         objective != entry["objectives"].end(); ++objective)
                    {
                        if (objective->is_number_integer())
                            progress.objectives[objective.key()] = objective->get<int>();
                    }
                }
                d.quests[it.key()] = std::move(progress);
            }
        }

        // titles
        if (j.contains("titles") && j["titles"].is_array())
        {
            for (const auto& t : j["titles"])
            {
                if (t.is_string())
                {
                    d.titles.push_back(t.get<std::string>());
                }
            }
        }

        // unlocks (Phase 7). Migration retro-compatible : si la section
        // manque, le vecteur reste vide. On ne lit pas les anciens
        // préfixes "unlock:" éventuellement stockés dans `titles` pour
        // ne pas doubler les entrées — un upgrade manuel côté admin peut
        // être nécessaire si vous aviez déjà délivré des unlocks avant
        // la migration v2→v3.
        if (j.contains("unlocks") && j["unlocks"].is_array())
        {
            for (const auto& u : j["unlocks"])
            {
                if (u.is_string())
                {
                    d.unlocks.push_back(u.get<std::string>());
                }
            }
        }

        // loadout : starterKitDelivered (Phase 4b)
        if (j.contains("loadout") && j["loadout"].is_object())
        {
            const auto& l = j["loadout"];
            d.starterKitDelivered = l.value("starter_kit_delivered", false);
            if (l.contains("pending_starter_kit") && l["pending_starter_kit"].is_array())
            {
                for (const auto& entry : l["pending_starter_kit"])
                {
                    if (entry.is_object())
                        d.pendingStarterKit.push_back(entry);
                }
            }
        }

        if (j.contains("effects") && j["effects"].is_object())
        {
            const auto& fx = j["effects"];
            if (fx.contains("cooldowns") && fx["cooldowns"].is_object())
            {
                for (auto it = fx["cooldowns"].begin(); it != fx["cooldowns"].end(); ++it)
                {
                    if (it->is_number_integer())
                        d.effectCooldowns[it.key()] = it->get<std::int64_t>();
                }
            }
            if (fx.contains("active") && fx["active"].is_array())
            {
                for (const auto& id : fx["active"])
                {
                    if (id.is_string()) d.activeEffects.push_back(id.get<std::string>());
                }
            }
        }

        return d;
    }

    nlohmann::json ProfessionProgression::ToJson() const
    {
        nlohmann::json j = {
            {"level",        level},
            {"xp",           xp},
            {"skill_points", skillPoints},
        };
        if (!professionId.empty())
        {
            j["profession_id"] = professionId;
        }
        if (!unlockedSkills.empty())
        {
            j["unlocked_skills"] = unlockedSkills;
        }
        if (!unlockedRecipes.empty())
        {
            j["unlocked_recipes"] = unlockedRecipes;
        }
        return j;
    }

    ProfessionProgression ProfessionProgression::FromJson(const std::string& id,
                                                          const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("progression invalide");
        }

        ProfessionProgression p;
        p.professionId = id;
        p.level        = j.value("level", 1);
        p.xp           = j.value("xp", 0);
        p.skillPoints  = j.value("skill_points", 0);
        if (p.level < 1 || p.xp < 0)
        {
            throw std::runtime_error("progression invalide");
        }
        if (p.skillPoints < 0)
        {
            throw std::runtime_error("progression invalide");
        }

        if (j.contains("unlocked_skills") && j["unlocked_skills"].is_array())
        {
            for (const auto& s : j["unlocked_skills"])
            {
                if (s.is_string())
                {
                    p.unlockedSkills.push_back(s.get<std::string>());
                }
            }
        }
        if (j.contains("unlocked_recipes") && j["unlocked_recipes"].is_array())
        {
            for (const auto& s : j["unlocked_recipes"])
            {
                if (s.is_string())
                {
                    p.unlockedRecipes.push_back(s.get<std::string>());
                }
            }
        }
        return p;
    }
}

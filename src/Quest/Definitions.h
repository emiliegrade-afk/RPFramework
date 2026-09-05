// ============================================================================
// RPFramework - Quest / Definitions
//
// Squelette minimal du module quêtes pour la phase 1 du cycle de
// développement suivant la fondation stabilisée.
// ==========================================================================
#pragma once

#include "json.hpp"

#include "Character/Definitions.h"

#include <string>
#include <vector>

namespace rpframework::quest
{
    // Représente un objectif de quête.
    //
    // Contraintes de configuration (validées au chargement par
    // Quest::FromJson) :
    //   - `id`      : OBLIGATOIRE, non vide, unique au sein d'une même quête.
    //                 Sert de clé dans PlayerData.quests[questId].objectives.
    //   - `type`    : OBLIGATOIRE, non vide (ex: "kill", "collect", "visit").
    //   - `entity`  : OBLIGATOIRE, non vide (la cible précise).
    //   - `target`  : OBLIGATOIRE, > 0.
    //   - `time_limit_sec` : 0 = sans limite ; sinon > 0.
    // Une quête dont au moins un objectif viole ces règles est rejetée au
    // load (log d'erreur) et n'apparaît pas dans le Registry.
    struct Objective
    {
        std::string id;
        std::string type;      // ex: "kill", "collect", "visit"
        int target = 0;        // cible de progression
        std::string entity;    // cible de l'objectif (monstre, item, zone ...)
        bool required = true;
        int timeLimitSeconds = 0; // 0 = sans limite

        nlohmann::json ToJson() const;
        static Objective FromJson(const nlohmann::json& j);
    };

    struct Reward
    {
        std::string type;      // ex: "currency", "item", "xp"
        std::string id;
        int amount = 0;
        nlohmann::json payload = nlohmann::json::object();

        nlohmann::json ToJson() const;
        static Reward FromJson(const nlohmann::json& j);
    };

    struct Quest
    {
        std::string id;
        std::string name;
        std::string description;
        std::string lore;
        std::string source;
        int minLevel = 1;
        character::SelectionCondition condition;
        std::vector<std::string> prerequisites;
        std::string category;
        bool repeatable = false;
        // "all" = tous les objectifs requis, "any" = au moins un requis.
        std::string objectiveMode = "all";

        std::vector<Objective> objectives;
        std::vector<Reward> rewards;

        nlohmann::json ToJson() const;
        static Quest FromJson(const std::string& idIn, const nlohmann::json& j);
    };
}

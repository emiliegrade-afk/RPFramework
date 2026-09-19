// ============================================================================
// RPFramework - Faction / Definitions
//
// Types de données configurables pour le système de factions (GDD §13).
// Le framework ne fournit aucune faction baked-in : tout vient de
// `config.factions.*` (data-driven, comme Character).
//
// Réutilise `Character::SelectionCondition` pour les conditions d'adhésion
// (level, races/professions/classes requises ou exclues, min_reputation
// par faction). Cela garde la cohérence : une race demandée par une
// faction = même syntaxe qu'une race demandée par une profession.
// ============================================================================
#pragma once

#include "Character/Definitions.h"  // SelectionCondition (rpframework::character)

#include "json.hpp"

#include <string>
#include <vector>

namespace rpframework::faction
{
    // Le module Character vit dans `rpframework::character`, pas dans
    // `Character` ; on crée un alias local pour la lisibilité.
    namespace character = rpframework::character;

    // Un rang au sein d'une faction. Les rangs sont ordonnés dans la
    // définition : rang[0] est le plus bas, rang[N-1] le plus élevé.
    // Le rang effectif d'un joueur est calculé comme le dernier rang
    // dont le `min_reputation` est atteint (ou 0 s'il n'a pas de
    // réputation >= min du premier rang).
    struct Rank
    {
        std::string id;            // unique dans la faction
        std::string name;          // affiché
        int         minReputation = 0;  // réputation minimale (cumul)
        nlohmann::json benefits = nlohmann::json::object();  // libre

        nlohmann::json ToJson() const;
        static Rank FromJson(const nlohmann::json& j);
    };

    // Une faction (GDD §13).
    struct Faction
    {
        std::string id;              // unique, sert de clé
        std::string name;
        std::string description;
        std::string lore;

        std::vector<Rank> ranks;      // ordonnés du plus bas au plus haut

        // Réputation initiale accordée à tout nouveau membre au join.
        // Vide = 0. Sert à "définir la couleur de départ" d'un joueur
        // qui rejoint la faction (ex: un noble qui rejoint la garde
        // commence avec +50 plutôt que 0).
        int initialReputation = 0;

        // Conditions d'adhésion (level, races exclues, min_reputation
        // avec d'autres factions, etc.). Vide = tout le monde peut
        // rejoindre.
        character::SelectionCondition joinCondition;

        // Restrictions : liste d'IDs de races / métiers / classes
        // qui ne peuvent PAS rejoindre. (Garde-fou simple en plus de
        // joinCondition.)
        std::vector<std::string> excludedRaces;
        std::vector<std::string> excludedProfessions;
        std::vector<std::string> excludedClasses;

        // Quêtes auto-démarrées à l'adhésion (journal de faction / guilde).
        std::vector<std::string> starterQuests;
        // Item journal donné une fois : { "id", "quantity", "blueprint" }.
        nlohmann::json journal = nlohmann::json::object();

        nlohmann::json ToJson() const;
        static Faction FromJson(const std::string& id, const nlohmann::json& j);
    };
}

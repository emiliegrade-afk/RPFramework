// ============================================================================
// RPFramework - Character / Definitions
//
// Types de données configurables pour le système de personnages (GDD §4-8).
// Le framework ne fournit AUCUNE race / métier / classe baked-in : tout vient
// de la config. Les structs ci-dessous sont les "shapes" attendues par le
// parser JSON.
//
// Ce fichier est header-only : les structs sont de purs conteneurs.
// ============================================================================
#pragma once

#include "json.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpframework::character
{
    // -------------------------------------------------------------------------
    // Modificateur de stat. Trois opérations supportées :
    //   Add      : value est ajouté à la stat courante
    //   Multiply : la stat courante est multipliée par value
    //   Set      : la stat courante est remplacée par value
    //
    // `target` est une string libre (ex: "health", "stamina", "weight",
    // "movement", "carry_weight", "xp_gain", "harvest_yield", etc.) — c'est
    // aux modules aval (AsaApi, gameplay) de comprendre les targets qu'ils
    // supportent. Le framework ne valide PAS la liste.
    // -------------------------------------------------------------------------
    struct StatModifier
    {
        enum class Op : std::uint8_t
        {
            Add      = 0,
            Multiply = 1,
            Set      = 2,
        };

        Op          op    = Op::Add;
        float       value = 0.0f;
        std::string target;     // ex: "health", "stamina", "movement"

        static Op OpFromString(std::string_view s, Op fallback = Op::Add);
    };

    // -------------------------------------------------------------------------
    // Conditions de sélection d'une race / métier / classe.
    // Toutes les conditions sont optionnelles : non-renseignées = pas de
    // contrainte. Un joueur qui satisfait toutes les conditions peut choisir.
    // -------------------------------------------------------------------------
    struct SelectionCondition
    {
        int minLevel = 1;
        int maxLevel = std::numeric_limits<int>::max();

        std::vector<std::string> requiredRaces;        // le joueur doit avoir une de ces races
        std::vector<std::string> excludedRaces;        // …ne doit PAS avoir une de ces races
        std::vector<std::string> requiredProfessions;
        std::vector<std::string> excludedProfessions;
        std::vector<std::string> requiredClasses;
        std::vector<std::string> excludedClasses;

        // Faction → réputation minimale requise. Le joueur doit avoir au moins
        // cette valeur dans la faction donnée (peut être négatif = seuil).
        std::unordered_map<std::string, int> minReputation;

        // Helpers de check. Tous prennent les sélections courantes du joueur.
        bool IsSatisfiedBy(const std::string& currentRace,
                           const std::string& currentProfession,
                           const std::string& currentClass,
                           int                currentLevel,
                           const std::unordered_map<std::string, int>& currentReputation) const;

        std::string DescribeViolation(
            const std::string& currentRace,
            const std::string& currentProfession,
            const std::string& currentClass,
            int                currentLevel,
            const std::unordered_map<std::string, int>& currentReputation) const;
    };

    // -------------------------------------------------------------------------
    // Race (GDD §5)
    // -------------------------------------------------------------------------
    struct Race
    {
        std::string id;                  // unique, sert de clé
        std::string name;
        std::string description;
        std::string lore;

        std::vector<StatModifier> bonuses;
        std::vector<StatModifier> maluses;
        std::vector<std::string>  skills;
        std::vector<std::string>  restrictions;

        std::string spawnZone;          // vide = pas de zone imposée
        std::string faction;            // vide = pas de faction imposée

        std::unordered_map<std::string, int> initialReputation;

        // L'équipement de départ est sérialisé en JSON libre. Sa forme
        // concrète (items ASA, quantités, etc.) dépend du module Loadout
        // (Phase 4b). Phase 4a le stocke et le rend accessible, rien de plus.
        nlohmann::json starterEquipment = nlohmann::json::array();

        SelectionCondition selectionCondition;

        // Sérialisation. Le `id` est passé en argument à FromJson car il
        // vient de la clé du dictionnaire, pas du payload.
        nlohmann::json ToJson() const;
        static Race    FromJson(const std::string& id, const nlohmann::json& j);
    };

    // -------------------------------------------------------------------------
    // Métier / Profession (GDD §6)
    // -------------------------------------------------------------------------
    struct Profession
    {
        std::string id;
        std::string name;
        std::string description;
        std::string lore;

        int maxLevel = 100;             // niveau max atteignable dans ce métier
        int xpPerLevel = 1000;          // XP requis par niveau (linéaire)

        std::vector<StatModifier> bonuses;
        std::vector<std::string>  skills;
        std::vector<std::string>  specializations;
        std::vector<std::string>  restrictions;

        nlohmann::json starterEquipment = nlohmann::json::array();
        nlohmann::json rewards          = nlohmann::json::object();

        SelectionCondition selectionCondition;

        nlohmann::json ToJson() const;
        static Profession FromJson(const std::string& id, const nlohmann::json& j);
    };

    // -------------------------------------------------------------------------
    // Classe (GDD §7) — optionnelle, le serveur peut désactiver le système.
    // -------------------------------------------------------------------------
    struct CharClass
    {
        std::string id;
        std::string name;
        std::string description;
        std::string lore;

        std::vector<StatModifier> bonuses;
        std::vector<StatModifier> maluses;
        std::vector<std::string>  restrictions;
        nlohmann::json            starterEquipment = nlohmann::json::array();

        SelectionCondition selectionCondition;

        nlohmann::json ToJson() const;
        static CharClass FromJson(const std::string& id, const nlohmann::json& j);
    };
}

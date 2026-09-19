// ============================================================================
// RPFramework - Crafting / Definitions
//
// Types de données configurables pour l'artisanat (GDD §40 et §41).
// Le framework ne fournit AUCUNE recette / station baked-in : tout vient
// de `config.crafting.*`. Les structs ci-dessous sont les "shapes"
// attendues par le parser JSON.
//
// TEMPORAIRE (A3) : `NormalizeBlueprintPath` est une duplication locale de
// `asa::NormalizeBlueprintPath` (chantier A1, pas encore mergé). C1 doit
// mutualiser et supprimer cette copie.
// ============================================================================
#pragma once

#include "json.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rpframework::crafting
{
    // -------------------------------------------------------------------------
    // Normalisation locale minimale d'un chemin blueprint.
    //   "Blueprint'/Game/X.Default__X_C'" → "/Game/X.X"
    // Pure, sans dépendance ARK. Idempotente sur les chemins déjà normalisés.
    // -------------------------------------------------------------------------
    std::string NormalizeBlueprintPath(std::string_view raw);

    // -------------------------------------------------------------------------
    // Pile d'items (ingrédient ou produit). Les chemins blueprint sont
    // stockés NORMALISÉS.
    // -------------------------------------------------------------------------
    struct ItemStack
    {
        std::string blueprint;
        int         quantity = 0;

        nlohmann::json ToJson() const;
        static ItemStack FromJson(const nlohmann::json& j, const std::string& context);
    };

    // -------------------------------------------------------------------------
    // Atelier (GDD §41). Identifié par un id de config (ex: "smithy"),
    // pas par le nom localisé. `blueprint` pointe vers la structure ARK.
    // -------------------------------------------------------------------------
    struct Station
    {
        std::string              id;
        std::string              name;
        std::string              blueprint;
        std::vector<std::string> professions;

        nlohmann::json ToJson() const;
        static Station FromJson(const std::string& id, const nlohmann::json& j);
    };

    // -------------------------------------------------------------------------
    // Recette (GDD §40). Validation au chargement (FromJson jette) :
    //   - output.blueprint non vide
    //   - output.quantity > 0
    //   - profession non vide
    //   - min_level >= 1
    //   - xp >= 0
    //   - craft_time_sec >= 0
    //   - chaque ingrédient : blueprint non vide, quantity > 0
    // La vérification « station déclarée » est faite par le Registry
    // (FromJson n'a pas la liste des stations).
    // -------------------------------------------------------------------------
    struct Recipe
    {
        std::string           id;
        std::string           name;
        std::string           station;
        std::string           profession;
        int                   minLevel = 1;
        std::string           requiredSkill;
        int                   craftTimeSec = 0;
        int                   xp = 0;
        ItemStack             output;
        std::vector<ItemStack> ingredients;

        nlohmann::json ToJson() const;
        static Recipe  FromJson(const std::string& id, const nlohmann::json& j);
    };
}

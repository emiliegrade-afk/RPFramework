// ============================================================================
// RPFramework - Crafting / Definitions
//
// Types de données configurables pour l'artisanat (GDD §40 et §41).
// Le framework ne fournit AUCUNE recette / station baked-in : tout vient
// de `config.crafting.*`. Les structs ci-dessous sont les "shapes"
// attendues par le parser JSON.
//
// Les chemins blueprint sont normalisés par `asa::NormalizeBlueprintPath`
// (module pur `Asa/BlueprintPath.h`). Il n'existe volontairement qu'une seule
// implémentation : une copie locale finirait par diverger sur la casse ou sur
// le suffixe `_C`, et les lookups échoueraient silencieusement.
// ============================================================================
#pragma once

#include "json.hpp"

#include <string>
#include <vector>

namespace rpframework::crafting
{
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
        // Effet appliqué à l'utilisation (manger / boire), pas au craft.
        std::string           effectId;

        nlohmann::json ToJson() const;
        static Recipe  FromJson(const std::string& id, const nlohmann::json& j);
    };

    // Consommable data-driven (GDD §45) : blueprint vanilla → effect_id.
    // Permet d'associer un plat / une potion sans en faire une recette
    // RPG (donc sans verrou AllowCraft). Une recette avec `effect`
    // alimente le même index.
    struct Consumable
    {
        std::string id;
        std::string blueprint;
        std::string effectId;

        nlohmann::json ToJson() const;
        static Consumable FromJson(const std::string& id, const nlohmann::json& j);
    };
}

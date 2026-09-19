// ============================================================================
// RPFramework - Loadout / Item
//
// Un item distribuable au joueur. La forme est volontairement libre :
//   - `id`       : identifiant (ex: "bread", "iron_sword", "blueprint_sword")
//   - `quantity` : nombre d'unités (défaut 1)
//   - `quality`  : niveau de qualité ASA optionnel (ex: "primitive",
//                  "ramshackle", "apprentice", "journeyman", "master",
//                  "ascendant")
//   - `extras`   : blob JSON libre pour des extensions futures (stat
//                  overrides, durabilité pré-rolée, chemin Blueprint ASA, etc.)
//
// Un champ JSON `blueprint` au niveau racine est copié dans extras["blueprint"]
// pour `AShooterPlayerController::GiveItem`. Sans blueprint, l'item reste RP-only.
// ============================================================================
#pragma once

#include "json.hpp"

#include <string>
#include <vector>

namespace rpframework::loadout
{
    struct Item
    {
        std::string    id;
        int            quantity = 1;
        std::string    quality;
        nlohmann::json extras   = nlohmann::json::object();

        // Sérialisation
        nlohmann::json ToJson() const;

        // Lecture depuis un objet JSON. Renvoie une Item invalide (id vide)
        // si la clé "id" est absente — l'appelant peut alors skipper.
        static Item FromJson(const nlohmann::json& j);
    };

    // Parse une liste d'items. Les entrées invalides (sans id) sont
    // silencieusement filtrées (logged en debug par l'appelant si besoin).
    std::vector<Item> ItemsFromJsonArray(const nlohmann::json& j);
}

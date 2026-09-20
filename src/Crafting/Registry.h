// ============================================================================
// RPFramework - Crafting / Registry
//
// Registre des stations et recettes chargées depuis `config.crafting.*`.
// Data-driven : aucune entrée baked-in. Une entrée invalide est rejetée
// au chargement (log d'erreur) et n'apparaît pas dans le Registry
// (pattern Character/Registry).
//
// GDD §40 / §41.
// ============================================================================
#pragma once

#include "Crafting/Definitions.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpframework::crafting
{
    class Registry
    {
    public:
        // Cycle de vie ---------------------------------------------------------
        static void Initialize();
        static void Shutdown();
        // Recharge la section `config.crafting.*`. Idempotent : remplace
        // les définitions en place.
        static void LoadFromConfig();

        // Charge les définitions depuis un sous-objet `crafting` brut.
        // Utile pour les tests (évite d'écrire un fichier config temporaire).
        // Si `section` est null, traite comme une section absente.
        static void LoadDefinitionsFromSection(const nlohmann::json* section);

        // API Recettes ---------------------------------------------------------
        static bool                    HasRecipe(const std::string& id);
        static std::optional<Recipe>   GetRecipe(const std::string& id);
        static std::optional<Recipe>   FindByOutputBlueprint(const std::string& blueprint);
        static std::vector<Recipe>     ListRecipes();
        static std::vector<Recipe>     ListForProfession(const std::string& professionId);

        // API Stations ---------------------------------------------------------
        static std::optional<Station>  GetStation(const std::string& id);

        // Tests / admin : reset complet des définitions. ---------------
        static void ResetForTests();

    private:
        Registry() = default;

        static Registry& Instance();
        void LoadFromConfigImpl();

        mutable std::mutex                                  mutex_;
        std::unordered_map<std::string, Station>            stations_;
        std::unordered_map<std::string, Recipe>             recipes_;
        // asa::BlueprintKey(produit) → id de recette. Un doublon d'output
        // est rejeté au chargement (pas de « premier arrivé » silencieux).
        std::unordered_map<std::string, std::string>        outputIndex_;
        bool                                                initialized_ = false;
    };

    // -------------------------------------------------------------------------
    // API figée pour C1 (à respecter au caractère près).
    // -------------------------------------------------------------------------
    void Load();
    std::optional<Recipe>  GetRecipe(const std::string& id);
    std::optional<Recipe>  FindByOutputBlueprint(const std::string& blueprint);
    std::vector<Recipe>    ListRecipes();
    std::vector<Recipe>    ListForProfession(const std::string& professionId);
    std::optional<Station> GetStation(const std::string& id);
    bool HasRecipe(const std::string& id);
}

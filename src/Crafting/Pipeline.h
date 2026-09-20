// ============================================================================
// RPFramework - Crafting / Pipeline (GDD §40, §42, phase 14 — chantier C1)
//
// Câble la boucle : craft détecté → recette → conditions → XP métier →
// montée de niveau → engrams désormais accessibles.
//
// Logique testable hors serveur : CheckRecipeConditions,
// CollectAccessibleEngrams et AllowCraft n'ont aucune dépendance ARK. Le
// hook Asa appelle AllowCraft avant l'original, puis OnItemCrafted.
//
// Pipeline des mutations (pattern Wallet / Progression) :
//   load → validation des conditions → Progression::AddProfessionXp
//   (lui-même : load → mutation → save → audit) → TryUnlockEngrams.
// Permission : non pertinente ici (événement serveur, comme
// AddProfessionXp). Rate limit : non, un craft légitime peut être rapide.
//
// Filet anti-triche (GDD §40 levier 2) : AllowCraft refuse les recettes
// RPG hors métier / niveau / skill. Le hook n'appelle alors pas l'original.
// ============================================================================
#pragma once

#include "Crafting/Definitions.h"
#include "Security/Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace rpframework::crafting
{
    using PlayerId = rpframework::security::PlayerId;

    enum class RecipeGate
    {
        Ok,
        WrongProfession,
        LevelTooLow,
        MissingSkill,
    };

    // Validation pure : compilable sous RPFRAMEWORK_TESTS.
    RecipeGate CheckRecipeConditions(
        const Recipe& recipe,
        std::string_view selectedProfession,
        int professionLevel,
        const std::vector<std::string>& unlockedSkills);

    // Engrams accessibles depuis character.professions[id].engrams ET les
    // recettes de config.crafting dont les conditions sont remplies.
    std::vector<std::string> CollectAccessibleEngrams(
        const std::string& professionId,
        int professionLevel,
        const std::vector<std::string>& unlockedSkills);

    struct CraftNotice
    {
        std::string message;
        bool        ok = true;
    };

    struct CraftOutcome
    {
        enum class Status
        {
            UnknownRecipe,
            ConditionsUnmet,
            PlayerUnavailable,
            Applied,
        };

        Status                   status = Status::UnknownRecipe;
        bool                     leveledUp = false;
        int                      xpGranted = 0;
        int                      level = 1;
        int                      previousLevel = 1;
        int                      xpTotal = 0;
        std::string              profession;
        std::string              recipeId;
        std::vector<std::string> newlyUnlockedEngrams;
        std::vector<CraftNotice> notices;
    };

    // True si le craft vanilla peut s'exécuter. Recette inconnue (vanilla)
    // : autorisé. Recette RPG dont les conditions échouent : refusé, pour
    // que le hook n'appelle pas l'original.
    bool AllowCraft(PlayerId player, const std::string& outputBlueprint);

    // Point d'entrée du hook craft. Recette inconnue → no-op silencieux.
    CraftOutcome OnItemCrafted(PlayerId player, const std::string& outputBlueprint);

    // Réévaluation à la sélection métier, à la connexion, ou après un
    // level up : débloque via Loadout::TryUnlockEngrams tout engram
    // désormais accessible. Renvoie la liste passée à TryUnlockEngrams.
    std::vector<std::string> GrantAccessibleEngrams(PlayerId player);
}

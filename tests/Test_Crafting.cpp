// ============================================================================
// RPFramework - Tests du registre de recettes (chantier A3)
//
// Couvre : chargement depuis un JSON en mémoire, rejet de chaque règle de
// validation, lookup par blueprint de produit, filtrage par métier.
// ============================================================================
#include "TestHarness.h"

#include "Asa/BlueprintPath.h"
#include "Core/Config.h"
#include "Crafting/Definitions.h"
#include "Crafting/Registry.h"

#include <algorithm>
#include <string>

using namespace rpframework::crafting;
// La normalisation n'a qu'une implémentation, dans Asa/BlueprintPath.h.
using rpframework::asa::NormalizeBlueprintPath;

namespace
{

    nlohmann::json ValidStations()
    {
        return nlohmann::json::parse(R"({
            "refining_forge": {
                "name": "Fonderie",
                "blueprint": "/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_Forge.PrimalItemStructure_Forge",
                "professions": ["blacksmith"]
            },
            "smithy": {
                "name": "Forge",
                "blueprint": "/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_AnvilBench.PrimalItemStructure_AnvilBench",
                "professions": ["blacksmith"]
            }
        })");
    }

    nlohmann::json ValidRecipeIronIngot()
    {
        return nlohmann::json::parse(R"({
            "name": "Lingot de fer",
            "station": "refining_forge",
            "profession": "blacksmith",
            "min_level": 1,
            "required_skill": "",
            "craft_time_sec": 0,
            "xp": 5,
            "output": {
                "blueprint": "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot",
                "quantity": 1
            },
            "ingredients": [
                { "blueprint": "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalOre.PrimalItemResource_MetalOre", "quantity": 2 }
            ]
        })");
    }

    nlohmann::json ValidRecipeIronSword()
    {
        return nlohmann::json::parse(R"({
            "name": "Épée de fer",
            "station": "smithy",
            "profession": "blacksmith",
            "min_level": 1,
            "xp": 35,
            "output": {
                "blueprint": "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword",
                "quantity": 1
            },
            "ingredients": [
                { "blueprint": "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot", "quantity": 8 }
            ]
        })");
    }

    nlohmann::json ValidCraftingSection()
    {
        nlohmann::json section;
        section["stations"] = ValidStations();
        section["recipes"]["iron_ingot"] = ValidRecipeIronIngot();
        section["recipes"]["iron_sword"] = ValidRecipeIronSword();
        return section;
    }

    nlohmann::json ExtraValidRecipe()
    {
        nlohmann::json r;
        r["name"] = "Valide";
        r["station"] = "smithy";
        r["profession"] = "blacksmith";
        r["min_level"] = 1;
        r["xp"] = 0;
        r["craft_time_sec"] = 0;
        r["output"] = nlohmann::json{
            {"blueprint", "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponPike.PrimalItem_WeaponPike"},
            {"quantity", 1}
        };
        r["ingredients"] = nlohmann::json::array({
            nlohmann::json{
                {"blueprint", "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot"},
                {"quantity", 1}
            }
        });
        return r;
    }

    void LoadSection(const nlohmann::json& section)
    {
        Registry::ResetForTests();
        Registry::LoadDefinitionsFromSection(&section);
    }

    bool HasRecipeId(const std::vector<Recipe>& recipes, const std::string& id)
    {
        return std::any_of(recipes.begin(), recipes.end(),
                           [&](const Recipe& r) { return r.id == id; });
    }
}

// ---------------------------------------------------------------------------
// Chargement depuis un JSON en mémoire (API figée C1)
// ---------------------------------------------------------------------------
TEST(Crafting_Registry_LoadsFromMemoryJson)
{
    LoadSection(ValidCraftingSection());

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("iron_sword") == true);
    EXPECT(HasRecipe("unknown") == false);

    const auto ingot = GetRecipe("iron_ingot");
    EXPECT(ingot.has_value());
    if (ingot)
    {
        EXPECT(ingot->name == "Lingot de fer");
        EXPECT(ingot->station == "refining_forge");
        EXPECT(ingot->profession == "blacksmith");
        EXPECT(ingot->minLevel == 1);
        EXPECT(ingot->requiredSkill.empty());
        EXPECT(ingot->craftTimeSec == 0);
        EXPECT(ingot->xp == 5);
        EXPECT(ingot->output.quantity == 1);
        EXPECT(ingot->output.blueprint
            == "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot");
        EXPECT(ingot->ingredients.size() == 1);
        if (!ingot->ingredients.empty())
        {
            EXPECT(ingot->ingredients[0].quantity == 2);
            EXPECT(ingot->ingredients[0].blueprint
                == "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalOre.PrimalItemResource_MetalOre");
        }
    }

    const auto sword = GetRecipe("iron_sword");
    EXPECT(sword.has_value());
    if (sword)
    {
        EXPECT(sword->name == "Épée de fer");
        EXPECT(sword->station == "smithy");
        EXPECT(sword->xp == 35);
        EXPECT(sword->craftTimeSec == 0);
        EXPECT(sword->output.quantity == 1);
        EXPECT(sword->ingredients.size() == 1);
        if (!sword->ingredients.empty())
            EXPECT(sword->ingredients[0].quantity == 8);
    }

    const auto forge = GetStation("refining_forge");
    EXPECT(forge.has_value());
    if (forge)
    {
        EXPECT(forge->name == "Fonderie");
        EXPECT(forge->blueprint
            == "/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_Forge.PrimalItemStructure_Forge");
        EXPECT(forge->professions.size() == 1);
        if (!forge->professions.empty())
            EXPECT(forge->professions[0] == "blacksmith");
    }

    const auto smithy = GetStation("smithy");
    EXPECT(smithy.has_value());
    if (smithy)
        EXPECT(smithy->name == "Forge");

    EXPECT(GetStation("unknown").has_value() == false);

    const auto all = ListRecipes();
    EXPECT(all.size() == 2);

    Registry::ResetForTests();
}

TEST(Crafting_Load_FromConfigSet)
{
    using namespace rpframework::core;

    Config::Get().Set("crafting", ValidCraftingSection());
    Registry::ResetForTests();
    Load();

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("iron_sword") == true);
    EXPECT(GetStation("smithy").has_value());

    Registry::ResetForTests();
}

TEST(Crafting_AbsentSectionYieldsEmptyRegistry)
{
    Registry::ResetForTests();
    Registry::LoadDefinitionsFromSection(nullptr);
    EXPECT(ListRecipes().empty());
    EXPECT(HasRecipe("iron_ingot") == false);
    EXPECT(GetStation("smithy").has_value() == false);
}

// ---------------------------------------------------------------------------
// Rejet de CHAQUE règle de validation (entrée absente du Registry)
// ---------------------------------------------------------------------------
TEST(Crafting_Rejects_EmptyOutputBlueprint)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["output"]["blueprint"] = "";
    section["recipes"]["bad_empty_output_bp"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_empty_output_bp") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_MissingOutput)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad.erase("output");
    section["recipes"]["bad_missing_output"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_missing_output") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_OutputQuantityZero)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["output"]["quantity"] = 0;
    section["recipes"]["bad_output_qty_zero"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_output_qty_zero") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_OutputQuantityNegative)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["output"]["quantity"] = -1;
    section["recipes"]["bad_output_qty_neg"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_output_qty_neg") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_UnknownStation)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["station"] = "atelier_fantome";
    section["recipes"]["bad_unknown_station"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_unknown_station") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_EmptyStation)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["station"] = "";
    section["recipes"]["bad_empty_station"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_empty_station") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_EmptyProfession)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["profession"] = "";
    section["recipes"]["bad_empty_profession"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_empty_profession") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_MinLevelBelowOne)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["min_level"] = 0;
    section["recipes"]["bad_min_level"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_min_level") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_NegativeXp)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["xp"] = -1;
    section["recipes"]["bad_neg_xp"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_neg_xp") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_NegativeCraftTime)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["craft_time_sec"] = -5;
    section["recipes"]["bad_neg_craft_time"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_neg_craft_time") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_IngredientEmptyBlueprint)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["ingredients"][0]["blueprint"] = "";
    section["recipes"]["bad_ing_empty_bp"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_ing_empty_bp") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_IngredientQuantityZero)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["ingredients"][0]["quantity"] = 0;
    section["recipes"]["bad_ing_qty_zero"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_ing_qty_zero") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_IngredientQuantityNegative)
{
    auto section = ValidCraftingSection();
    auto bad = ExtraValidRecipe();
    bad["ingredients"][0]["quantity"] = -3;
    section["recipes"]["bad_ing_qty_neg"] = bad;
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_ing_qty_neg") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_NonObjectPayload)
{
    auto section = ValidCraftingSection();
    section["recipes"]["bad_not_object"] = "pas un objet";
    LoadSection(section);

    EXPECT(HasRecipe("iron_ingot") == true);
    EXPECT(HasRecipe("bad_not_object") == false);

    Registry::ResetForTests();
}

TEST(Crafting_Rejects_DuplicateOutputBlueprint)
{
    auto section = ValidCraftingSection();
    auto first = ExtraValidRecipe();
    first["name"] = "Premiere";
    auto dup = ExtraValidRecipe();
    dup["name"] = "Seconde";
    dup["output"]["blueprint"] =
        "/game/primalearth/coreblueprints/weapons/primalitem_weaponpike.primalitem_weaponpike";
    section["recipes"]["a_first"] = first;
    section["recipes"]["z_dup"] = dup;
    LoadSection(section);

    EXPECT(HasRecipe("a_first") == true);
    EXPECT(HasRecipe("z_dup") == false);
    EXPECT(HasRecipe("iron_ingot") == true);

    const auto found = FindByOutputBlueprint(
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponPike.PrimalItem_WeaponPike");
    EXPECT(found.has_value());
    if (found)
        EXPECT(found->id == "a_first");

    Registry::ResetForTests();
}

// ---------------------------------------------------------------------------
// Lookup par blueprint de produit
// ---------------------------------------------------------------------------
TEST(Crafting_FindByOutputBlueprint)
{
    LoadSection(ValidCraftingSection());

    const auto byIngot = FindByOutputBlueprint(
        "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot");
    EXPECT(byIngot.has_value());
    if (byIngot)
        EXPECT(byIngot->id == "iron_ingot");

    const auto bySword = FindByOutputBlueprint(
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword");
    EXPECT(bySword.has_value());
    if (bySword)
        EXPECT(bySword->id == "iron_sword");

    EXPECT(FindByOutputBlueprint("/Game/Does/Not/Exist.Exist").has_value() == false);
    EXPECT(FindByOutputBlueprint("").has_value() == false);

    Registry::ResetForTests();
}

TEST(Crafting_FindByOutputBlueprint_NormalizesQuery)
{
    LoadSection(ValidCraftingSection());

    const auto wrapped = FindByOutputBlueprint(
        "Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.Default__PrimalItem_WeaponSword_C'");
    EXPECT(wrapped.has_value());
    if (wrapped)
        EXPECT(wrapped->id == "iron_sword");

    Registry::ResetForTests();
}

// ---------------------------------------------------------------------------
// Filtrage par métier
// ---------------------------------------------------------------------------
TEST(Crafting_ListForProfession)
{
    auto section = ValidCraftingSection();
    auto herbal = ExtraValidRecipe();
    herbal["name"] = "Tisane";
    herbal["profession"] = "herbalist";
    herbal["output"]["blueprint"] =
        "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_Stimulant.PrimalItemConsumable_Stimulant";
    section["recipes"]["herbal_tea"] = herbal;
    LoadSection(section);

    const auto smith = ListForProfession("blacksmith");
    EXPECT(smith.size() == 2);
    EXPECT(HasRecipeId(smith, "iron_ingot"));
    EXPECT(HasRecipeId(smith, "iron_sword"));
    EXPECT(!HasRecipeId(smith, "herbal_tea"));

    const auto herbs = ListForProfession("herbalist");
    EXPECT(herbs.size() == 1);
    EXPECT(HasRecipeId(herbs, "herbal_tea"));

    EXPECT(ListForProfession("guard").empty());
    EXPECT(ListForProfession("").empty());

    Registry::ResetForTests();
}

// ---------------------------------------------------------------------------
// Chemins blueprint stockés NORMALISÉS
// ---------------------------------------------------------------------------
TEST(Crafting_StoresNormalizedBlueprintPaths)
{
    nlohmann::json section;
    section["stations"]["refining_forge"] = nlohmann::json{
        {"name", "Fonderie"},
        {"blueprint", "Blueprint'/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_Forge.Default__PrimalItemStructure_Forge_C'"},
        {"professions", nlohmann::json::array({"blacksmith"})}
    };

    nlohmann::json recipe;
    recipe["name"] = "Lingot wrappé";
    recipe["station"] = "refining_forge";
    recipe["profession"] = "blacksmith";
    recipe["min_level"] = 1;
    recipe["xp"] = 5;
    recipe["output"] = nlohmann::json{
        {"blueprint", "Blueprint'/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.Default__PrimalItemResource_MetalIngot_C'"},
        {"quantity", 1}
    };
    recipe["ingredients"] = nlohmann::json::array({
        nlohmann::json{
            {"blueprint", "Blueprint'/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalOre.PrimalItemResource_MetalOre_C'"},
            {"quantity", 2}
        }
    });
    section["recipes"]["wrapped_ingot"] = recipe;

    LoadSection(section);

    const auto station = GetStation("refining_forge");
    EXPECT(station.has_value());
    if (station)
    {
        EXPECT(station->blueprint
            == "/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_Forge.PrimalItemStructure_Forge");
    }

    const auto rec = GetRecipe("wrapped_ingot");
    EXPECT(rec.has_value());
    if (rec)
    {
        EXPECT(rec->output.blueprint
            == "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot");
        EXPECT(rec->ingredients.size() == 1);
        if (!rec->ingredients.empty())
        {
            EXPECT(rec->ingredients[0].blueprint
                == "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalOre.PrimalItemResource_MetalOre");
        }
    }

    EXPECT(NormalizeBlueprintPath("").empty());
    EXPECT(NormalizeBlueprintPath("Blueprint'/Game/X.Default__X_C'") == "/Game/X.X");

    Registry::ResetForTests();
}

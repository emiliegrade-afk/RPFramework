// ============================================================================
// RPFramework - Tests C1 : câblage craft → recette → XP → engrams
// ============================================================================
#include "TestHarness.h"

#include "Asa/BlueprintPath.h"
#include "Character/Registry.h"
#include "Core/Config.h"
#include "Crafting/Pipeline.h"
#include "Crafting/Registry.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Progression/Professions.h"
#include "Security/AuditLog.h"
#include "Security/Types.h"

#include "json.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
    using namespace rpframework;

    const std::string kSwordBp =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword";
    const std::string kPickBp =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponMetalPick.PrimalItem_WeaponMetalPick";
    const std::string kIngotBp =
        "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot";
    const std::string kPikeBp =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponPike.PrimalItem_WeaponPike";
    const std::string kUnknownBp =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponStonePick.PrimalItem_WeaponStonePick";

    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_c1_" + tag + "_" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void ConfigurePlayerStore(const std::filesystem::path& dir)
    {
        data::PlayerStore::Shutdown();
        core::Config::Get().Set("data.save_dir", dir.string());
        core::Config::Get().Set("data.backup_count", 3);
        data::PlayerStore::LoadFromConfig();
    }

    void CleanupAll(const std::filesystem::path& dir)
    {
        crafting::Registry::ResetForTests();
        character::Registry::Shutdown();
        security::AuditLog::Shutdown();
        data::PlayerStore::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void LoadSmithAndRecipes()
    {
        nlohmann::json character;
        character["professions"]["blacksmith"] = {
            {"name", "Forgeron"},
            {"xp_per_level", 100},
            {"max_level", 10},
            {"engrams", nlohmann::json::array({kPikeBp})}
        };
        character["professions"]["herbalist"] = {
            {"name", "Herboriste"},
            {"xp_per_level", 100},
            {"max_level", 10}
        };
        character::Registry::ResetForTests();
        character::Registry::LoadDefinitionsFromSection(&character);

        nlohmann::json crafting;
        crafting["stations"]["smithy"] = {
            {"name", "Forge"},
            {"blueprint",
             "/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_AnvilBench.PrimalItemStructure_AnvilBench"},
            {"professions", nlohmann::json::array({"blacksmith"})}
        };
        crafting["recipes"]["iron_sword"] = {
            {"name", "Épée de fer"},
            {"station", "smithy"},
            {"profession", "blacksmith"},
            {"min_level", 1},
            {"xp", 35},
            {"output", {{"blueprint", kSwordBp}, {"quantity", 1}}},
            {"ingredients", nlohmann::json::array({
                {{"blueprint", kIngotBp}, {"quantity", 8}}
            })}
        };
        crafting["recipes"]["metal_pick"] = {
            {"name", "Pioche en métal"},
            {"station", "smithy"},
            {"profession", "blacksmith"},
            {"min_level", 2},
            {"required_skill", ""},
            {"xp", 20},
            {"output", {{"blueprint", kPickBp}, {"quantity", 1}}},
            {"ingredients", nlohmann::json::array({
                {{"blueprint", kIngotBp}, {"quantity", 5}}
            })}
        };
        crafting["recipes"]["master_blade"] = {
            {"name", "Lame de maître"},
            {"station", "smithy"},
            {"profession", "blacksmith"},
            {"min_level", 1},
            {"required_skill", "metallurgy"},
            {"xp", 50},
            {"output", {
                {"blueprint",
                 "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponOneShotRifle.PrimalItem_WeaponOneShotRifle"},
                {"quantity", 1}
            }},
            {"ingredients", nlohmann::json::array({
                {{"blueprint", kIngotBp}, {"quantity", 1}}
            })}
        };
        crafting::Registry::ResetForTests();
        crafting::Registry::LoadDefinitionsFromSection(&crafting);
    }

    bool ContainsBlueprint(const std::vector<std::string>& list, const std::string& blueprint)
    {
        const auto key = asa::BlueprintKey(blueprint);
        return std::any_of(list.begin(), list.end(), [&](const std::string& bp) {
            return asa::BlueprintKey(bp) == key;
        });
    }

    data::PlayerData MakeSmith(security::PlayerId id)
    {
        data::PlayerData p;
        p.id = id;
        p.profession = "blacksmith";
        return p;
    }
}

TEST(CraftPipeline_CheckRecipe_Eligible)
{
    LoadSmithAndRecipes();
    const auto recipe = crafting::GetRecipe("iron_sword");
    EXPECT(recipe.has_value());
    if (recipe)
    {
        EXPECT(crafting::CheckRecipeConditions(*recipe, "blacksmith", 1, {})
            == crafting::RecipeGate::Ok);
    }
    crafting::Registry::ResetForTests();
    character::Registry::Shutdown();
}

TEST(CraftPipeline_CheckRecipe_WrongProfession)
{
    LoadSmithAndRecipes();
    const auto recipe = crafting::GetRecipe("iron_sword");
    EXPECT(recipe.has_value());
    if (recipe)
    {
        EXPECT(crafting::CheckRecipeConditions(*recipe, "herbalist", 10, {})
            == crafting::RecipeGate::WrongProfession);
        EXPECT(crafting::CheckRecipeConditions(*recipe, "", 10, {})
            == crafting::RecipeGate::WrongProfession);
    }
    crafting::Registry::ResetForTests();
    character::Registry::Shutdown();
}

TEST(CraftPipeline_CheckRecipe_LevelTooLow)
{
    LoadSmithAndRecipes();
    const auto recipe = crafting::GetRecipe("metal_pick");
    EXPECT(recipe.has_value());
    if (recipe)
    {
        EXPECT(crafting::CheckRecipeConditions(*recipe, "blacksmith", 1, {})
            == crafting::RecipeGate::LevelTooLow);
        EXPECT(crafting::CheckRecipeConditions(*recipe, "blacksmith", 2, {})
            == crafting::RecipeGate::Ok);
    }
    crafting::Registry::ResetForTests();
    character::Registry::Shutdown();
}

TEST(CraftPipeline_CheckRecipe_MissingSkill)
{
    LoadSmithAndRecipes();
    const auto recipe = crafting::GetRecipe("master_blade");
    EXPECT(recipe.has_value());
    if (recipe)
    {
        EXPECT(crafting::CheckRecipeConditions(*recipe, "blacksmith", 5, {})
            == crafting::RecipeGate::MissingSkill);
        EXPECT(crafting::CheckRecipeConditions(*recipe, "blacksmith", 5, {"metallurgy"})
            == crafting::RecipeGate::Ok);
    }
    crafting::Registry::ResetForTests();
    character::Registry::Shutdown();
}

TEST(CraftPipeline_CollectAccessibleEngrams_LevelGate)
{
    LoadSmithAndRecipes();

    const auto at1 = crafting::CollectAccessibleEngrams("blacksmith", 1, {});
    EXPECT(ContainsBlueprint(at1, kPikeBp));
    EXPECT(ContainsBlueprint(at1, kSwordBp));
    EXPECT(!ContainsBlueprint(at1, kPickBp));

    const auto at2 = crafting::CollectAccessibleEngrams("blacksmith", 2, {});
    EXPECT(ContainsBlueprint(at2, kPikeBp));
    EXPECT(ContainsBlueprint(at2, kSwordBp));
    EXPECT(ContainsBlueprint(at2, kPickBp));

    const auto withSkill = crafting::CollectAccessibleEngrams(
        "blacksmith", 2, {"metallurgy"});
    EXPECT(ContainsBlueprint(withSkill,
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponOneShotRifle.PrimalItem_WeaponOneShotRifle"));

    crafting::Registry::ResetForTests();
    character::Registry::Shutdown();
}

TEST(CraftPipeline_RecipeFoundAddsXp)
{
    const auto dir = MakeTempPlayerDir("xp");
    ConfigurePlayerStore(dir);
    LoadSmithAndRecipes();
    security::AuditLog::Initialize();

    auto smith = MakeSmith(98100);
    EXPECT(data::PlayerStore::Save(smith));
    const auto r = crafting::OnItemCrafted(98100, kSwordBp);
    EXPECT(r.status == crafting::CraftOutcome::Status::Applied);
    EXPECT(r.recipeId == "iron_sword");
    EXPECT(r.profession == "blacksmith");
    EXPECT(r.xpGranted == 35);
    EXPECT(!r.leveledUp);
    EXPECT(progression::GetProfessionXp(98100, "blacksmith") == 35);
    EXPECT(progression::GetProfessionLevel(98100, "blacksmith") == 1);
    EXPECT(!r.notices.empty());

    CleanupAll(dir);
}

TEST(CraftPipeline_UnknownRecipeIsSilentNoOp)
{
    const auto dir = MakeTempPlayerDir("unknown");
    ConfigurePlayerStore(dir);
    LoadSmithAndRecipes();
    security::AuditLog::Initialize();

    auto smith = MakeSmith(98101);
    EXPECT(data::PlayerStore::Save(smith));
    const auto r = crafting::OnItemCrafted(98101, kUnknownBp);
    EXPECT(r.status == crafting::CraftOutcome::Status::UnknownRecipe);
    EXPECT(r.xpGranted == 0);
    EXPECT(r.notices.empty());
    EXPECT(progression::GetProfessionXp(98101, "blacksmith") == 0);

    const auto empty = crafting::OnItemCrafted(98101, "");
    EXPECT(empty.status == crafting::CraftOutcome::Status::UnknownRecipe);
    EXPECT(progression::GetProfessionXp(98101, "blacksmith") == 0);

    CleanupAll(dir);
}

TEST(CraftPipeline_WrongProfessionNoXp)
{
    const auto dir = MakeTempPlayerDir("wrongjob");
    ConfigurePlayerStore(dir);
    LoadSmithAndRecipes();
    security::AuditLog::Initialize();

    data::PlayerData p;
    p.id = 98102;
    p.profession = "herbalist";
    EXPECT(data::PlayerStore::Save(p));

    const auto r = crafting::OnItemCrafted(98102, kSwordBp);
    EXPECT(r.status == crafting::CraftOutcome::Status::ConditionsUnmet);
    EXPECT(r.xpGranted == 0);
    EXPECT(progression::GetProfessionXp(98102, "blacksmith") == 0);
    EXPECT(progression::GetProfessionXp(98102, "herbalist") == 0);

    CleanupAll(dir);
}

TEST(CraftPipeline_LevelTooLowNoXp)
{
    const auto dir = MakeTempPlayerDir("lowlevel");
    ConfigurePlayerStore(dir);
    LoadSmithAndRecipes();
    security::AuditLog::Initialize();

    auto smith = MakeSmith(98103);
    EXPECT(data::PlayerStore::Save(smith));
    const auto r = crafting::OnItemCrafted(98103, kPickBp);
    EXPECT(r.status == crafting::CraftOutcome::Status::ConditionsUnmet);
    EXPECT(r.xpGranted == 0);
    EXPECT(progression::GetProfessionXp(98103, "blacksmith") == 0);
    EXPECT(progression::GetProfessionLevel(98103, "blacksmith") == 1);

    CleanupAll(dir);
}

TEST(CraftPipeline_LevelUpUnlocksEngrams)
{
    const auto dir = MakeTempPlayerDir("levelup");
    ConfigurePlayerStore(dir);
    LoadSmithAndRecipes();
    security::AuditLog::Initialize();

    data::PlayerData p = MakeSmith(98104);
    p.professions["blacksmith"].professionId = "blacksmith";
    p.professions["blacksmith"].level = 1;
    p.professions["blacksmith"].xp = 80;
    EXPECT(data::PlayerStore::Save(p));

    const auto before = crafting::CollectAccessibleEngrams("blacksmith", 1, {});
    EXPECT(!ContainsBlueprint(before, kPickBp));

    const auto r = crafting::OnItemCrafted(98104, kSwordBp);
    EXPECT(r.status == crafting::CraftOutcome::Status::Applied);
    EXPECT(r.leveledUp);
    EXPECT(r.previousLevel == 1);
    EXPECT(r.level == 2);
    EXPECT(r.xpTotal == 115);
    EXPECT(progression::GetProfessionLevel(98104, "blacksmith") == 2);
    EXPECT(ContainsBlueprint(r.newlyUnlockedEngrams, kPickBp));
    EXPECT(!ContainsBlueprint(r.newlyUnlockedEngrams, kPikeBp));

    bool toldUnlock = false;
    for (const auto& notice : r.notices)
    {
        if (notice.message.find("Pioche en métal") != std::string::npos)
            toldUnlock = true;
    }
    EXPECT(toldUnlock);

    CleanupAll(dir);
}

TEST(CraftPipeline_FindsRecipeViaWrappedBlueprint)
{
    const auto dir = MakeTempPlayerDir("wrap");
    ConfigurePlayerStore(dir);
    LoadSmithAndRecipes();
    security::AuditLog::Initialize();

    auto smith = MakeSmith(98105);
    EXPECT(data::PlayerStore::Save(smith));
    const auto r = crafting::OnItemCrafted(
        98105,
        "Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword_C'");
    EXPECT(r.status == crafting::CraftOutcome::Status::Applied);
    EXPECT(r.xpGranted == 35);

    CleanupAll(dir);
}

TEST(CraftPipeline_GrantAccessibleEngramsAtSelection)
{
    const auto dir = MakeTempPlayerDir("grant");
    ConfigurePlayerStore(dir);
    LoadSmithAndRecipes();
    security::AuditLog::Initialize();

    auto smith = MakeSmith(98106);
    EXPECT(data::PlayerStore::Save(smith));
    const auto granted = crafting::GrantAccessibleEngrams(98106);
    EXPECT(ContainsBlueprint(granted, kPikeBp));
    EXPECT(ContainsBlueprint(granted, kSwordBp));
    EXPECT(!ContainsBlueprint(granted, kPickBp));

    CleanupAll(dir);
}

TEST(CraftPipeline_AllowCraftGatesVanillaOriginal)
{
    const auto dir = MakeTempPlayerDir("allow");
    ConfigurePlayerStore(dir);
    LoadSmithAndRecipes();
    security::AuditLog::Initialize();

    auto smith = MakeSmith(98107);
    EXPECT(data::PlayerStore::Save(smith));
    EXPECT(crafting::AllowCraft(98107, kSwordBp));
    EXPECT(crafting::AllowCraft(98107, kUnknownBp));
    EXPECT(crafting::AllowCraft(98107, ""));
    EXPECT(!crafting::AllowCraft(98107, kPickBp));

    data::PlayerData herbalist;
    herbalist.id = 98108;
    herbalist.profession = "herbalist";
    EXPECT(data::PlayerStore::Save(herbalist));
    EXPECT(!crafting::AllowCraft(98108, kSwordBp));
    EXPECT(crafting::AllowCraft(98108, kUnknownBp));

    CleanupAll(dir);
}

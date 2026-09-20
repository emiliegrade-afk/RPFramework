// ============================================================================
// RPFramework - Tests cuisine / alchimie (GDD §45, phase 19)
// ============================================================================
#include "TestHarness.h"

#include "Character/Registry.h"
#include "Core/Config.h"
#include "Crafting/Pipeline.h"
#include "Crafting/Registry.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Effects/Registry.h"
#include "Loadout/AsaDeliver.h"
#include "Security/AuditLog.h"

#include "json.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace rpframework;

namespace
{
    const std::string kChiliBp =
        "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_Soup_FocalChili.PrimalItemConsumable_Soup_FocalChili";
    const std::string kStimBp =
        "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_Stimulant.PrimalItemConsumable_Stimulant";
    const std::string kMeatBp =
        "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_CookedMeat.PrimalItemConsumable_CookedMeat";
    const std::string kBerryBp =
        "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_Berry_Mejoberry.PrimalItemConsumable_Berry_Mejoberry";
    const std::string kPotBp =
        "/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_CookingPot.PrimalItemStructure_CookingPot";

    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_cook_" + tag + "_" + std::to_string(std::rand()));
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
        effects::Registry::ResetForTests();
        character::Registry::Shutdown();
        security::AuditLog::Shutdown();
        data::PlayerStore::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    nlohmann::json CookingSection()
    {
        nlohmann::json section;
        section["stations"]["cooking_pot"] = {
            {"name", "Marmite"},
            {"blueprint", kPotBp},
            {"professions", nlohmann::json::array({"herbalist"})}
        };
        section["recipes"]["focal_chili"] = {
            {"name", "Ragout"},
            {"station", "cooking_pot"},
            {"profession", "herbalist"},
            {"min_level", 1},
            {"xp", 15},
            {"effect", "hunter_stew"},
            {"output", {{"blueprint", kChiliBp}, {"quantity", 1}}},
            {"ingredients", nlohmann::json::array({
                {{"blueprint", kMeatBp}, {"quantity", 1}},
                {{"blueprint", kBerryBp}, {"quantity", 10}}
            })}
        };
        return section;
    }

    void LoadCooking()
    {
        nlohmann::json character;
        character["professions"]["herbalist"] = {
            {"name", "Herboriste"},
            {"xp_per_level", 100},
            {"max_level", 10}
        };
        character::Registry::ResetForTests();
        character::Registry::LoadDefinitionsFromSection(&character);

        nlohmann::json effects;
        effects["hunter_stew"] = {
            {"name", "Ragout"},
            {"type", "buff"},
            {"buff_blueprint", "/Game/Test/Buff_Stew.Buff_Stew"},
            {"duration_sec", 60},
            {"stacking", "refresh"},
            {"max_stacks", 1},
            {"cooldown_sec", 0},
            {"modifiers", nlohmann::json::array({
                {{"target", "stamina"}, {"op", "add"}, {"value", 20.0}}
            })},
            {"conditions", {{"min_level", 1}}}
        };
        effects["vigor_brew"] = {
            {"name", "Vigueur"},
            {"type", "buff"},
            {"buff_blueprint", "/Game/Test/Buff_Vigor.Buff_Vigor"},
            {"duration_sec", 30},
            {"stacking", "refresh"},
            {"max_stacks", 1},
            {"cooldown_sec", 0},
            {"modifiers", nlohmann::json::array({
                {{"target", "stamina"}, {"op", "add"}, {"value", 10.0}}
            })},
            {"conditions", {{"min_level", 1}}}
        };
        effects::Registry::ResetForTests();
        effects::Registry::LoadDefinitionsFromSection(&effects);

        const auto section = CookingSection();
        crafting::Registry::ResetForTests();
        crafting::Registry::LoadDefinitionsFromSection(&section);
        security::AuditLog::Initialize();
    }
}

TEST(Cooking_RecipeEffectIndexesConsumable)
{
    LoadCooking();
    const auto effect = crafting::FindEffectForBlueprint(kChiliBp);
    EXPECT(effect.has_value());
    if (effect) EXPECT(*effect == "hunter_stew");
    EXPECT(!crafting::FindEffectForBlueprint(kMeatBp).has_value());
    crafting::Registry::ResetForTests();
    effects::Registry::ResetForTests();
    character::Registry::Shutdown();
}

TEST(Cooking_OnItemUsedAppliesEffect)
{
    const auto dir = MakeTempPlayerDir("eat");
    ConfigurePlayerStore(dir);
    LoadCooking();
    loadout::g_lastBuffBlueprint.clear();

    data::PlayerData p;
    p.id = 99001;
    p.profession = "herbalist";
    EXPECT(data::PlayerStore::Save(p));

    const auto unknown = crafting::OnItemUsed(99001, kMeatBp);
    EXPECT(!unknown.applied);

    const auto eaten = crafting::OnItemUsed(99001, kChiliBp);
    EXPECT(eaten.applied);
    EXPECT(eaten.effectId == "hunter_stew");
    EXPECT(loadout::g_lastBuffBlueprint.find("Buff_Stew") != std::string::npos);

    CleanupAll(dir);
}

TEST(Cooking_CraftDoesNotApplyConsumeEffect)
{
    const auto dir = MakeTempPlayerDir("craft");
    ConfigurePlayerStore(dir);
    LoadCooking();
    loadout::g_lastBuffBlueprint.clear();

    data::PlayerData p;
    p.id = 99002;
    p.profession = "herbalist";
    p.professions["herbalist"].professionId = "herbalist";
    p.professions["herbalist"].level = 1;
    EXPECT(data::PlayerStore::Save(p));

    const auto crafted = crafting::OnItemCrafted(99002, kChiliBp);
    EXPECT(crafted.status == crafting::CraftOutcome::Status::Applied);
    EXPECT(loadout::g_lastBuffBlueprint.empty());

    CleanupAll(dir);
}

TEST(Cooking_ConsumableOverlayOverridesRecipe)
{
    LoadCooking();
    auto section = CookingSection();
    section["consumables"]["focal_chili"] = {
        {"blueprint", kChiliBp},
        {"effect", "vigor_brew"}
    };
    section["consumables"]["stimulant"] = {
        {"blueprint", kStimBp},
        {"effect", "vigor_brew"}
    };
    crafting::Registry::LoadDefinitionsFromSection(&section);

    const auto chili = crafting::FindEffectForBlueprint(kChiliBp);
    EXPECT(chili.has_value());
    if (chili) EXPECT(*chili == "vigor_brew");
    const auto stim = crafting::FindEffectForBlueprint(kStimBp);
    EXPECT(stim.has_value());
    if (stim) EXPECT(*stim == "vigor_brew");

    crafting::Registry::ResetForTests();
    effects::Registry::ResetForTests();
    character::Registry::Shutdown();
}

TEST(Cooking_RejectsInvalidConsumable)
{
    LoadCooking();
    auto section = CookingSection();
    section["consumables"]["broken"] = {
        {"blueprint", kStimBp},
        {"effect", ""}
    };
    crafting::Registry::LoadDefinitionsFromSection(&section);
    EXPECT(!crafting::FindEffectForBlueprint(kStimBp).has_value());
    const auto chili = crafting::FindEffectForBlueprint(kChiliBp);
    EXPECT(chili.has_value());
    if (chili) EXPECT(*chili == "hunter_stew");

    crafting::Registry::ResetForTests();
    effects::Registry::ResetForTests();
    character::Registry::Shutdown();
}

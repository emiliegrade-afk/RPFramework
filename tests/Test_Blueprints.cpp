// ============================================================================
// RPFramework - Tests A1 : clé blueprint canonique + alias d'événements
// ============================================================================
#include "TestHarness.h"

#include "Asa/Blueprints.h"
#include "Core/Config.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Quest/Engine.h"
#include "Quest/Events.h"
#include "Quest/Registry.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include "json.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_" + tag + "_" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void ConfigurePlayerStore(const std::filesystem::path& dir, int backupCount = 3)
    {
        using namespace rpframework;
        data::PlayerStore::Shutdown();
        core::Config::Get().Set("data.save_dir", dir.string());
        core::Config::Get().Set("data.backup_count", backupCount);
        data::PlayerStore::LoadFromConfig();
    }

    void CleanupPlayerStore(const std::filesystem::path& dir)
    {
        rpframework::data::PlayerStore::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    constexpr const char* kBoarBlueprint =
        "/Game/PrimalEarth/Dinos/Boar/Boar_Character_BP.Boar_Character_BP";
    constexpr const char* kSwordBlueprint =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword";
}

TEST(Blueprints_Normalize_Empty)
{
    using namespace rpframework::asa;
    EXPECT(NormalizeBlueprintPath("").empty());
    EXPECT(NormalizeBlueprintPath("   ").empty());
    EXPECT(NormalizeBlueprintPath("\t\n").empty());
}

TEST(Blueprints_Normalize_Wrapper)
{
    using namespace rpframework::asa;
    EXPECT(NormalizeBlueprintPath("Blueprint'/Game/X.X'") == "/Game/X.X");
    EXPECT(NormalizeBlueprintPath(
        "Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword'")
        == kSwordBlueprint);
}

TEST(Blueprints_Normalize_DefaultPrefix)
{
    using namespace rpframework::asa;
    EXPECT(NormalizeBlueprintPath("/Game/X.Default__X") == "/Game/X.X");
    EXPECT(NormalizeBlueprintPath(
        "/Game/PrimalEarth/Dinos/Boar/Boar_Character_BP.Default__Boar_Character_BP")
        == kBoarBlueprint);
}

TEST(Blueprints_Normalize_ClassSuffix)
{
    using namespace rpframework::asa;
    EXPECT(NormalizeBlueprintPath("/Game/X.X_C") == "/Game/X.X");
    EXPECT(NormalizeBlueprintPath(
        "/Game/PrimalEarth/Dinos/Boar/Boar_Character_BP.Boar_Character_BP_C")
        == kBoarBlueprint);
}

TEST(Blueprints_Normalize_FullUnrealForm)
{
    using namespace rpframework::asa;
    EXPECT(NormalizeBlueprintPath("Blueprint'/Game/X.Default__X_C'") == "/Game/X.X");
    EXPECT(NormalizeBlueprintPath(
        "Blueprint'/Game/PrimalEarth/Dinos/Boar/Boar_Character_BP.Default__Boar_Character_BP_C'")
        == kBoarBlueprint);
}

TEST(Blueprints_Normalize_AlreadyCanonical)
{
    using namespace rpframework::asa;
    EXPECT(NormalizeBlueprintPath(kSwordBlueprint) == kSwordBlueprint);
    EXPECT(NormalizeBlueprintPath(NormalizeBlueprintPath(
        "Blueprint'/Game/X.Default__X_C'")) == "/Game/X.X");
}

TEST(Blueprints_BlueprintPathOf_StubInTests)
{
    EXPECT(rpframework::asa::BlueprintPathOf(nullptr).empty());
}

TEST(Blueprints_ReportGameplay_FirstHuntKeepsSlugAlias)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("a1_first_hunt");
    ConfigurePlayerStore(dir);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json quests;
    quests["first_hunt"] = {
        {"name", "Premiere chasse"},
        {"auto_complete", false},
        {"min_level", 1},
        {"objectives", {{{"id", "kill_boar"}, {"type", "kill"},
                         {"entity", "boar"}, {"target", 2}, {"required", true}}}}
    };
    quest::Registry::ResetForTests();
    quest::Registry::LoadDefinitionsFromSection(&quests);

    constexpr security::PlayerId pid = 96100;
    data::PlayerData player;
    player.id = pid;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(pid, "first_hunt").success());

    // Piège Match.h : le chemin seul ne contient plus le token "boar".
    EXPECT(quest::ReportGameplay(pid, "kill",
        std::vector<std::string>{kBoarBlueprint}) == 0);

    // La clé blueprint est en [0], le slug reste l'alias qui fait matcher.
    EXPECT(quest::ReportGameplay(pid, "kill",
        std::vector<std::string>{kBoarBlueprint, "boar"}) == 1);

    auto load = data::PlayerStore::LoadDetailed(pid);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->quests["first_hunt"].status
            == data::QuestProgress::Status::Active);
        EXPECT(load.data->quests["first_hunt"].objectives["kill_boar"] == 1);
    }

    // Ancienne signature conservée.
    EXPECT(quest::ReportGameplay(pid, "kill", "boar") == 1);
    load = data::PlayerStore::LoadDetailed(pid);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->quests["first_hunt"].status
            == data::QuestProgress::Status::Active);
        EXPECT(load.data->quests["first_hunt"].objectives["kill_boar"] == 2);
    }

    quest::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Blueprints_ReportGameplay_HarvestAliasesAreGranular)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("a1_harvest");
    ConfigurePlayerStore(dir);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json quests;
    quests["mine_metal"] = {
        {"name", "Minerai"},
        {"auto_complete", false},
        {"min_level", 1},
        {"objectives", {{{"id", "get_metal"}, {"type", "collect"},
                         {"entity", "metal"}, {"target", 1}, {"required", true}}}}
    };
    quests["mine_stone"] = {
        {"name", "Pierre"},
        {"auto_complete", false},
        {"min_level", 1},
        {"objectives", {{{"id", "get_stone"}, {"type", "collect"},
                         {"entity", "stone"}, {"target", 1}, {"required", true}}}}
    };
    quests["any_harvest"] = {
        {"name", "Recolte"},
        {"auto_complete", false},
        {"min_level", 1},
        {"objectives", {{{"id", "any"}, {"type", "collect"},
                         {"entity", "harvest"}, {"target", 1}, {"required", true}}}}
    };
    quest::Registry::ResetForTests();
    quest::Registry::LoadDefinitionsFromSection(&quests);

    constexpr security::PlayerId pid = 96101;
    data::PlayerData player;
    player.id = pid;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(pid, "mine_metal").success());
    EXPECT(quest::Start(pid, "mine_stone").success());
    EXPECT(quest::Start(pid, "any_harvest").success());

    const std::string metalBp =
        "/Game/PrimalEarth/CoreBlueprints/Harvest/MetalRock.MetalRock";
    EXPECT(quest::ReportGameplay(pid, "collect",
        std::vector<std::string>{metalBp, "metal_rock", "harvest"}) == 2);

    auto load = data::PlayerStore::LoadDetailed(pid);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->quests["mine_metal"].objectives["get_metal"] == 1);
        EXPECT(load.data->quests["mine_stone"].objectives["get_stone"] == 0);
        EXPECT(load.data->quests["any_harvest"].objectives["any"] == 1);
    }

    quest::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

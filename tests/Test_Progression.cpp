// ============================================================================
// RPFramework - Tests B1 : progression métier
// ============================================================================
#include "TestHarness.h"

#include "Character/Registry.h"
#include "Core/Config.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Progression/Professions.h"
#include "Quest/Engine.h"
#include "Quest/Events.h"
#include "Quest/Registry.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include "json.hpp"

#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>

namespace
{
    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_b1_" + tag + "_" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void ConfigurePlayerStore(const std::filesystem::path& dir)
    {
        using namespace rpframework;
        data::PlayerStore::Shutdown();
        core::Config::Get().Set("data.save_dir", dir.string());
        core::Config::Get().Set("data.backup_count", 3);
        data::PlayerStore::LoadFromConfig();
    }

    void CleanupPlayerStore(const std::filesystem::path& dir)
    {
        rpframework::data::PlayerStore::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void LoadSmithProfession(int xpPerLevel = 100, int maxLevel = 5)
    {
        using namespace rpframework;
        nlohmann::json character;
        character["professions"]["blacksmith"] = {
            {"name", "Forgeron"},
            {"xp_per_level", xpPerLevel},
            {"max_level", maxLevel}
        };
        character::Registry::ResetForTests();
        character::Registry::LoadDefinitionsFromSection(&character);
    }
}

TEST(Progression_RejectsUnknownProfession)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("unknown");
    ConfigurePlayerStore(dir);
    LoadSmithProfession();

    data::PlayerData p;
    p.id = 97100;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = progression::AddProfessionXp(97100, "cook", 10, "test");
    EXPECT(!r.ok);
    EXPECT(progression::GetProfessionXp(97100, "cook") == 0);

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Progression_RejectsNonPositiveAmount)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("amount");
    ConfigurePlayerStore(dir);
    LoadSmithProfession();

    data::PlayerData p;
    p.id = 97101;
    EXPECT(data::PlayerStore::Save(p));

    EXPECT(!progression::AddProfessionXp(97101, "blacksmith", 0, "test").ok);
    EXPECT(!progression::AddProfessionXp(97101, "blacksmith", -5, "test").ok);
    EXPECT(progression::GetProfessionXp(97101, "blacksmith") == 0);
    EXPECT(progression::GetProfessionLevel(97101, "blacksmith") == 1);

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Progression_RejectsMissingPlayer)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("missing");
    ConfigurePlayerStore(dir);
    LoadSmithProfession();

    EXPECT(!progression::AddProfessionXp(97102, "blacksmith", 10, "test").ok);

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Progression_AddsXpWithoutLevelUp)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("nolevel");
    ConfigurePlayerStore(dir);
    LoadSmithProfession(100, 5);

    data::PlayerData p;
    p.id = 97103;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = progression::AddProfessionXp(97103, "blacksmith", 50, "craft");
    EXPECT(r.ok);
    EXPECT(!r.LeveledUp());
    EXPECT(r.level == 1);
    EXPECT(r.previousLevel == 1);
    EXPECT(r.xp == 50);
    EXPECT(r.skillPointsGained == 0);
    EXPECT(progression::GetProfessionLevel(97103, "blacksmith") == 1);
    EXPECT(progression::GetProfessionXp(97103, "blacksmith") == 50);

    auto load = data::PlayerStore::LoadDetailed(97103);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->level == 1);
        EXPECT(load.data->xp == 0);
        EXPECT(load.data->professions["blacksmith"].skillPoints == 0);
    }

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Progression_LevelsUpAndGrantsDefaultSkillPoint)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("levelup");
    ConfigurePlayerStore(dir);
    LoadSmithProfession(100, 10);
    core::Config::Get().Set("character.professions.blacksmith.skill_points_per_level", 1);

    data::PlayerData p;
    p.id = 97104;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = progression::AddProfessionXp(97104, "blacksmith", 250, "craft");
    EXPECT(r.ok);
    EXPECT(r.LeveledUp());
    EXPECT(r.previousLevel == 1);
    EXPECT(r.level == 3);
    EXPECT(r.xp == 250);
    EXPECT(r.skillPointsGained == 2);

    auto load = data::PlayerStore::LoadDetailed(97104);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->professions["blacksmith"].level == 3);
        EXPECT(load.data->professions["blacksmith"].skillPoints == 2);
    }

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Progression_RespectsSkillPointsPerLevelConfig)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("points");
    ConfigurePlayerStore(dir);
    LoadSmithProfession(100, 10);
    core::Config::Get().Set("character.professions.blacksmith.skill_points_per_level", 3);

    data::PlayerData p;
    p.id = 97105;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = progression::AddProfessionXp(97105, "blacksmith", 100, "craft");
    EXPECT(r.ok);
    EXPECT(r.level == 2);
    EXPECT(r.skillPointsGained == 3);

    character::Registry::Shutdown();
    core::Config::Get().Set("character.professions.blacksmith.skill_points_per_level", 1);
    CleanupPlayerStore(dir);
}

TEST(Progression_CapsAtMaxLevelAndNeverLowers)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("cap");
    ConfigurePlayerStore(dir);
    LoadSmithProfession(100, 3);

    data::PlayerData p;
    p.id = 97106;
    p.professions["blacksmith"].professionId = "blacksmith";
    p.professions["blacksmith"].level = 3;
    p.professions["blacksmith"].xp = 50;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = progression::AddProfessionXp(97106, "blacksmith", 500, "craft");
    EXPECT(r.ok);
    EXPECT(!r.LeveledUp());
    EXPECT(r.level == 3);
    EXPECT(r.previousLevel == 3);
    EXPECT(r.xp == 550);
    EXPECT(r.skillPointsGained == 0);

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Progression_RejectsXpOverflow)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("overflow");
    ConfigurePlayerStore(dir);
    LoadSmithProfession();

    data::PlayerData p;
    p.id = 97107;
    p.professions["blacksmith"].professionId = "blacksmith";
    p.professions["blacksmith"].xp = std::numeric_limits<int>::max() - 5;
    EXPECT(data::PlayerStore::Save(p));

    EXPECT(!progression::AddProfessionXp(97107, "blacksmith", 10, "craft").ok);
    EXPECT(progression::GetProfessionXp(97107, "blacksmith")
        == std::numeric_limits<int>::max() - 5);

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Progression_QuestXpDoesNotUseProfessionCurve)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_global");
    ConfigurePlayerStore(dir);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    // Courbe métier volontairement différente de 1000 : si le niveau global
    // la réutilise encore, 2500 XP donneraient le niveau 26 au lieu de 3.
    nlohmann::json character;
    character["professions"]["hunter"] = {
        {"name", "Chasseur"},
        {"xp_per_level", 100},
        {"max_level", 10}
    };
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&character);

    nlohmann::json section;
    section["hunt"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "k"}, {"type", "kill"},
                         {"entity", "wolf"}, {"target", 1}}}},
        {"rewards", {{{"type", "xp"}, {"id", "xp"}, {"amount", 2500}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);

    data::PlayerData player;
    player.id = 97108;
    player.profession = "hunter";
    player.level = 1;
    player.xp = 0;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(97108, "hunt").success());
    EXPECT(quest::ReportGameplay(97108, "kill", "wolf") == 1);
    EXPECT(quest::Complete(97108, "hunt").success());

    auto loaded = data::PlayerStore::LoadDetailed(97108);
    EXPECT(loaded.HasData());
    if (loaded.HasData())
    {
        EXPECT(loaded.data->xp == 2500);
        EXPECT(loaded.data->level == 3);
        EXPECT(loaded.data->professions.count("hunter") == 0);
    }

    quest::Registry::Shutdown();
    character::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

// ============================================================================
// RPFramework - Tests compétences + application d'effets
// ============================================================================
#include "TestHarness.h"

#include "Character/Registry.h"
#include "Core/Config.h"
#include "Crafting/Registry.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Effects/Apply.h"
#include "Effects/Registry.h"
#include "Loadout/AsaDeliver.h"
#include "Progression/Skills.h"
#include "Quest/Commands.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include "json.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace rpframework;

namespace
{

    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_skills_" + tag + "_" + std::to_string(std::rand()));
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
        progression::ResetSkillsForTests();
        effects::Registry::ResetForTests();
        crafting::Registry::ResetForTests();
        character::Registry::Shutdown();
        security::AuditLog::Shutdown();
        data::PlayerStore::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void Prepare()
    {
        security::Permissions::Initialize();
        security::Permissions::Register("skill.unlock", security::Level::PLAYER);
        security::RateLimiter::Initialize();
        security::AuditLog::Initialize();

        nlohmann::json character;
        character["professions"]["blacksmith"] = {
            {"name", "Forgeron"},
            {"xp_per_level", 100},
            {"max_level", 10}
        };
        character["professions"]["herbalist"] = {
            {"name", "Herboriste"},
            {"xp_per_level", 100},
            {"max_level", 10}
        };
        character::Registry::ResetForTests();
        character::Registry::LoadDefinitionsFromSection(&character);

        nlohmann::json effects;
        effects["focus"] = {
            {"name", "Focus"},
            {"type", "buff"},
            {"buff_blueprint", "/Game/Test/Buff_Focus.Buff_Focus"},
            {"duration_sec", 30},
            {"stacking", "refresh"},
            {"max_stacks", 1},
            {"cooldown_sec", 3600},
            {"modifiers", nlohmann::json::array({
                {{"target", "stamina"}, {"op", "add"}, {"value", 5.0}}
            })},
            {"conditions", {{"min_level", 1}}}
        };
        effects["once"] = {
            {"name", "Unique"},
            {"type", "buff"},
            {"buff_blueprint", "/Game/Test/Buff_Once.Buff_Once"},
            {"duration_sec", 10},
            {"stacking", "none"},
            {"max_stacks", 1},
            {"cooldown_sec", 0},
            {"modifiers", nlohmann::json::array({
                {{"target", "health"}, {"op", "add"}, {"value", 1.0}}
            })},
            {"conditions", {{"min_level", 1}}}
        };
        effects::Registry::ResetForTests();
        effects::Registry::LoadDefinitionsFromSection(&effects);

        nlohmann::json skills;
        skills["metallurgy"] = {
            {"name", "Metallurgie"},
            {"profession", "blacksmith"},
            {"cost", 1},
            {"min_level", 1},
            {"requires", nlohmann::json::array()},
            {"unlock_recipes", nlohmann::json::array({"master_blade"})},
            {"effect", "focus"}
        };
        skills["masterwork"] = {
            {"name", "Chef d'oeuvre"},
            {"profession", "blacksmith"},
            {"cost", 1},
            {"min_level", 1},
            {"requires", nlohmann::json::array({"metallurgy"})},
            {"effect", ""}
        };
        progression::ResetSkillsForTests();
        progression::LoadSkillsFromSection(&skills);
    }
}

TEST(Skills_UnlockSpendsPointsAndAppliesEffect)
{
    const auto dir = MakeTempPlayerDir("unlock");
    ConfigurePlayerStore(dir);
    Prepare();
    security::RateLimiter::Reset(98001, "skill.unlock");
    loadout::g_lastBuffBlueprint.clear();

    data::PlayerData p;
    p.id = 98001;
    p.profession = "blacksmith";
    p.professions["blacksmith"].professionId = "blacksmith";
    p.professions["blacksmith"].level = 2;
    p.professions["blacksmith"].skillPoints = 2;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = progression::UnlockSkill(98001, "metallurgy");
    EXPECT(r.ok);
    EXPECT(r.remainingPoints == 1);
    EXPECT(loadout::g_lastBuffBlueprint.find("Buff_Focus") != std::string::npos);

    auto load = data::PlayerStore::LoadDetailed(98001);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->professions["blacksmith"].skillPoints == 1);
        EXPECT(load.data->professions["blacksmith"].unlockedSkills.size() == 1);
        EXPECT(load.data->professions["blacksmith"].unlockedSkills[0] == "metallurgy");
        EXPECT(!load.data->effectCooldowns.empty());
    }

    CleanupAll(dir);
}

TEST(Skills_RejectsWrongProfessionMissingPointsAndPrereq)
{
    const auto dir = MakeTempPlayerDir("reject");
    ConfigurePlayerStore(dir);
    Prepare();
    security::RateLimiter::Reset(98002, "skill.unlock");
    security::RateLimiter::Reset(98003, "skill.unlock");

    data::PlayerData herbalist;
    herbalist.id = 98002;
    herbalist.profession = "herbalist";
    herbalist.professions["herbalist"].professionId = "herbalist";
    herbalist.professions["herbalist"].skillPoints = 5;
    EXPECT(data::PlayerStore::Save(herbalist));
    EXPECT(!progression::UnlockSkill(98002, "metallurgy").ok);

    data::PlayerData smith;
    smith.id = 98003;
    smith.profession = "blacksmith";
    smith.professions["blacksmith"].professionId = "blacksmith";
    smith.professions["blacksmith"].skillPoints = 0;
    EXPECT(data::PlayerStore::Save(smith));
    EXPECT(!progression::UnlockSkill(98003, "metallurgy").ok);

    smith.professions["blacksmith"].skillPoints = 2;
    EXPECT(data::PlayerStore::Save(smith));
    EXPECT(!progression::UnlockSkill(98003, "masterwork").ok);
    EXPECT(progression::UnlockSkill(98003, "metallurgy").ok);
    EXPECT(progression::UnlockSkill(98003, "masterwork").ok);

    CleanupAll(dir);
}

TEST(Effects_ApplyCooldownAndStackingNone)
{
    const auto dir = MakeTempPlayerDir("fx");
    ConfigurePlayerStore(dir);
    Prepare();
    loadout::g_lastBuffBlueprint.clear();

    data::PlayerData p;
    p.id = 98004;
    EXPECT(data::PlayerStore::Save(p));

    EXPECT(effects::Apply(98004, "focus").ok);
    EXPECT(loadout::g_lastBuffBlueprint.find("Buff_Focus") != std::string::npos);
    EXPECT(!effects::Apply(98004, "focus").ok);

    EXPECT(effects::Apply(98004, "once").ok);
    EXPECT(!effects::Apply(98004, "once").ok);

    CleanupAll(dir);
}

TEST(Skills_ChatUnlock)
{
    const auto dir = MakeTempPlayerDir("chat");
    ConfigurePlayerStore(dir);
    Prepare();
    security::RateLimiter::Reset(98005, "skill.unlock");

    data::PlayerData p;
    p.id = 98005;
    p.profession = "blacksmith";
    p.professions["blacksmith"].professionId = "blacksmith";
    p.professions["blacksmith"].skillPoints = 1;
    EXPECT(data::PlayerStore::Save(p));

    const auto listed = quest::HandleCommand(98005, {"skill", "list"});
    EXPECT(listed.success);
    EXPECT(listed.message.find("metallurgy") != std::string::npos);

    const auto unlocked = quest::HandleCommand(98005, {"skill", "unlock", "metallurgy"});
    EXPECT(unlocked.success);

    CleanupAll(dir);
}

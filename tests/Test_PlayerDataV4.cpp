// ============================================================================
// RPFramework - Tests PlayerData v4 : progression par métier (chantier A2)
//
// Couvre le schéma ProfessionProgression, la section JSON `professions`,
// la migration v3→v4 et le round-trip ToJson / FromJson. Aucune logique
// de gain d'XP (chantier B1).
// ============================================================================
#include "TestHarness.h"

#include "Core/Config.h"
#include "Data/Migration.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{
    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_a2_" + tag + "_" + std::to_string(std::rand()));
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

    bool FromJsonThrows(const nlohmann::json& j)
    {
        try
        {
            (void)rpframework::data::PlayerData::FromJson(j);
            return false;
        }
        catch (const std::exception&)
        {
            return true;
        }
    }

    nlohmann::json MakeMinimalPlayerJson(const std::string& id)
    {
        return {
            {"meta", {{"schema_version", rpframework::data::kPlayerDataSchemaVersion}}},
            {"identity", {{"id", id}}},
            {"character", nlohmann::json::object()},
            {"progression", {{"level", 1}, {"xp", 0}}},
        };
    }
}

TEST(PlayerDataV4_SchemaVersionIs4)
{
    using namespace rpframework::data;
    EXPECT(kPlayerDataSchemaVersion == 4);
    EXPECT(kCurrentSchemaVersion == 4);
    EXPECT(kPlayerDataSchemaVersion == kCurrentSchemaVersion);
}

TEST(PlayerDataV4_RoundTripMultipleProfessions)
{
    using namespace rpframework::data;

    PlayerData in;
    in.id = 4242001;
    in.name = "MultiMetier";
    in.profession = "blacksmith";
    in.level = 7;
    in.xp = 420;

    ProfessionProgression smith;
    smith.professionId = "blacksmith";
    smith.level = 12;
    smith.xp = 350;
    smith.skillPoints = 2;
    smith.unlockedSkills = {"metallurgy", "weaponsmith"};
    smith.unlockedRecipes = {"iron_ingot", "iron_sword"};
    in.professions["blacksmith"] = smith;

    ProfessionProgression cook;
    cook.professionId = "cook";
    cook.level = 8;
    cook.xp = 90;
    cook.skillPoints = 1;
    cook.unlockedRecipes = {"hunter_stew"};
    in.professions["cook"] = cook;

    ProfessionProgression herb;
    herb.professionId = "herbalist";
    herb.level = 5;
    herb.xp = 10;
    in.professions["herbalist"] = herb;

    const auto j = in.ToJson();
    EXPECT(j.contains("professions"));
    EXPECT(j["professions"].is_object());
    EXPECT(j["professions"].size() == 3);
    EXPECT(j.contains("character"));
    EXPECT(j["character"]["profession"] == "blacksmith");
    EXPECT(j["progression"]["level"] == 7);
    EXPECT(j["progression"]["xp"] == 420);

    const auto out = PlayerData::FromJson(j);
    EXPECT(out.id == in.id);
    EXPECT(out.name == in.name);
    EXPECT(out.profession == "blacksmith");
    EXPECT(out.level == 7);
    EXPECT(out.xp == 420);
    EXPECT(out.professions.size() == 3);

    EXPECT(out.professions.count("blacksmith") == 1);
    EXPECT(out.professions.at("blacksmith").professionId == "blacksmith");
    EXPECT(out.professions.at("blacksmith").level == 12);
    EXPECT(out.professions.at("blacksmith").xp == 350);
    EXPECT(out.professions.at("blacksmith").skillPoints == 2);
    EXPECT(out.professions.at("blacksmith").unlockedSkills.size() == 2);
    EXPECT(out.professions.at("blacksmith").unlockedSkills[0] == "metallurgy");
    EXPECT(out.professions.at("blacksmith").unlockedSkills[1] == "weaponsmith");
    EXPECT(out.professions.at("blacksmith").unlockedRecipes.size() == 2);
    EXPECT(out.professions.at("blacksmith").unlockedRecipes[0] == "iron_ingot");
    EXPECT(out.professions.at("blacksmith").unlockedRecipes[1] == "iron_sword");

    EXPECT(out.professions.count("cook") == 1);
    EXPECT(out.professions.at("cook").level == 8);
    EXPECT(out.professions.at("cook").xp == 90);
    EXPECT(out.professions.at("cook").skillPoints == 1);
    EXPECT(out.professions.at("cook").unlockedRecipes.size() == 1);
    EXPECT(out.professions.at("cook").unlockedRecipes[0] == "hunter_stew");
    EXPECT(out.professions.at("cook").unlockedSkills.empty());

    EXPECT(out.professions.count("herbalist") == 1);
    EXPECT(out.professions.at("herbalist").level == 5);
    EXPECT(out.professions.at("herbalist").xp == 10);
    EXPECT(out.professions.at("herbalist").skillPoints == 0);
    EXPECT(out.professions.at("herbalist").unlockedSkills.empty());
    EXPECT(out.professions.at("herbalist").unlockedRecipes.empty());
    EXPECT(!j["professions"]["herbalist"].contains("unlocked_skills"));
    EXPECT(!j["professions"]["herbalist"].contains("unlocked_recipes"));
}

TEST(PlayerDataV4_OmitsEmptyProfessionsSection)
{
    using namespace rpframework::data;
    PlayerData in;
    in.id = 4242002;
    in.level = 3;
    in.xp = 15;
    const auto j = in.ToJson();
    EXPECT(!j.contains("professions"));
    EXPECT(!j.contains("reputation"));
    EXPECT(!j.contains("titles"));

    const auto out = PlayerData::FromJson(j);
    EXPECT(out.professions.empty());
    EXPECT(out.level == 3);
    EXPECT(out.xp == 15);
}

TEST(PlayerDataV4_ProfessionFieldPreserved)
{
    using namespace rpframework::data;
    PlayerData in;
    in.id = 4242003;
    in.profession = "hunter";
    in.level = 4;
    in.xp = 80;

    const auto j = in.ToJson();
    EXPECT(j["character"]["profession"] == "hunter");
    EXPECT(!j.contains("professions"));

    const auto out = PlayerData::FromJson(j);
    EXPECT(out.profession == "hunter");
    EXPECT(out.professions.empty());
}

TEST(PlayerDataV4_FromJsonRejectsNegativeGlobalXp)
{
    auto j = MakeMinimalPlayerJson("4242004");
    j["identity"]["id"] = "4242004";
    j["progression"]["xp"] = -1;
    EXPECT(FromJsonThrows(j));
}

TEST(PlayerDataV4_FromJsonRejectsInvalidGlobalLevel)
{
    auto j = MakeMinimalPlayerJson("4242005");
    j["identity"]["id"] = "4242005";
    j["progression"]["level"] = 0;
    EXPECT(FromJsonThrows(j));
}

TEST(PlayerDataV4_FromJsonRejectsNegativeProfessionXp)
{
    auto j = MakeMinimalPlayerJson("4242006");
    j["identity"]["id"] = "4242006";
    j["professions"] = {
        {"blacksmith", {{"level", 2}, {"xp", -1}, {"skill_points", 0}}}
    };
    EXPECT(FromJsonThrows(j));
}

TEST(PlayerDataV4_FromJsonRejectsInvalidProfessionLevel)
{
    auto j = MakeMinimalPlayerJson("4242007");
    j["identity"]["id"] = "4242007";
    j["professions"] = {
        {"blacksmith", {{"level", 0}, {"xp", 10}, {"skill_points", 0}}}
    };
    EXPECT(FromJsonThrows(j));
}

TEST(PlayerDataV4_FromJsonRejectsCorruptProfessionEntry)
{
    auto j = MakeMinimalPlayerJson("4242008");
    j["identity"]["id"] = "4242008";
    j["professions"] = {{"blacksmith", "pas-un-objet"}};
    EXPECT(FromJsonThrows(j));
}

TEST(PlayerDataV4_MigrateV3ToV4CopiesProfessionProgression)
{
    using namespace rpframework::data;
    nlohmann::json v3 = {
        {"meta", {{"schema_version", 3}}},
        {"identity", {{"id", "4242009"}, {"name", "Forgeron"}}},
        {"character", {{"race", "human"}, {"profession", "blacksmith"}}},
        {"progression", {{"level", 12}, {"xp", 350}}},
        {"unlocks", nlohmann::json::array()},
    };

    EXPECT(Migrate(v3, 3) == true);
    EXPECT(v3["meta"]["schema_version"] == 4);
    EXPECT(v3["meta"]["schema_version"] == kCurrentSchemaVersion);
    EXPECT(v3["character"]["profession"] == "blacksmith");
    EXPECT(v3["progression"]["level"] == 12);
    EXPECT(v3["progression"]["xp"] == 350);
    EXPECT(v3.contains("professions"));
    EXPECT(v3["professions"].is_object());
    EXPECT(v3["professions"].contains("blacksmith"));
    EXPECT(v3["professions"]["blacksmith"]["level"] == 12);
    EXPECT(v3["professions"]["blacksmith"]["xp"] == 350);
    EXPECT(v3["professions"]["blacksmith"]["skill_points"] == 0);

    const auto loaded = PlayerData::FromJson(v3);
    EXPECT(loaded.profession == "blacksmith");
    EXPECT(loaded.level == 12);
    EXPECT(loaded.xp == 350);
    EXPECT(loaded.professions.size() == 1);
    EXPECT(loaded.professions.at("blacksmith").level == 12);
    EXPECT(loaded.professions.at("blacksmith").xp == 350);
    EXPECT(loaded.schemaVersion == 4);
}

TEST(PlayerDataV4_MigrateV3EmptyProfessionDoesNotInventEntry)
{
    using namespace rpframework::data;
    nlohmann::json v3 = {
        {"meta", {{"schema_version", 3}}},
        {"identity", {{"id", "4242010"}}},
        {"character", {{"race", "human"}}},
        {"progression", {{"level", 4}, {"xp", 9}}},
    };

    EXPECT(Migrate(v3, 3) == true);
    EXPECT(v3["meta"]["schema_version"] == 4);
    EXPECT(v3.contains("professions"));
    EXPECT(v3["professions"].is_object());
    EXPECT(v3["professions"].empty());
    EXPECT(!v3["character"].contains("profession"));
}

TEST(PlayerDataV4_MigrateDoesNotOverwriteExistingProfessionEntry)
{
    using namespace rpframework::data;
    nlohmann::json v3 = {
        {"meta", {{"schema_version", 3}}},
        {"identity", {{"id", "4242011"}}},
        {"character", {{"profession", "blacksmith"}}},
        {"progression", {{"level", 99}, {"xp", 1}}},
        {"professions", {
            {"blacksmith", {{"level", 3}, {"xp", 40}, {"skill_points", 1}}}
        }},
    };

    EXPECT(Migrate(v3, 3) == true);
    EXPECT(v3["professions"]["blacksmith"]["level"] == 3);
    EXPECT(v3["professions"]["blacksmith"]["xp"] == 40);
    EXPECT(v3["professions"]["blacksmith"]["skill_points"] == 1);
}

TEST(PlayerDataV4_MigrateAlreadyV4IsNoOp)
{
    using namespace rpframework::data;
    nlohmann::json current;
    current["meta"] = {{"schema_version", kCurrentSchemaVersion}};
    EXPECT(Migrate(current, kCurrentSchemaVersion) == false);
}

TEST(PlayerDataV4_V3FileMigratesAndResavesWithoutLoss)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("v3_roundtrip");
    ConfigurePlayerStore(dir);

    constexpr security::PlayerId pid = 4242012;
    const auto path = data::PlayerStore::GetFilePath(pid);
    {
        nlohmann::json v3 = {
            {"meta", {{"schema_version", 3}}},
            {"identity", {{"id", "4242012"}, {"name", "Veteran"}}},
            {"character", {{"race", "human"}, {"profession", "blacksmith"},
                           {"class", "warrior"}}},
            {"progression", {{"level", 12}, {"xp", 350}}},
            {"reputation", {{"town", 20}}},
            {"unlocks", nlohmann::json::array()},
        };
        std::ofstream out(path, std::ios::trunc);
        out << v3.dump(2);
    }

    const auto loaded = data::PlayerStore::LoadDetailed(pid);
    EXPECT(loaded.status == data::PlayerLoadStatus::Loaded);
    EXPECT(loaded.HasData());
    if (!loaded.HasData())
    {
        CleanupPlayerStore(dir);
        return;
    }

    EXPECT(loaded.data->profession == "blacksmith");
    EXPECT(loaded.data->playerClass == "warrior");
    EXPECT(loaded.data->level == 12);
    EXPECT(loaded.data->xp == 350);
    EXPECT(loaded.data->professions.count("blacksmith") == 1);
    EXPECT(loaded.data->professions.at("blacksmith").level == 12);
    EXPECT(loaded.data->professions.at("blacksmith").xp == 350);
    EXPECT(loaded.data->schemaVersion == 4);

    nlohmann::json persistedAfterLoad;
    {
        std::ifstream in(path);
        in >> persistedAfterLoad;
    }
    EXPECT(persistedAfterLoad["meta"]["schema_version"] == data::kCurrentSchemaVersion);
    EXPECT(persistedAfterLoad["character"]["profession"] == "blacksmith");
    EXPECT(persistedAfterLoad["professions"]["blacksmith"]["level"] == 12);
    EXPECT(persistedAfterLoad["professions"]["blacksmith"]["xp"] == 350);

    auto toSave = *loaded.data;
    toSave.professions["cook"].professionId = "cook";
    toSave.professions["cook"].level = 2;
    toSave.professions["cook"].xp = 15;
    EXPECT(data::PlayerStore::Save(toSave) == true);

    const auto reloaded = data::PlayerStore::LoadDetailed(pid);
    EXPECT(reloaded.HasData());
    if (reloaded.HasData())
    {
        EXPECT(reloaded.data->profession == "blacksmith");
        EXPECT(reloaded.data->level == 12);
        EXPECT(reloaded.data->xp == 350);
        EXPECT(reloaded.data->professions.size() == 2);
        EXPECT(reloaded.data->professions.at("blacksmith").level == 12);
        EXPECT(reloaded.data->professions.at("blacksmith").xp == 350);
        EXPECT(reloaded.data->professions.at("cook").level == 2);
        EXPECT(reloaded.data->professions.at("cook").xp == 15);
        EXPECT(reloaded.data->reputation.at("town") == 20);
    }

    CleanupPlayerStore(dir);
}

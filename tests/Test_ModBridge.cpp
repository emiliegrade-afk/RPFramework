// ============================================================================
// RPFramework - Tests D1 : canal console rpf (parsing + routing hors serveur)
// ============================================================================
#include "TestHarness.h"

#include "Character/Registry.h"
#include "Core/Config.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Mod/Bridge.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include "json.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
    using namespace rpframework;

    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_d1_" + tag + "_" + std::to_string(std::rand()));
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

    void LoadSampleCharacter()
    {
        nlohmann::json character;
        character["races"]["human"] = {{"name", "Humain"}};
        character["races"]["dwarf"] = {{"name", "Nain"}};
        character["professions"]["blacksmith"] = {{"name", "Forgeron"}};
        character["professions"]["guard"] = {{"name", "Garde"}};
        character::Registry::ResetForTests();
        character::Registry::LoadDefinitionsFromSection(&character);
    }

    void CleanupAll(const std::filesystem::path& dir)
    {
        character::Registry::Shutdown();
        security::AuditLog::Shutdown();
        data::PlayerStore::Shutdown();
        mod::ResetForTests();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
}

TEST(D1_Tokenize_GuillemetsEtChemins)
{
    const auto simple = mod::Tokenize("rpf ping");
    EXPECT(simple.size() == 2);
    EXPECT(simple[0] == "rpf");
    EXPECT(simple[1] == "ping");

    const auto quoted = mod::Tokenize(R"(rpf mod get "character.races.human")");
    EXPECT(quoted.size() == 4);
    EXPECT(quoted[0] == "rpf");
    EXPECT(quoted[1] == "mod");
    EXPECT(quoted[2] == "get");
    EXPECT(quoted[3] == "character.races.human");

    const auto path = mod::Tokenize(
        R"(rpf mod get /Game/Mods/RPFramework/Stations/MedievalForge.MedievalForge)");
    EXPECT(path.size() == 4);
    EXPECT(path[3] == "/Game/Mods/RPFramework/Stations/MedievalForge.MedievalForge");

    const auto spacedPath = mod::Tokenize(
        R"(rpf station inspect "/Game/Mods/My Mod/Forge.Forge")");
    EXPECT(spacedPath.size() == 4);
    EXPECT(spacedPath[3] == "/Game/Mods/My Mod/Forge.Forge");

    const auto escaped = mod::Tokenize(R"(rpf auth "code\"secret")");
    EXPECT(escaped.size() == 3);
    EXPECT(escaped[2] == "code\"secret");

    const auto extraSpaces = mod::Tokenize("  rpf   race   select   human  ");
    EXPECT(extraSpaces.size() == 4);
    EXPECT(extraSpaces[3] == "human");

    const auto unclosed = mod::Tokenize(R"(rpf mod get "character.races)");
    EXPECT(unclosed.size() == 4);
    EXPECT(unclosed[3] == "character.races");
}

TEST(D1_Ping_Succes)
{
    mod::ResetForTests();
    security::Permissions::Initialize();
    const auto result = mod::Execute(71001, "rpf ping");
    EXPECT(result.handled);
    EXPECT(result.success);
    EXPECT(result.message == "RPFramework OK");

    const auto sansPrefixe = mod::Execute(71001, "ping");
    EXPECT(sansPrefixe.success);
    EXPECT(sansPrefixe.message == "RPFramework OK");
}

TEST(D1_RaceSelect_SansPermission)
{
    mod::ResetForTests();
    security::Permissions::Initialize();
    security::Permissions::Register("rpf.race.select", security::Level::OWNER);

    const auto denied = mod::Execute(71002, "rpf race select human");
    EXPECT(denied.handled);
    EXPECT(!denied.success);
    EXPECT(denied.message.find("permission") != std::string::npos);

    security::Permissions::Register("rpf.race.select", security::Level::PLAYER);
}

TEST(D1_RaceSelect_MauvaisId)
{
    mod::ResetForTests();
    security::Permissions::Initialize();
    LoadSampleCharacter();

    const auto bad = mod::Execute(71003, "rpf race select bad@id");
    EXPECT(bad.handled);
    EXPECT(!bad.success);
    EXPECT(bad.message.find("invalide") != std::string::npos);

    character::Registry::Shutdown();
}

TEST(D1_ModGet_RefuseSiPasModerator)
{
    mod::ResetForTests();
    security::Permissions::Initialize();
    core::Config::Get().Set("debug", true);

    const auto denied = mod::Execute(71004, "rpf mod get debug");
    EXPECT(denied.handled);
    EXPECT(!denied.success);
    EXPECT(denied.message.find("permission") != std::string::npos);
}

TEST(D1_ModGet_OkApresAuth)
{
    mod::ResetForTests();
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();
    core::Config::Get().Set("security.admin_code", "s3cret-d1");
    core::Config::Get().Set("debug", true);
    security::RateLimiter::Reset(71005, "rpf.auth");

    const auto badAuth = mod::Execute(71005, "rpf auth wrong");
    EXPECT(!badAuth.success);
    EXPECT(badAuth.message.find("invalide") != std::string::npos);
    EXPECT(!mod::IsSessionAdmin(71005));

    const auto auth = mod::Execute(71005, "rpf auth s3cret-d1");
    EXPECT(auth.success);
    EXPECT(auth.message == "auth ok");
    EXPECT(mod::IsSessionAdmin(71005));

    const auto got = mod::Execute(71005, "rpf mod get debug");
    EXPECT(got.success);
    EXPECT(got.message.find("true") != std::string::npos);

    const auto quoted = mod::Execute(71005, R"(rpf mod get "debug")");
    EXPECT(quoted.success);

    const auto mutated = mod::Execute(71005, "rpf mod set debug false");
    EXPECT(mutated.handled);
    EXPECT(!mutated.success);
    EXPECT(mutated.message.find("rpf mod get") != std::string::npos);

    core::Config::Get().Set("security.admin_code", "");
    mod::ClearSessionAuth(71005);
    security::AuditLog::Shutdown();
}

TEST(D1_Auth_DesactiveeSansCode)
{
    mod::ResetForTests();
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    core::Config::Get().Set("security.admin_code", "");
    security::RateLimiter::Reset(71006, "rpf.auth");

    const auto result = mod::Execute(71006, "rpf auth anything");
    EXPECT(!result.success);
    EXPECT(result.message.find("desactivee") != std::string::npos);
}

TEST(D1_RaceList_EtPlayerStatus)
{
    mod::ResetForTests();
    security::Permissions::Initialize();
    LoadSampleCharacter();

    const auto dir = MakeTempPlayerDir("status");
    ConfigurePlayerStore(dir);

    const auto list = mod::Execute(71007, "rpf race list");
    EXPECT(list.success);
    EXPECT(list.message.find("human") != std::string::npos);

    const auto jobs = mod::Execute(71007, "rpf job list");
    EXPECT(jobs.success);
    EXPECT(jobs.message.find("blacksmith") != std::string::npos);

    data::PlayerData data;
    data.id = 71007;
    data.race = "human";
    data.profession = "blacksmith";
    data.level = 3;
    data.xp = 40;
    data.professions["blacksmith"].professionId = "blacksmith";
    data.professions["blacksmith"].level = 2;
    data.professions["blacksmith"].xp = 15;
    EXPECT(data::PlayerStore::Save(data));

    const auto status = mod::Execute(71007, "rpf player status");
    EXPECT(status.success);
    EXPECT(status.message.find("\"race\":\"human\"") != std::string::npos);
    EXPECT(status.message.find("\"profession\":\"blacksmith\"") != std::string::npos);
    EXPECT(status.message.find("\"level\":3") != std::string::npos);
    EXPECT(status.message.find("\"job_level\":2") != std::string::npos);

    CleanupAll(dir);
}

TEST(D1_ModuleInconnu_ErreurPropre)
{
    mod::ResetForTests();
    const auto result = mod::Execute(71008, "rpf widgets open journal");
    EXPECT(result.handled);
    EXPECT(!result.success);
    EXPECT(result.message.find("inconnu") != std::string::npos);
}

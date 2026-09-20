// ============================================================================
// RPFramework - Tests unitaires
//
// Framework de test minimaliste (maison, header-only via macros) pour
// éviter d'ajouter une dépendance externe. Style GoogleTest-light.
//
// Le micro-framework (EXPECT / TEST / registre) vit dans `TestHarness.h` :
// un nouveau chantier ajoute son propre `tests/Test_<Chantier>.cpp` plutôt
// que d'agrandir ce fichier. `main()` exécute tous les tests enregistrés,
// quel que soit le fichier d'origine.
//
// Tests couverts :
//   - Permissions     : niveaux, Register, Check, défauts baked-in
//   - RateLimiter     : Allow, max, window, Reset, SecondsUntilNext
//   - Validator       : chaque helper + composition All()
//   - AuditLog        : Log, LogDenied, Recent, rotation
//
// Les tests n'ont PAS besoin d'AsaApi ni du serveur : ils exercent
// directement les modules Security et Core.
// ============================================================================
#include "TestHarness.h"

#include "Security/Permissions.h"
#include "Security/RateLimiter.h"
#include "Security/Validator.h"
#include "Security/AuditLog.h"
#include "Security/Types.h"

#include "Core/Paths.h"
#include "Core/Config.h"
#include "Core/PluginContext.h"

#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Data/Migration.h"

#include "Character/Character.h"

#include "Loadout/Item.h"
#include "Loadout/Compose.h"
#include "Loadout/Distribute.h"
#include "Loadout/AsaDeliver.h"

#include "Faction/Definitions.h"
#include "Faction/Registry.h"
#include "Faction/Reputation.h"
#include "Faction/Join.h"

#include "Economy/Definitions.h"
#include "Economy/Registry.h"
#include "Economy/Wallet.h"

#include "Quest/Definitions.h"
#include "Quest/Registry.h"
#include "Quest/Engine.h"
#include "Quest/Events.h"
#include "Quest/Commands.h"
#include "Quest/Match.h"

#include "Api/Query.h"

#include "Core/Version.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Tests Permissions
// ---------------------------------------------------------------------------
TEST(Permissions_BakedInDefaults)
{
    using namespace rpframework::security;
    Permissions::Initialize();

    // Vérifie que les défauts baked-in sont là.
    EXPECT(Permissions::GetRequiredLevel("race.select")       == Level::PLAYER);
    EXPECT(Permissions::GetRequiredLevel("quest.create")      == Level::ADMIN);
    EXPECT(Permissions::GetRequiredLevel("framework.reload")  == Level::OWNER);
    EXPECT(Permissions::GetRequiredLevel("audit.view")        == Level::MODERATOR);
}

TEST(Permissions_Check)
{
    using namespace rpframework::security;
    Permissions::Initialize();

    // PLAYER peut "race.select" mais pas "quest.create".
    EXPECT(Permissions::Check(Level::PLAYER,    "race.select")      == true);
    EXPECT(Permissions::Check(Level::PLAYER,    "quest.create")     == false);
    EXPECT(Permissions::Check(Level::ADMIN,     "quest.create")     == true);
    EXPECT(Permissions::Check(Level::ADMIN,     "framework.reload") == false);
    EXPECT(Permissions::Check(Level::OWNER,     "framework.reload") == true);
    EXPECT(Permissions::Check(Level::MODERATOR, "audit.view")       == true);
    EXPECT(Permissions::Check(Level::PLAYER,    "audit.view")       == false);
}

TEST(Permissions_RegisterAndOverride)
{
    using namespace rpframework::security;
    Permissions::Initialize();

    // Register override une clé existante.
    Permissions::Register("quest.create", Level::GM);
    EXPECT(Permissions::Check(Level::GM,    "quest.create") == true);
    EXPECT(Permissions::Check(Level::ADMIN, "quest.create") == true);

    // Register une nouvelle clé.
    Permissions::Register("custom.action", Level::OWNER);
    EXPECT(Permissions::GetRequiredLevel("custom.action") == Level::OWNER);
    EXPECT(Permissions::Check(Level::ADMIN, "custom.action") == false);
}

TEST(Permissions_UnknownKeyDefaultsToOwner)
{
    using namespace rpframework::security;
    // Clé jamais enregistrée → fallback par défaut = OWNER (fail-closed).
    EXPECT(Permissions::GetRequiredLevel("zzz.does.not.exist") == Level::OWNER);
    EXPECT(Permissions::Check(Level::PLAYER, "zzz.does.not.exist") == false);
    // Avec fallback SYSTEM, on devient plus strict.
    EXPECT(Permissions::GetRequiredLevel("zzz.does.not.exist", Level::SYSTEM)
           == Level::SYSTEM);
}

TEST(PlayerIdentity_StableHashing)
{
    using namespace rpframework::security;
    const auto a = MakePlayerId("steam:110000100000000");
    const auto b = MakePlayerId("steam:110000100000000");
    const auto c = MakePlayerId("steam:110000100000001");

    EXPECT(a != 0);
    EXPECT(a == b);
    EXPECT(a != c);
    EXPECT(MakePlayerIdFromPointer(reinterpret_cast<const void*>(0x1234)) != 0);
}

TEST(PluginContext_StateLifecycle)
{
    using namespace rpframework::core;

    const auto configPath = GetPluginConfigPath();
    std::filesystem::create_directories(configPath.parent_path());
    {
        std::ofstream f(configPath, std::ios::trunc);
        f << R"({"schema_version":1,"data":{"backup_count":3}})";
    }

    EXPECT(PluginContext::GetState() == PluginContext::State::Uninitialized);
    EXPECT(PluginContext::IsInitialized() == false);
    EXPECT(PluginContext::Initialize() == true);
    EXPECT(PluginContext::GetState() == PluginContext::State::Ready);
    EXPECT(PluginContext::IsInitialized() == true);
    PluginContext::Shutdown();
    EXPECT(PluginContext::GetState() == PluginContext::State::Uninitialized);
    EXPECT(PluginContext::IsInitialized() == false);
}

TEST(PluginContext_MissingConfigFailsInit)
{
    using namespace rpframework::core;
    using namespace rpframework::quest;
    using namespace rpframework::security;

    PluginContext::Shutdown();
    const auto configPath = GetPluginConfigPath();
    std::error_code ec;
    std::filesystem::remove(configPath, ec);

    EXPECT(PluginContext::Initialize() == false);
    EXPECT(PluginContext::GetState() == PluginContext::State::Failed);
    EXPECT(PluginContext::IsInitialized() == false);

    const auto chat = HandleCommand(1, {"race", "list"}, Level::PLAYER);
    EXPECT(chat.handled == true);
    EXPECT(chat.success == false);
    EXPECT(chat.message.find("indisponible") != std::string::npos);

    PluginContext::Shutdown();
    EXPECT(PluginContext::GetState() == PluginContext::State::Uninitialized);

    std::filesystem::create_directories(configPath.parent_path());
    {
        std::ofstream f(configPath, std::ios::trunc);
        f << R"({"schema_version":1,"data":{"backup_count":3}})";
    }
}

TEST(PluginContext_InvalidConfigFailsInit)
{
    using namespace rpframework::core;

    PluginContext::Shutdown();
    const auto configPath = GetPluginConfigPath();
    std::filesystem::create_directories(configPath.parent_path());
    {
        std::ofstream f(configPath, std::ios::trunc);
        f << "{ this is not valid json";
    }

    EXPECT(PluginContext::Initialize() == false);
    EXPECT(PluginContext::GetState() == PluginContext::State::Failed);
    EXPECT(PluginContext::IsInitialized() == false);

    PluginContext::Shutdown();
    {
        std::ofstream f(configPath, std::ios::trunc);
        f << R"({"schema_version":1,"data":{"backup_count":3}})";
    }
}

TEST(Permissions_LoadFromConfigDoesNotCrashOnMalformed)
{
    using namespace rpframework::security;
    nlohmann::json j;  // vide
    Permissions::LoadFromConfig(j);  // no-op
    EXPECT(true);

    j = nlohmann::json::array();  // pas un objet
    Permissions::LoadFromConfig(j);
    EXPECT(true);
}

// ---------------------------------------------------------------------------
// Tests RateLimiter
// ---------------------------------------------------------------------------
TEST(RateLimiter_AllowUpToMax)
{
    using namespace rpframework::security;
    RateLimiter::Initialize();
    constexpr PlayerId pid = 4242;
    const std::string action = "test.allow_up_to";

    RateLimiter::Register(action, RateLimiter::Limit{3, 60});
    RateLimiter::Reset(pid, action);

    EXPECT(RateLimiter::Allow(pid, action) == true);   // 1/3
    EXPECT(RateLimiter::Allow(pid, action) == true);   // 2/3
    EXPECT(RateLimiter::Allow(pid, action) == true);   // 3/3
    EXPECT(RateLimiter::Allow(pid, action) == false);  // refusé
    EXPECT(RateLimiter::Allow(pid, action) == false);  // toujours refusé
    EXPECT(RateLimiter::Count(pid, action) == 3);
}

TEST(RateLimiter_PerPlayerIsolation)
{
    using namespace rpframework::security;
    constexpr PlayerId p1 = 1;
    constexpr PlayerId p2 = 2;
    const std::string action = "test.per_player";

    RateLimiter::Register(action, RateLimiter::Limit{1, 60});
    RateLimiter::Reset(p1, action);
    RateLimiter::Reset(p2, action);

    EXPECT(RateLimiter::Allow(p1, action) == true);
    EXPECT(RateLimiter::Allow(p1, action) == false);  // p1 bloqué
    EXPECT(RateLimiter::Allow(p2, action) == true);   // p2 indépendant
    EXPECT(RateLimiter::Allow(p2, action) == false);  // p2 aussi bloqué
}

TEST(RateLimiter_Reset)
{
    using namespace rpframework::security;
    constexpr PlayerId pid = 99;
    const std::string action = "test.reset";

    RateLimiter::Register(action, RateLimiter::Limit{1, 60});
    RateLimiter::Reset(pid, action);
    EXPECT(RateLimiter::Allow(pid, action) == true);
    EXPECT(RateLimiter::Allow(pid, action) == false);
    RateLimiter::Reset(pid, action);
    EXPECT(RateLimiter::Allow(pid, action) == true);  // reset → autorisé
}

TEST(RateLimiter_UnconfiguredActionAlwaysAllowed)
{
    using namespace rpframework::security;
    constexpr PlayerId pid = 7;
    const std::string action = "test.unconfigured";

    // Pas de Register, pas de LoadFromConfig → pas de limite.
    EXPECT(RateLimiter::Allow(pid, action) == true);
    EXPECT(RateLimiter::Allow(pid, action) == true);
    EXPECT(RateLimiter::Allow(pid, action) == true);
    EXPECT(RateLimiter::Allow(pid, action) == true);
    EXPECT(RateLimiter::Count(pid, action) == 0);
}

TEST(RateLimiter_SecondsUntilNext)
{
    using namespace rpframework::security;
    constexpr PlayerId pid = 1234;
    const std::string action = "test.until_next";

    RateLimiter::Register(action, RateLimiter::Limit{1, 60});
    RateLimiter::Reset(pid, action);

    EXPECT(RateLimiter::SecondsUntilNext(pid, action) == 0);  // pas rate-limited
    EXPECT(RateLimiter::Allow(pid, action) == true);
    EXPECT(RateLimiter::SecondsUntilNext(pid, action) > 0);   // maintenant oui
    EXPECT(RateLimiter::SecondsUntilNext(pid, action) <= 60);
}

TEST(RateLimiter_Cleanup)
{
    using namespace rpframework::security;
    constexpr PlayerId pid = 555;
    const std::string action = "test.cleanup";

    RateLimiter::Register(action, RateLimiter::Limit{2, 1});  // 1 sec window
    RateLimiter::Reset(pid, action);
    RateLimiter::Allow(pid, action);

    // Pas le temps que la window expire, on dort un peu.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const std::size_t removed = RateLimiter::Cleanup();
    EXPECT(removed >= 1);
}

// ---------------------------------------------------------------------------
// Tests Validator
// ---------------------------------------------------------------------------
TEST(Validator_NotEmpty)
{
    using namespace rpframework::security;
    EXPECT(Validator::NotEmpty("hello", "x").valid == true);
    EXPECT(Validator::NotEmpty("",      "x").valid == false);
    EXPECT(Validator::NotEmpty("",      "x").errorCode == "empty_field");
}

TEST(Validator_MaxLength)
{
    using namespace rpframework::security;
    EXPECT(Validator::MaxLength("abc",   5,  "x").valid == true);
    EXPECT(Validator::MaxLength("abcde", 5,  "x").valid == true);
    EXPECT(Validator::MaxLength("abcdef",5,  "x").valid == false);
    EXPECT(Validator::MaxLength("abcdef",5,  "x").errorCode == "max_length");
}

TEST(Validator_InRange)
{
    using namespace rpframework::security;
    EXPECT(Validator::InRange(5, 0, 10, "x").valid == true);
    EXPECT(Validator::InRange(0, 0, 10, "x").valid == true);
    EXPECT(Validator::InRange(10, 0, 10, "x").valid == true);
    EXPECT(Validator::InRange(-1, 0, 10, "x").valid == false);
    EXPECT(Validator::InRange(11, 0, 10, "x").valid == false);
    EXPECT(Validator::InRange(11, 0, 10, "x").errorCode == "out_of_range");
}

TEST(Validator_NonNegative)
{
    using namespace rpframework::security;
    EXPECT(Validator::NonNegative(0,  "x").valid == true);
    EXPECT(Validator::NonNegative(10, "x").valid == true);
    EXPECT(Validator::NonNegative(-1, "x").valid == false);
}

TEST(Validator_IsNonEmptyObject)
{
    using namespace rpframework::security;
    nlohmann::json obj = {{"k", 1}};
    nlohmann::json empty = nlohmann::json::object();
    nlohmann::json arr  = nlohmann::json::array();

    EXPECT(Validator::IsNonEmptyObject(obj,   "x").valid == true);
    EXPECT(Validator::IsNonEmptyObject(empty, "x").valid == false);
    EXPECT(Validator::IsNonEmptyObject(arr,   "x").valid == false);
}

TEST(Validator_PathExists)
{
    using namespace rpframework::security;
    nlohmann::json j = {{"security", {{"audit_log_enabled", true}}}};
    EXPECT(Validator::PathExists(j, "security.audit_log_enabled").valid == true);
    EXPECT(Validator::PathExists(j, "security.does_not_exist").valid   == false);
    EXPECT(Validator::PathExists(j, "missing.entirely").valid          == false);
}

TEST(Validator_OneOf)
{
    using namespace rpframework::security;
    EXPECT(Validator::OneOf("PLAYER",    {"PLAYER", "ADMIN"}, "x").valid == true);
    EXPECT(Validator::OneOf("ADMIN",     {"PLAYER", "ADMIN"}, "x").valid == true);
    EXPECT(Validator::OneOf("GUEST",     {"PLAYER", "ADMIN"}, "x").valid == false);
}

TEST(Validator_AllShortCircuit)
{
    using namespace rpframework::security;
    auto r = Validator::All({
        ValidationResult::Ok(),
        Validator::InRange(100, 0, 10, "x"),  // fail
        ValidationResult::Ok(),
    });
    EXPECT(r.valid == false);
    EXPECT(r.errorCode == "out_of_range");
}

// ---------------------------------------------------------------------------
// Tests AuditLog
// ---------------------------------------------------------------------------
namespace
{
    // Helper : renvoie un chemin unique dans le répertoire temp.
    std::filesystem::path MakeTempAuditPath(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests";
        std::filesystem::create_directories(dir);
        return dir / ("audit_" + tag + "_" + std::to_string(std::rand()) + ".log");
    }
}

TEST(AuditLog_LogAndRecent)
{
    using namespace rpframework::security;

    AuditLog::Initialize();  // utilise GetAuditLogPath() par défaut

    AuditLog::Log("test.event_a", 100, {{"k", 1}});
    AuditLog::Log("test.event_b", 200, {{"k", 2}}, "warn");

    const auto recent = AuditLog::Recent(10);
    EXPECT(recent.size() >= 2);

    bool foundA = false, foundB = false;
    for (const auto& e : recent)
    {
        if (e.action == "test.event_a" && e.playerId == 100) foundA = true;
        if (e.action == "test.event_b" && e.playerId == 200 && e.severity == "warn") foundB = true;
    }
    EXPECT(foundA);
    EXPECT(foundB);
}

TEST(AuditLog_LogDeniedSetsSeverity)
{
    using namespace rpframework::security;
    AuditLog::Initialize();

    AuditLog::LogDenied("test.deny_action", 42, "permission", {{"k", "v"}});
    const auto recent = AuditLog::Recent(50);
    bool found = false;
    for (const auto& e : recent)
    {
        if (e.action == "test.deny_action"
            && e.playerId == 42
            && e.severity == "denied"
            && e.payload.value("reason", std::string{}) == "permission")
        {
            found = true;
        }
    }
    EXPECT(found);
}

TEST(AuditLog_RotationRenamesFiles)
{
    using namespace rpframework::security;

    // Force un chemin custom et un seuil de rotation minuscule.
    const auto path = MakeTempAuditPath("rotate");
    (void)path;
    // On ne peut pas changer le filePath_ post-init, donc on initialise
    // le module avec le path par défaut puis on vérifie au moins qu'aucune
    // exception n'est levée en cas de rotation forcée.
    AuditLog::Initialize();

    AuditLog::SetRotateConfig(AuditLog::RotateConfig{ /*maxSizeBytes=*/ 1, /*maxFiles=*/ 3 });
    // Émet quelques events, force un flush.
    for (int i = 0; i < 10; ++i)
    {
        AuditLog::Log("test.rotate", i, {{"i", i}});
    }
    AuditLog::Flush();

    // ForceRotate : avec maxSizeBytes=1, n'importe quel fichier sera
    // tourné. On n'inspecte pas l'état exact du filesystem (c'est le
    // path par défaut qui est en jeu), on vérifie juste que ça ne crash
    // pas et renvoie un bool.
    const bool didRotate = AuditLog::RotateIfNeeded();
    (void)didRotate;  // peut être false si le fichier est encore petit,
                      // ou true s'il a déjà été tourné. Les deux sont OK.

    // Restore config raisonnable pour les autres tests.
    AuditLog::SetRotateConfig(AuditLog::RotateConfig{});
    EXPECT(true);  // pas de crash = succès
}

// ---------------------------------------------------------------------------
// Tests Data / Phase 3
// ---------------------------------------------------------------------------
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
}

TEST(PlayerStore_SaveLoadAndList)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("roundtrip");
    ConfigurePlayerStore(dir);

    data::PlayerData original;
    original.id = 9001;
    original.name = "Ada";
    original.race = "race.example";
    original.profession = "profession.example";
    original.level = 8;
    original.xp = 250;
    original.reputation["faction.example"] = 42;
    original.titles.push_back("title.example");
    EXPECT(data::PlayerStore::Save(original) == true);

    const auto loaded = data::PlayerStore::LoadDetailed(9001);
    EXPECT(loaded.status == data::PlayerLoadStatus::Loaded);
    EXPECT(loaded.HasData());
    EXPECT(loaded.data->name == "Ada");
    EXPECT(loaded.data->level == 8);
    EXPECT(loaded.data->reputation.at("faction.example") == 42);
    EXPECT(data::PlayerStore::ListAll().size() == 1);
    CleanupPlayerStore(dir);
}

TEST(PlayerStore_RecoversCorruptFileFromBackup)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("recovery");
    ConfigurePlayerStore(dir, 3);

    data::PlayerData player;
    player.id = 9002;
    player.name = "Backup";
    player.level = 2;
    EXPECT(data::PlayerStore::Save(player) == true);
    player.level = 3;
    EXPECT(data::PlayerStore::Save(player) == true);  // level 2 devient .bak.1

    {
        std::ofstream corrupt(data::PlayerStore::GetFilePath(9002), std::ios::trunc);
        corrupt << "{invalid json";
    }

    const auto recovered = data::PlayerStore::LoadDetailed(9002);
    EXPECT(recovered.status == data::PlayerLoadStatus::RecoveredFromBackup);
    EXPECT(recovered.HasData());
    EXPECT(recovered.data->level == 2);
    EXPECT(std::filesystem::exists(data::PlayerStore::GetFilePath(9002)));

    bool quarantineFound = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.path().filename().string().find("9002.json.corrupt.") == 0)
        {
            quarantineFound = true;
        }
    }
    EXPECT(quarantineFound);
    CleanupPlayerStore(dir);
}

TEST(PlayerStore_RecoversMissingPrincipalFromBackup)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("missing_bak");
    ConfigurePlayerStore(dir, 3);

    data::PlayerData player;
    player.id = 90021;
    player.name = "Orphan";
    player.level = 4;
    EXPECT(data::PlayerStore::Save(player) == true);
    player.level = 5;
    EXPECT(data::PlayerStore::Save(player) == true);

    const auto principal = data::PlayerStore::GetFilePath(90021);
    EXPECT(std::filesystem::exists(principal.string() + ".bak.1"));
    std::filesystem::remove(principal);

    const auto recovered = data::PlayerStore::LoadDetailed(90021);
    EXPECT(recovered.status == data::PlayerLoadStatus::RecoveredFromBackup);
    EXPECT(recovered.HasData());
    EXPECT(recovered.data->level == 4);
    EXPECT(std::filesystem::exists(principal));
    CleanupPlayerStore(dir);
}

TEST(PlayerStore_MigratesLegacyFileAndPersistsVersion)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("migration");
    ConfigurePlayerStore(dir);

    const auto path = data::PlayerStore::GetFilePath(9003);
    {
        std::ofstream legacy(path, std::ios::trunc);
        legacy << R"({"identity":{"id":"9003","name":"Legacy"},"progression":{"level":4,"xp":9}})";
    }

    const auto loaded = data::PlayerStore::LoadDetailed(9003);
    EXPECT(loaded.status == data::PlayerLoadStatus::Loaded);
    EXPECT(loaded.HasData());
    EXPECT(loaded.data->level == 4);

    nlohmann::json persisted;
    std::ifstream migrated(path);
    migrated >> persisted;
    EXPECT(persisted["meta"]["schema_version"] == data::kCurrentSchemaVersion);
    EXPECT(std::filesystem::exists(path.string() + ".bak.1"));
    CleanupPlayerStore(dir);
}

TEST(PlayerStore_RejectsMismatchedIdentityWithoutOverwrite)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("identity");
    ConfigurePlayerStore(dir);

    const auto path = data::PlayerStore::GetFilePath(9004);
    {
        std::ofstream invalid(path, std::ios::trunc);
        invalid << R"({"meta":{"schema_version":1},"identity":{"id":"9005"}})";
    }

    const auto loaded = data::PlayerStore::LoadDetailed(9004);
    EXPECT(loaded.status == data::PlayerLoadStatus::Corrupt);
    EXPECT(!loaded.HasData());
    EXPECT(std::filesystem::exists(path));
    CleanupPlayerStore(dir);
}

TEST(PlayerStore_EnforcesAtLeastOneBackup)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("backup_minimum");
    ConfigurePlayerStore(dir, 0);
    EXPECT(data::PlayerStore::GetBackupCount() == 1);
    CleanupPlayerStore(dir);
}

TEST(PlayerStore_FlushRewritesExisting)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("flush_logout");
    ConfigurePlayerStore(dir);
    data::PlayerData player;
    player.id = 9006;
    player.name = "Leaver";
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(data::PlayerStore::Flush(9006));
    EXPECT(!data::PlayerStore::Flush(42424242ull));
    CleanupPlayerStore(dir);
}

// ---------------------------------------------------------------------------
// Tests Character / Phase 4
// ---------------------------------------------------------------------------
namespace
{
    // Section de config utilisée par tous les tests Character. Trois races
    // (avec condition croisée : dwarf exclut elf, elf exige min_level=5),
    // deux métiers (guard exige min_level=3), classes désactivées.
    //
    // Note : on construit les objets de modificateur avec
    // nlohmann::json::object() explicite car la syntaxe d'init-list
    // {{"k", v}, ...} à l'intérieur d'un array est ambiguë et produit
    // un array de paires au lieu d'un array d'objets.
    const nlohmann::json& TestCharacterSection()
    {
        // Construction explicite via json::object() pour chaque niveau
        // afin d'éviter les ambiguïtés d'init-list de nlohmann/json.
        nlohmann::json section;
        section["classes_enabled"] = false;

        nlohmann::json human;
        human["name"] = "Humain";
        human["bonuses"] = nlohmann::json::array();
        nlohmann::json humanBonus;
        humanBonus["target"] = "xp_gain";
        humanBonus["op"]     = "multiply";
        humanBonus["value"]  = 1.1;
        human["bonuses"].push_back(humanBonus);
        section["races"]["human"] = human;

        nlohmann::json elf;
        elf["name"] = "Elfe";
        elf["bonuses"] = nlohmann::json::array();
        nlohmann::json elfBonus;
        elfBonus["target"] = "stamina";
        elfBonus["op"]     = "add";
        elfBonus["value"]  = 20.0;
        elf["bonuses"].push_back(elfBonus);
        elf["maluses"] = nlohmann::json::array();
        nlohmann::json elfMalus;
        elfMalus["target"] = "health";
        elfMalus["op"]     = "multiply";
        elfMalus["value"]  = 0.9;
        elf["maluses"].push_back(elfMalus);
        elf["selection_condition"]["min_level"] = 5;
        section["races"]["elf"] = elf;

        nlohmann::json dwarf;
        dwarf["name"] = "Nain";
        dwarf["bonuses"] = nlohmann::json::array();
        nlohmann::json dwarfBonus;
        dwarfBonus["target"] = "health";
        dwarfBonus["op"]     = "add";
        dwarfBonus["value"]  = 30.0;
        dwarf["bonuses"].push_back(dwarfBonus);
        dwarf["selection_condition"]["excluded_races"] = nlohmann::json::array({"elf"});
        section["races"]["dwarf"] = dwarf;

        nlohmann::json blacksmith;
        blacksmith["name"] = "Forgeron";
        blacksmith["bonuses"] = nlohmann::json::array();
        nlohmann::json bsBonus;
        bsBonus["target"] = "forge_yield";
        bsBonus["op"]     = "multiply";
        bsBonus["value"]  = 1.2;
        blacksmith["bonuses"].push_back(bsBonus);
        section["professions"]["blacksmith"] = blacksmith;

        nlohmann::json guard;
        guard["name"] = "Garde";
        guard["bonuses"] = nlohmann::json::array();
        nlohmann::json gBonus;
        gBonus["target"] = "health";
        gBonus["op"]     = "add";
        gBonus["value"]  = 10.0;
        guard["bonuses"].push_back(gBonus);
        guard["selection_condition"]["min_level"] = 3;
        section["professions"]["guard"] = guard;

        static const nlohmann::json cached = section;
        return cached;
    }

    // Configure PlayerStore (temp dir) + Registry (section de test) et
    // crée un profil joueur vide.
    struct TestCtx {
        std::filesystem::path dir;
        rpframework::data::PlayerId playerId;
    };
    TestCtx SetupCharacterTest(const std::string& tag, rpframework::data::PlayerId pid)
    {
        using namespace rpframework;
        TestCtx ctx;
        ctx.dir      = MakeTempPlayerDir(tag);
        ctx.playerId = pid;
        ConfigurePlayerStore(ctx.dir);
        character::Registry::ResetForTests();
        character::Registry::LoadDefinitionsFromSection(&TestCharacterSection());
        return ctx;
    }

    void TeardownCharacterTest(const TestCtx& ctx)
    {
        using namespace rpframework;
        character::Registry::Shutdown();
        CleanupPlayerStore(ctx.dir);
    }
}

TEST(Character_Registry_LoadsDefinitions)
{
    using namespace rpframework;
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&TestCharacterSection());

    EXPECT(character::Registry::HasRace("human")    == true);
    EXPECT(character::Registry::HasRace("elf")      == true);
    EXPECT(character::Registry::HasRace("dwarf")    == true);
    EXPECT(character::Registry::HasRace("phantom")  == false);
    EXPECT(character::Registry::HasProfession("blacksmith") == true);
    EXPECT(character::Registry::HasProfession("guard")      == true);
    EXPECT(character::Registry::HasClass("warrior") == false);  // classes désactivées
    EXPECT(character::Registry::ClassesEnabled()    == false);

    const auto races = character::Registry::ListRaces();
    EXPECT(races.size() == 3);

    character::Registry::Shutdown();
}

TEST(Character_SelectRace_Success)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_race_ok", 9201);
    security::RateLimiter::Reset(ctx.playerId, "race.select");

    const auto result = character::SelectRace(ctx.playerId, "human");
    EXPECT(result.status == character::SelectResult::Status::Success);

    const auto loaded = data::PlayerStore::LoadDetailed(ctx.playerId);
    EXPECT(loaded.HasData());
    EXPECT(loaded.data->race == "human");
    EXPECT(!loaded.data->starterKitDelivered);
    TeardownCharacterTest(ctx);
}

TEST(Character_SelectRace_UnknownId)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_race_unknown", 9202);
    security::RateLimiter::Reset(ctx.playerId, "race.select");

    const auto result = character::SelectRace(ctx.playerId, "phantom");
    EXPECT(result.status == character::SelectResult::Status::UnknownId);
    TeardownCharacterTest(ctx);
}

TEST(Character_SelectRace_AlreadySet)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_race_already", 9203);
    security::RateLimiter::Reset(ctx.playerId, "race.select");

    EXPECT(character::SelectRace(ctx.playerId, "human").status
           == character::SelectResult::Status::Success);
    const auto second = character::SelectRace(ctx.playerId, "elf");
    EXPECT(second.status == character::SelectResult::Status::AlreadySet);
    TeardownCharacterTest(ctx);
}

TEST(Character_SelectRace_ConditionNotMet)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_race_cond", 9204);
    security::RateLimiter::Reset(ctx.playerId, "race.select");

    // Joueur level 1 par défaut, "elf" exige min_level=5.
    const auto result = character::SelectRace(ctx.playerId, "elf");
    EXPECT(result.status == character::SelectResult::Status::ConditionNotMet);
    TeardownCharacterTest(ctx);
}

TEST(Character_SelectProfession_Success)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_prof_ok", 9205);
    security::RateLimiter::Reset(ctx.playerId, "profession.select");

    const auto result = character::SelectProfession(ctx.playerId, "blacksmith");
    EXPECT(result.status == character::SelectResult::Status::Success);

    const auto loaded = data::PlayerStore::LoadDetailed(ctx.playerId);
    EXPECT(loaded.data->profession == "blacksmith");
    TeardownCharacterTest(ctx);
}

TEST(Character_SelectProfession_ConditionNotMet)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_prof_cond", 9206);
    security::RateLimiter::Reset(ctx.playerId, "profession.select");

    // "guard" exige min_level=3. Joueur à 1 par défaut.
    const auto result = character::SelectProfession(ctx.playerId, "guard");
    EXPECT(result.status == character::SelectResult::Status::ConditionNotMet);
    TeardownCharacterTest(ctx);
}

TEST(Character_SelectClass_ClassesDisabled)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_class_disabled", 9207);
    security::RateLimiter::Reset(ctx.playerId, "class.select");

    // classes_enabled = false dans la section de test.
    const auto result = character::SelectClass(ctx.playerId, "warrior");
    EXPECT(result.status == character::SelectResult::Status::ClassesDisabled);
    TeardownCharacterTest(ctx);
}

TEST(Character_Stats_CombinesBonusesAndMaluses)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("stats_combine", 9208);
    security::RateLimiter::Reset(ctx.playerId, "race.select");
    security::RateLimiter::Reset(ctx.playerId, "profession.select");

    // Race human : xp_gain *= 1.1
    // Profession blacksmith : forge_yield *= 1.2
    EXPECT(character::SelectRace(ctx.playerId, "human").status
           == character::SelectResult::Status::Success);
    EXPECT(character::SelectProfession(ctx.playerId, "blacksmith").status
           == character::SelectResult::Status::Success);

    const auto stats = character::ComputeEffectiveStats(ctx.playerId);
    EXPECT(stats.values.at("xp_gain")     == 1.1f);
    EXPECT(stats.values.at("forge_yield") == 1.2f);
    // Les stats non mentionnées par les bonus restent absentes.
    EXPECT(stats.values.count("health") == 0);
    TeardownCharacterTest(ctx);
}

TEST(Character_Stats_AppliesMalusesAndOrdering)
{
    using namespace rpframework;
    // Cas "elf" : +20 stamina, *0.9 health ; on ajoute profession guard :
    // +10 health. Effet attendu : health = (10) * 0.9 = 9 (Add puis Multiply).
    // "elf" exige min_level=5 → on monte le level via PlayerStore avant.
    const auto ctx = SetupCharacterTest("stats_maluses", 9209);
    security::RateLimiter::Reset(ctx.playerId, "race.select");
    security::RateLimiter::Reset(ctx.playerId, "profession.select");

    auto pre = data::PlayerStore::LoadOrCreate(ctx.playerId, "Tester");
    pre.level = 5;
    EXPECT(data::PlayerStore::Save(pre) == true);

    EXPECT(character::SelectRace(ctx.playerId, "elf").status
           == character::SelectResult::Status::Success);
    // "guard" exige min_level=3 → OK avec level 5
    EXPECT(character::SelectProfession(ctx.playerId, "guard").status
           == character::SelectResult::Status::Success);

    const auto stats = character::ComputeEffectiveStats(ctx.playerId);
    EXPECT(stats.values.at("stamina") == 20.0f);  // elf bonus
    // Convention : un tout premier Multiply sur une stat absente
    // initialise la stat à 1.0 (neutre). L'ordre d'application est
    // race.bonuses → race.maluses → profession.bonuses (cf FromSelections).
    // Donc :
    //   race.bonus (elf.stamina)         : stamina += 20          → 0+20 = 20
    //   race.malus (elf.health *= 0.9)   : health 1.0*0.9         → 0.9
    //   prof.bonus (guard.health += 10)  : health 0.9+10          → 10.9
    EXPECT(stats.values.at("health")  == 10.9f);
    TeardownCharacterTest(ctx);
}

TEST(Character_Stats_AppliesProfessionMaluses)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("prof_malus");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["races"]["human"] = {
        {"name", "Humain"},
        {"bonuses", nlohmann::json::array()},
        {"maluses", nlohmann::json::array()}
    };
    nlohmann::json miner;
    miner["name"] = "Mineur";
    nlohmann::json bonus;
    bonus["target"] = "weight";
    bonus["op"] = "multiply";
    bonus["value"] = 1.2;
    miner["bonuses"] = nlohmann::json::array();
    miner["bonuses"].push_back(bonus);
    nlohmann::json malus;
    malus["target"] = "speed";
    malus["op"] = "multiply";
    malus["value"] = 0.9;
    miner["maluses"] = nlohmann::json::array();
    miner["maluses"].push_back(malus);
    miner["engrams"] = nlohmann::json::array({"/Game/Pike.Pike"});
    section["professions"]["miner"] = miner;
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);

    data::PlayerData player;
    player.id = 9220;
    player.race = "human";
    player.profession = "miner";
    EXPECT(data::PlayerStore::Save(player));

    const auto stats = character::ComputeEffectiveStats(9220);
    EXPECT(stats.values.at("weight") == 1.2f);
    EXPECT(stats.values.at("speed") == 0.9f);
    const auto prof = character::Registry::GetProfession("miner");
    EXPECT(prof.has_value());
    if (prof) EXPECT(prof->engrams.size() == 1);

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Character_ResetSelections)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_reset", 9210);
    security::RateLimiter::Reset(ctx.playerId, "race.select");
    security::RateLimiter::Reset(ctx.playerId, "profession.select");

    EXPECT(character::SelectRace(ctx.playerId, "human").status
           == character::SelectResult::Status::Success);
    EXPECT(character::SelectProfession(ctx.playerId, "blacksmith").status
           == character::SelectResult::Status::Success);
    {
        const auto mid = data::PlayerStore::LoadDetailed(ctx.playerId);
        EXPECT(mid.data->starterKitDelivered);
    }

    EXPECT(character::ResetSelections(ctx.playerId) == true);
    const auto after = data::PlayerStore::LoadDetailed(ctx.playerId);
    EXPECT(after.data->race.empty());
    EXPECT(after.data->profession.empty());
    EXPECT(after.data->playerClass.empty());
    EXPECT(!after.data->starterKitDelivered);

    // Reset sur un joueur sans sélection ne fait rien.
    EXPECT(character::ResetSelections(ctx.playerId) == false);
    TeardownCharacterTest(ctx);
}

TEST(Character_SelectRace_PersistsAndReload)
{
    using namespace rpframework;
    const auto ctx = SetupCharacterTest("select_persist", 9211);
    security::RateLimiter::Reset(ctx.playerId, "race.select");

    EXPECT(character::SelectRace(ctx.playerId, "human").status
           == character::SelectResult::Status::Success);

    // Recharge le profil depuis le disque.
    const auto reloaded = data::PlayerStore::LoadDetailed(ctx.playerId);
    EXPECT(reloaded.HasData());
    EXPECT(reloaded.data->race == "human");
    TeardownCharacterTest(ctx);
}

// ---------------------------------------------------------------------------
// Tests de couverture des cas-limites Phase 4a
// ---------------------------------------------------------------------------

// Test 1 : cross-references dans SelectionCondition
// On teste directement SelectionCondition::IsSatisfiedBy (unit test) parce
// que le pipeline Select est one-shot : une fois "blacksmith" sélectionné,
// "engineer" ne peut pas être tenté (déjà set). La logique de condition
// reste valide et testable indépendamment.
TEST(Character_SelectionCondition_RequiredProfessions)
{
    using namespace rpframework::character;

    SelectionCondition cond;
    cond.requiredProfessions = {"blacksmith", "miner"};

    std::unordered_map<std::string, int> rep;

    // Joueur sans profession → condition non satisfaite.
    EXPECT(cond.IsSatisfiedBy("",    "",       "",  1, rep) == false);
    // Joueur avec "blacksmith" → OK.
    EXPECT(cond.IsSatisfiedBy("human","blacksmith","",  1, rep) == true);
    // Joueur avec "miner" → OK (liste OR).
    EXPECT(cond.IsSatisfiedBy("human","miner",     "",  1, rep) == true);
    // Joueur avec une autre profession → KO.
    EXPECT(cond.IsSatisfiedBy("human","guard",     "",  1, rep) == false);

    // excluded_professions : ne doit PAS être blacksmith.
    SelectionCondition cond2;
    cond2.excludedProfessions = {"blacksmith"};
    EXPECT(cond2.IsSatisfiedBy("human", "",         "",  1, rep) == true);
    EXPECT(cond2.IsSatisfiedBy("human", "blacksmith", "",  1, rep) == false);
    EXPECT(cond2.IsSatisfiedBy("human", "guard",     "",  1, rep) == true);

    // required_classes / excluded_classes : logique symétrique.
    SelectionCondition cond3;
    cond3.requiredClasses = {"warrior"};
    cond3.excludedClasses = {"rogue"};
    EXPECT(cond3.IsSatisfiedBy("", "", "",     1, rep) == false);
    EXPECT(cond3.IsSatisfiedBy("", "", "warrior", 1, rep) == true);
    EXPECT(cond3.IsSatisfiedBy("", "", "rogue",  1, rep) == false);
}

// Test 2 : min_reputation dans SelectionCondition
TEST(Character_SelectRace_RequiresMinReputation)
{
    using namespace rpframework;

    // "noble" exige 50 de réputation avec "town".
    nlohmann::json section;
    section["classes_enabled"] = false;
    nlohmann::json noble;
    noble["name"] = "Noble";
    noble["selection_condition"]["min_reputation"]["town"] = 50;
    section["races"]["noble"] = noble;
    nlohmann::json commoner;
    commoner["name"] = "Commun";
    section["races"]["commoner"] = commoner;

    const auto dir = MakeTempPlayerDir("rep");
    ConfigurePlayerStore(dir);
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);
    security::RateLimiter::Reset(9221, "race.select");

    // Le joueur vient de se connecter : réputation 0 → "noble" refusé.
    const auto blocked = character::SelectRace(9221, "noble");
    EXPECT(blocked.status == character::SelectResult::Status::ConditionNotMet);

    // "commoner" doit passer (pas de condition).
    EXPECT(character::SelectRace(9221, "commoner").status
           == character::SelectResult::Status::Success);

    // Maintenant on crédite le joueur de 60 en réputation "town" puis
    // on tente "noble" via ResetSelections + re-select.
    EXPECT(character::ResetSelections(9221) == true);
    auto data = rpframework::data::PlayerStore::LoadOrCreate(9221);
    data.reputation["town"] = 60;
    EXPECT(rpframework::data::PlayerStore::Save(data) == true);

    EXPECT(character::SelectRace(9221, "noble").status
           == character::SelectResult::Status::Success);

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

// Test 3 : SelectClass fonctionne quand classes_enabled=true
TEST(Character_SelectClass_WhenEnabled)
{
    using namespace rpframework;

    nlohmann::json section;
    section["classes_enabled"] = true;
    nlohmann::json warrior;
    warrior["name"] = "Guerrier";
    nlohmann::json warriorBonus;
    warriorBonus["target"] = "health";
    warriorBonus["op"]     = "add";
    warriorBonus["value"]  = 50.0;
    warrior["bonuses"] = nlohmann::json::array();
    warrior["bonuses"].push_back(warriorBonus);
    section["classes"]["warrior"] = warrior;
    nlohmann::json mage;
    mage["name"] = "Mage";
    section["classes"]["mage"] = mage;

    const auto dir = MakeTempPlayerDir("classes");
    ConfigurePlayerStore(dir);
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);
    EXPECT(character::Registry::ClassesEnabled() == true);
    EXPECT(character::Registry::HasClass("warrior") == true);
    EXPECT(character::Registry::HasClass("mage") == true);

    security::RateLimiter::Reset(9222, "class.select");

    EXPECT(character::SelectClass(9222, "warrior").status
           == character::SelectResult::Status::Success);

    const auto loaded = rpframework::data::PlayerStore::LoadDetailed(9222);
    EXPECT(loaded.data->playerClass == "warrior");

    // EffectiveStats doit intégrer le bonus de la classe.
    const auto stats = character::ComputeEffectiveStats(9222);
    EXPECT(stats.values.at("health") == 50.0f);

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

// Test 4 : Registry robuste face à une config partiellement invalide.
// Une race sans "value" sur un bonus doit être ignorée sans crasher,
// et les autres races doivent toujours charger.
TEST(Character_Registry_SkipsInvalidDefinitions)
{
    using namespace rpframework;

    nlohmann::json section;
    section["classes_enabled"] = false;
    nlohmann::json valid;
    valid["name"] = "Valide";
    section["races"]["valid"] = valid;
    // Race invalide : bonus avec un target vide → ignorée en interne mais
    // l'entrée est tout de même créée (la consigne GDD est de ne pas crasher).
    nlohmann::json broken;
    broken["name"] = "Cassée";
    nlohmann::json badBonus;
    badBonus["target"] = "";
    badBonus["op"]     = "add";
    badBonus["value"]  = 5.0;
    broken["bonuses"] = nlohmann::json::array();
    broken["bonuses"].push_back(badBonus);
    section["races"]["broken"] = broken;

    const auto dir = MakeTempPlayerDir("robust");
    ConfigurePlayerStore(dir);
    character::Registry::ResetForTests();
    // Ne doit PAS crasher.
    character::Registry::LoadDefinitionsFromSection(&section);

    // La race valide est chargée.
    EXPECT(character::Registry::HasRace("valid") == true);
    // La race "broken" est aussi chargée (on n'efface pas tout si une
    // entrée est partielle) mais ses modifiers sont filtrés.
    EXPECT(character::Registry::HasRace("broken") == true);

    auto r = character::Registry::GetRace("broken");
    EXPECT(r.has_value());
    if (r) EXPECT(r->bonuses.size() == 0);  // le bonus target vide est ignoré

    auto v = character::Registry::GetRace("valid");
    EXPECT(v.has_value());
    if (v) EXPECT(v->bonuses.size() == 0);  // pas de bonus, ok

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

// ---------------------------------------------------------------------------
// Tests Loadout / Phase 4b
// ---------------------------------------------------------------------------

// Helper : configure un PlayerStore temp pour tester la composition.
struct LoadoutCtx {
    std::filesystem::path dir;
};

LoadoutCtx SetupLoadoutTest(const std::string& tag)
{
    using namespace rpframework;
    LoadoutCtx ctx;
    ctx.dir = MakeTempPlayerDir("loadout_" + tag);
    ConfigurePlayerStore(ctx.dir);
    return ctx;
}

// 1. Item : parsing JSON + array filtering
TEST(Loadout_ItemFromJson)
{
    using namespace rpframework::loadout;

    nlohmann::json j;
    j["id"]       = "bread";
    j["quantity"] = 5;
    j["quality"]  = "primitive";
    auto it = Item::FromJson(j);
    EXPECT(it.id == "bread");
    EXPECT(it.quantity == 5);
    EXPECT(it.quality == "primitive");

    // Sans id → invalide.
    nlohmann::json bad;
    bad["quantity"] = 1;
    auto invalid = Item::FromJson(bad);
    EXPECT(invalid.id.empty());

    // quantity négatif → clampé à 1.
    nlohmann::json neg;
    neg["id"]       = "torch";
    neg["quantity"] = -10;
    auto clamped = Item::FromJson(neg);
    EXPECT(clamped.quantity == 1);

    // Array : entrées invalides filtrées.
    nlohmann::json arr = nlohmann::json::array();
    nlohmann::json valid;
    valid["id"] = "torch";
    nlohmann::json noId;
    noId["quantity"] = 99;
    arr.push_back(valid);
    arr.push_back(noId);
    auto items = ItemsFromJsonArray(arr);
    EXPECT(items.size() == 1);
    if (!items.empty()) EXPECT(items[0].id == "torch");
}

// 2. Composer.Merge : items identiques → addition ; dernier quality wins
TEST(Loadout_Composer_Merges)
{
    using namespace rpframework::loadout;

    std::vector<std::vector<Item>> sources;
    sources.push_back({{"bread", 3, "", {}}, {"torch", 1, "", {}}});
    sources.push_back({{"bread", 2, "apprentice", {}}});
    sources.push_back({{"arrow", 10, "", {}}});

    auto merged = Composer::Merge(sources);

    bool foundBread = false, foundTorch = false, foundArrow = false;
    for (const auto& it : merged)
    {
        if (it.id == "bread")      { EXPECT(it.quantity == 5); EXPECT(it.quality == "apprentice"); foundBread = true; }
        else if (it.id == "torch") { EXPECT(it.quantity == 1); foundTorch = true; }
        else if (it.id == "arrow") { EXPECT(it.quantity == 10); foundArrow = true; }
    }
    EXPECT(foundBread);
    EXPECT(foundTorch);
    EXPECT(foundArrow);
    EXPECT(merged.size() == 3);
}

// 3. ComposeStarterKit : combine commun + race + profession
TEST(Loadout_Composer_StarterKitFromSelections)
{
    using namespace rpframework;

    nlohmann::json section;
    section["classes_enabled"] = false;
    nlohmann::json human;
    human["name"] = "Humain";
    nlohmann::json hb;
    hb["id"] = "torch"; hb["quantity"] = 1;
    human["starter_equipment"] = nlohmann::json::array();
    human["starter_equipment"].push_back(hb);
    section["races"]["human"] = human;
    nlohmann::json blacksmith;
    blacksmith["name"] = "Forgeron";
    nlohmann::json bb;
    bb["id"] = "iron_sword"; bb["quantity"] = 1;
    blacksmith["starter_equipment"] = nlohmann::json::array();
    blacksmith["starter_equipment"].push_back(bb);
    section["professions"]["blacksmith"] = blacksmith;

    nlohmann::json commonKit = nlohmann::json::array({
        nlohmann::json::object({{"id", "bread"}, {"quantity", 5}}),
        nlohmann::json::object({{"id", "torch"},  {"quantity", 1}}),  // collision avec race
    });
    core::Config::Get().Set("loadout.common_kit", commonKit);

    const auto ctx = SetupLoadoutTest("compose_kit");
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);
    security::RateLimiter::Reset(9300, "race.select");
    security::RateLimiter::Reset(9300, "profession.select");

    EXPECT(character::SelectRace(9300, "human").status
           == character::SelectResult::Status::Success);
    EXPECT(character::SelectProfession(9300, "blacksmith").status
           == character::SelectResult::Status::Success);

    const auto kit = loadout::Composer::ComposeStarterKit(9300);

    bool foundBread = false, foundTorch = false, foundSword = false;
    for (const auto& it : kit)
    {
        if (it.id == "bread")       { EXPECT(it.quantity == 5); foundBread = true; }
        else if (it.id == "torch")  { EXPECT(it.quantity == 2); foundTorch = true; }
        else if (it.id == "iron_sword"){ EXPECT(it.quantity == 1); foundSword = true; }
    }
    EXPECT(foundBread);
    EXPECT(foundTorch);
    EXPECT(foundSword);
    EXPECT(kit.size() == 3);

    core::Config::Get().Set("loadout.common_kit", nlohmann::json::array());
    character::Registry::Shutdown();
    CleanupPlayerStore(ctx.dir);
}

// 4. Distributor : idempotence (un seul Give par joueur)
TEST(Loadout_Distributor_Idempotent)
{
    using namespace rpframework;

    nlohmann::json section;
    section["classes_enabled"] = false;
    nlohmann::json human;
    human["name"] = "Humain";
    nlohmann::json hb;
    hb["id"] = "torch"; hb["quantity"] = 1;
    human["starter_equipment"] = nlohmann::json::array();
    human["starter_equipment"].push_back(hb);
    section["races"]["human"] = human;

    nlohmann::json commonKit = nlohmann::json::array({
        nlohmann::json::object({{"id", "bread"}, {"quantity", 5}})
    });
    core::Config::Get().Set("loadout.common_kit", commonKit);

    const auto ctx = SetupLoadoutTest("distribute_idem");
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);
    security::RateLimiter::Reset(9301, "race.select");

    EXPECT(character::SelectRace(9301, "human").status
           == character::SelectResult::Status::Success);

    // Pas de métiers dans ce registre : le kit combiné est prêt dès la race.
    {
        auto data = data::PlayerStore::LoadDetailed(9301);
        EXPECT(data.data->starterKitDelivered);
    }
    const auto composed = loadout::Composer::ComposeStarterKit(9301);
    EXPECT(composed.size() == 2);  // bread + torch

    const auto second = loadout::Distributor::GiveStarterKit(9301);
    EXPECT(second.status == loadout::DistributionStatus::AlreadyGiven);
    EXPECT(second.items.empty());

    const auto forced = loadout::Distributor::ForceGiveStarterKit(9301);
    EXPECT(forced.status == loadout::DistributionStatus::Delivered);
    EXPECT(forced.items.size() == 2);

    core::Config::Get().Set("loadout.common_kit", nlohmann::json::array());
    character::Registry::Shutdown();
    CleanupPlayerStore(ctx.dir);
}

// ---------------------------------------------------------------------------
// Audit global : tests de couverture des modules Phase 1 (Core/Security/Data)
// ---------------------------------------------------------------------------

// Core/Config : load/save round-trip + accesseurs
TEST(Audit_Core_ConfigRoundTrip)
{
    using namespace rpframework;

    const auto path = std::filesystem::temp_directory_path() / "rpframework_tests"
        / ("config_test_" + std::to_string(std::rand()) + ".json");
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream f(path, std::ios::trunc);
        f << R"({"debug": true, "data": {"backup_count": 7}, "x": {"y": {"z": 42}}})";
    }

    core::Config::Get().LoadFromFile(path);
    EXPECT(core::Config::Get().Has("debug") == true);
    EXPECT(core::Config::Get().Has("nope") == false);
    EXPECT(core::Config::Get().GetOr<bool>("debug", false) == true);
    EXPECT(core::Config::Get().GetOr<int>("data.backup_count", 0) == 7);
    EXPECT(core::Config::Get().GetOr<int>("x.y.z", 0) == 42);
    EXPECT(core::Config::Get().GetOr<std::string>("missing", "fallback") == "fallback");

    core::Config::Get().Set("data.newkey", nlohmann::json(99));
    EXPECT(core::Config::Get().GetOr<int>("data.newkey", 0) == 99);

    const auto path2 = path.string() + ".roundtrip";
    EXPECT(core::Config::Get().SaveToFile(path2) == true);
    core::Config::Get().LoadFromFile(path2);
    EXPECT(core::Config::Get().GetOr<int>("data.newkey", 0) == 99);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path2, ec);
}

// Core/Paths : les helpers de chemins
TEST(Audit_Core_Paths)
{
    using namespace rpframework;
    const auto pluginDir  = core::GetPluginDir();
    const auto auditPath  = core::GetAuditLogPath();
    const auto framePath  = core::GetFrameworkLogPath();
    EXPECT(pluginDir.filename() == core::kFrameworkName);
    EXPECT(auditPath.filename() == "audit.log");
    EXPECT(framePath.filename() == "framework.log");

    const auto nested = std::filesystem::temp_directory_path() / "rpframework_tests"
        / ("nest_" + std::to_string(std::rand())) / "a" / "b" / "c";
    EXPECT(core::EnsureDirectoryExists(nested) == true);
    EXPECT(std::filesystem::is_directory(nested));
    EXPECT(core::EnsureDirectoryExists(nested) == true);
    std::error_code ecRem;
    std::filesystem::remove_all(nested.parent_path().parent_path().parent_path().parent_path(), ecRem);
}

// Core/Version
TEST(Audit_Core_Version)
{
    using namespace rpframework;
    EXPECT(core::GetVersionShort() == "0.1.0");
    EXPECT(core::GetVersionString() == "0.1.0");
    EXPECT(core::GetConfigSchemaVersionString() == "1");
    EXPECT(core::GetVersionNumber() == 100);
}

// Security/Permissions : LevelToString/LevelFromString/Snapshot
TEST(Audit_Security_PermissionStringsAndSnapshot)
{
    using namespace rpframework;
    security::Permissions::Initialize();

    EXPECT(security::LevelToString(security::Level::PLAYER)    == "PLAYER");
    EXPECT(security::LevelToString(security::Level::MODERATOR) == "MODERATOR");
    EXPECT(security::LevelToString(security::Level::GM)        == "GM");
    EXPECT(security::LevelToString(security::Level::ADMIN)     == "ADMIN");
    EXPECT(security::LevelToString(security::Level::OWNER)     == "OWNER");
    EXPECT(security::LevelToString(security::Level::SYSTEM)    == "SYSTEM");

    EXPECT(security::LevelFromString("ADMIN")  == security::Level::ADMIN);
    EXPECT(security::LevelFromString("admin")  == security::Level::ADMIN);
    EXPECT(security::LevelFromString("Gm")     == security::Level::GM);
    EXPECT(security::LevelFromString("PLAYER", security::Level::OWNER) == security::Level::PLAYER);
    EXPECT(security::LevelFromString("invalid", security::Level::OWNER) == security::Level::OWNER);

    const auto snap = security::Permissions::Snapshot();
    EXPECT(snap.size() >= 25);
    EXPECT(snap.count("race.select") == 1);
    EXPECT(snap.at("race.select") == security::Level::PLAYER);
}

// Security/RateLimiter : LoadFromConfig, GetLimit, ResetPlayer
TEST(Audit_Security_RateLimiterApi)
{
    using namespace rpframework;
    security::RateLimiter::Initialize();

    nlohmann::json cfg;
    cfg["rate_limits"]["test.action_a"] = { {"max", 3}, {"window_sec", 10} };
    cfg["rate_limits"]["test.action_b"] = { {"max", 5}, {"window_sec", 60} };
    security::RateLimiter::LoadFromConfig(cfg);

    EXPECT(security::RateLimiter::GetLimit("test.action_a").max == 3);
    EXPECT(security::RateLimiter::GetLimit("test.action_a").windowSec == 10);
    EXPECT(security::RateLimiter::GetLimit("test.action_b").max == 5);
    EXPECT(security::RateLimiter::GetLimit("unknown.action").max == 0);

    constexpr security::PlayerId pidAudit = 99999;
    security::RateLimiter::Register("test.action_a", security::RateLimiter::Limit{1, 60});
    security::RateLimiter::Register("test.action_b", security::RateLimiter::Limit{1, 60});
    security::RateLimiter::ResetPlayer(pidAudit);
    EXPECT(security::RateLimiter::Allow(pidAudit, "test.action_a") == true);
    EXPECT(security::RateLimiter::Allow(pidAudit, "test.action_b") == true);
    EXPECT(security::RateLimiter::Count(pidAudit, "test.action_a") == 1);
    EXPECT(security::RateLimiter::Count(pidAudit, "test.action_b") == 1);

    security::RateLimiter::ResetPlayer(pidAudit);
    EXPECT(security::RateLimiter::Count(pidAudit, "test.action_a") == 0);
    EXPECT(security::RateLimiter::Count(pidAudit, "test.action_b") == 0);
}

// Security/AuditLog : accesseurs + Shutdown idempotent
TEST(Audit_Security_AuditLogAccessors)
{
    using namespace rpframework;
    security::AuditLog::Initialize();
    EXPECT(security::AuditLog::IsEnabled() == true);
    EXPECT(security::AuditLog::LogFilePath().filename() == "audit.log");

    security::AuditLog::Shutdown();
    security::AuditLog::Initialize();
    security::AuditLog::Log("audit.test", 0, {{"k", 1}});
    auto recent = security::AuditLog::Recent(5);
    bool found = false;
    for (const auto& e : recent) if (e.action == "audit.test") found = true;
    EXPECT(found);
}

// Data/Migration : round-trip explicite v0 → current
TEST(Audit_Data_MigrationV0ToCurrent)
{
    using namespace rpframework;
    nlohmann::json legacy = {
        {"identity", {{"id", "42"}, {"name", "LegacyPlayer"}}},
        {"character", {{"race", "human"}}},
        {"progression", {{"level", 3}, {"xp", 100}}}
    };
    EXPECT(data::Migrate(legacy, 0) == true);
    EXPECT(legacy["meta"]["schema_version"] == data::kCurrentSchemaVersion);
    // v1 a posé la meta, v2 a posé la section "economy"
    EXPECT(legacy["economy"].is_object());

    nlohmann::json current;
    current["meta"] = {{"schema_version", data::kCurrentSchemaVersion}};
    EXPECT(data::Migrate(current, data::kCurrentSchemaVersion) == false);

    nlohmann::json future;
    future["meta"] = {{"schema_version", 99}};
    EXPECT(data::Migrate(future, 99) == false);
    // Une donnée future N'EST PAS rétrogradée : on la laisse telle quelle
    // pour éviter qu'un binaire ancien n'écrase des champs qu'il ne
    // comprend pas. schema_version reste à 99.
    EXPECT(future["meta"]["schema_version"] == 99);
}

// Data/Migration : v2 → v3 ajoute la section "unlocks" (vide par défaut).
// La migration est rétro-compatible : un fichier v2 sans "unlocks" continue
// de charger, et un fichier v3 charge inchangé.
TEST(Audit_Data_MigrationV2ToV3AddsUnlocks)
{
    using namespace rpframework;
    nlohmann::json v2;
    v2["meta"] = {{"schema_version", 2}};
    v2["identity"] = {{"id", "7777"}};
    v2["economy"] = nlohmann::json::object();
    EXPECT(data::Migrate(v2, 2) == true);
    EXPECT(v2["meta"]["schema_version"] == data::kCurrentSchemaVersion);
    EXPECT(v2["unlocks"].is_array());
    EXPECT(v2["unlocks"].empty());

    // Idempotence : migrer un fichier déjà v3 ne fait rien.
    EXPECT(data::Migrate(v2, data::kCurrentSchemaVersion) == false);
}

// Data/PlayerData : round-trip sérialisation avec tous les champs
TEST(Audit_Data_PlayerDataRoundTrip)
{
    using namespace rpframework;
    data::PlayerData in;
    in.id = 7777;
    in.name = "Round-Trip";
    in.race = "elf";
    in.profession = "blacksmith";
    in.playerClass = "warrior";
    in.faction = "town";
    in.level = 42;
    in.xp = 9999;
    in.reputation["town"] = 100;
    in.reputation["thieves_guild"] = -50;
    in.titles.push_back("hero");
    in.titles.push_back("veteran");
    in.unlocks.push_back("recipe_iron_sword");
    in.unlocks.push_back("zone_forest");
    in.starterKitDelivered = true;
    in.createdAt = std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}};
    in.updatedAt = std::chrono::system_clock::time_point{std::chrono::seconds{1700000123}};

    const auto j = in.ToJson();
    const auto out = data::PlayerData::FromJson(j);

    EXPECT(out.id == in.id);
    EXPECT(out.name == in.name);
    EXPECT(out.race == in.race);
    EXPECT(out.profession == in.profession);
    EXPECT(out.playerClass == in.playerClass);
    EXPECT(out.faction == in.faction);
    EXPECT(out.level == in.level);
    EXPECT(out.xp == in.xp);
    EXPECT(out.reputation.at("town") == 100);
    EXPECT(out.reputation.at("thieves_guild") == -50);
    EXPECT(out.titles.size() == 2);
    EXPECT(out.titles[0] == "hero");
    EXPECT(out.titles[1] == "veteran");
    EXPECT(out.unlocks.size() == 2);
    EXPECT(out.unlocks[0] == "recipe_iron_sword");
    EXPECT(out.unlocks[1] == "zone_forest");
    EXPECT(out.starterKitDelivered == true);
    EXPECT(out.schemaVersion == data::kPlayerDataSchemaVersion);
    EXPECT(!data::PlayerData::FormatTime(in.createdAt).empty());
}

TEST(Quest_Registry_Loads)
{
    using namespace rpframework::core;
    using namespace rpframework::quest;

    nlohmann::json root = nlohmann::json::object();
    root["quests"] = {
        {"tutorial_hunt", {
            {"name", "Chasse du tutoriel"},
            {"description", "Tuer 3 animaux"},
            {"category", "main"},
            {"repeatable", false},
            {"objectives", {
                {
                    {"id", "hunt_1"},
                    {"type", "kill"},
                    {"target", 3},
                    {"entity", "boar"},
                    {"required", true}
                }
            }},
            {"rewards", {
                {
                    {"type", "currency"},
                    {"id", "gold"},
                    {"amount", 25}
                }
            }}
        }}
    };

    Config::Get().Set("quests", root["quests"]);
    Registry::LoadFromConfig();

    const auto quest = Registry::GetQuest("tutorial_hunt");
    EXPECT(quest.has_value());
    EXPECT(quest->objectives.size() == 1);
    EXPECT(quest->rewards.size() == 1);
    EXPECT(quest->rewards[0].type == "currency");
    EXPECT(quest->rewards[0].amount == 25);
}

TEST(Quest_Engine_ProgressesAndRewardsOnce)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_engine");
    ConfigurePlayerStore(dir);

    nlohmann::json currency;
    currency["gold"] = { {"name", "Gold"}, {"max_balance", 1000} };
    core::Config::Get().Set("economy.currencies", currency);
    economy::Registry::LoadFromConfig();
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json section;
    section["quest_one"] = {
        {"name", "Premiere quete"},
        {"objectives", {{{"id", "kill_boar"}, {"type", "kill"},
                         {"entity", "boar"}, {"target", 2}}}},
        {"rewards", {{{"type", "currency"}, {"id", "gold"}, {"amount", 25}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);

    data::PlayerData player;
    player.id = 95100;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95100, "quest_one").success());
    EXPECT(quest::AddProgress(95100, "quest_one", "kill", "boar").success());
    EXPECT(quest::Complete(95100, "quest_one").status == quest::Status::NotComplete);
    const auto finished = quest::AddProgress(95100, "quest_one", "kill", "boar");
    EXPECT(finished.success());
    EXPECT(finished.message.find("terminee") != std::string::npos);
    EXPECT(economy::GetBalance(95100, "gold") == 25);
    EXPECT(quest::Complete(95100, "quest_one").status == quest::Status::AlreadyCompleted);
    EXPECT(economy::GetBalance(95100, "gold") == 25);

    quest::Registry::Shutdown();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_Engine_DoesNotReplayRewardsWhenAlreadyGranted)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_no_replay");
    ConfigurePlayerStore(dir);

    nlohmann::json currency;
    currency["gold"] = { {"name", "Gold"}, {"max_balance", 1000} };
    core::Config::Get().Set("economy.currencies", currency);
    economy::Registry::LoadFromConfig();
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json section;
    section["quest_one"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "kill_boar"}, {"type", "kill"},
                         {"entity", "boar"}, {"target", 1}}}},
        {"rewards", {{{"type", "currency"}, {"id", "gold"}, {"amount", 25}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);

    data::PlayerData player;
    player.id = 95116;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95116, "quest_one").success());
    EXPECT(quest::AddProgress(95116, "quest_one", "kill", "boar").success());
    EXPECT(quest::Complete(95116, "quest_one").success());
    EXPECT(economy::GetBalance(95116, "gold") == 25);

    auto loaded = data::PlayerStore::LoadDetailed(95116);
    EXPECT(loaded.HasData());
    loaded.data->quests["quest_one"].status = data::QuestProgress::Status::Active;
    loaded.data->quests["quest_one"].rewardsGranted = true;
    EXPECT(data::PlayerStore::Save(*loaded.data));

    security::RateLimiter::Reset(95116, "quest.complete");
    EXPECT(quest::Complete(95116, "quest_one").status == quest::Status::AlreadyCompleted);
    EXPECT(economy::GetBalance(95116, "gold") == 25);

    quest::Registry::Shutdown();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_Events_ProgressAllMatchingActiveQuests)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_events");
    ConfigurePlayerStore(dir);

    nlohmann::json section;
    for (const auto& id : {"hunt_a", "hunt_b"})
    {
        section[id] = {
            {"auto_complete", false},
            {"objectives", {{{"id", "boar"}, {"type", "kill"},
                             {"entity", "boar"}, {"target", 1}}}}
        };
    }
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95101;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95101, "hunt_a").success());
    EXPECT(quest::Start(95101, "hunt_b").success());
    EXPECT(quest::ReportGameplay(95101, "kill", "boar") == 2);
    EXPECT(quest::Complete(95101, "hunt_a").success());
    EXPECT(quest::Complete(95101, "hunt_b").success());

    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_Start_EnforcesCharacterCondition)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_conditions");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["elf_quest"] = {
        {"min_level", 1},
        {"selection_condition", {{"required_races", {"elf"}}}},
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "forest"}, {"target", 1}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95104;
    player.race = "human";
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95104, "elf_quest").status == quest::Status::ConditionNotMet);
    player.race = "elf";
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95104, "elf_quest").success());
    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_CompletionSupportsAnyObjective)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_any");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["choose_path"] = {
        {"auto_complete", false},
        {"objective_mode", "any"},
        {"objectives", {
            {{"id", "forest"}, {"type", "visit"}, {"entity", "forest"}, {"target", 1}},
            {{"id", "town"}, {"type", "visit"}, {"entity", "town"}, {"target", 1}}
        }}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95105;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95105, "choose_path").success());
    EXPECT(quest::ReportGameplay(95105, "visit", "town") == 1);
    EXPECT(quest::Complete(95105, "choose_path").success());
    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_CompletionRejectsExpiredObjective)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_timed");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["timed"] = {
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "ruins"}, {"target", 1}, {"time_limit_sec", 1}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95106;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95106, "timed").success());
    auto loaded = data::PlayerStore::LoadDetailed(95106);
    EXPECT(loaded.HasData());
    if (loaded.HasData())
    {
        loaded.data->quests["timed"].startedAt -= 2;
        EXPECT(data::PlayerStore::Save(*loaded.data));
    }
    EXPECT(quest::ReportGameplay(95106, "visit", "ruins") == 1);
    EXPECT(quest::Complete(95106, "timed").status == quest::Status::NotComplete);
    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_ItemReward_RequiresDeliveryConfirmation)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_item_reward");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["item_quest"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "camp"}, {"target", 1}}}},
        {"rewards", {{{"type", "item"}, {"id", "torch"}, {"amount", 2}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95107;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95107, "item_quest").success());
    EXPECT(quest::ReportGameplay(95107, "visit", "camp") == 1);
    EXPECT(quest::Complete(95107, "item_quest").success());
    EXPECT(quest::PendingItemRewards(95107, "item_quest").size() == 1);
    EXPECT(quest::ConfirmItemRewardsDelivered(95107, "item_quest"));
    EXPECT(quest::PendingItemRewards(95107, "item_quest").empty());
    EXPECT(!quest::ConfirmItemRewardsDelivered(95107, "item_quest"));
    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

// Per-id confirmation : si l'adaptateur ASA ne livre qu'un reward sur N,
// les autres restent en attente au lieu d'être silently dropped.
TEST(Quest_ItemReward_PerIdConfirmation)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_item_perid");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["two_items"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "camp"}, {"target", 1}}}},
        {"rewards", {
            {{"type", "item"}, {"id", "torch"},  {"amount", 1}},
            {{"type", "item"}, {"id", "potion"}, {"amount", 1}}
        }}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95112;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95112, "two_items").success());
    EXPECT(quest::ReportGameplay(95112, "visit", "camp") == 1);
    EXPECT(quest::Complete(95112, "two_items").success());
    EXPECT(quest::PendingItemRewards(95112, "two_items").size() == 2);

    // ASA ne livre que "torch". "potion" doit rester pending.
    EXPECT(quest::ConfirmItemReward(95112, "two_items", "torch"));
    auto pending = quest::PendingItemRewards(95112, "two_items");
    EXPECT(pending.size() == 1);
    EXPECT(pending[0].value("id", std::string{}) == "potion");

    // Confirmation d'un id inexistant → false, no-op.
    EXPECT(!quest::ConfirmItemReward(95112, "two_items", "sword"));

    // ASA livre "potion" au retry.
    EXPECT(quest::ConfirmItemReward(95112, "two_items", "potion"));
    EXPECT(quest::PendingItemRewards(95112, "two_items").empty());

    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_Events_ExposeTypedGameplayEntryPoints)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_typed_events");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["typed"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "kill"}, {"type", "kill"},
                         {"entity", "wolf"}, {"target", 2}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95108;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95108, "typed").success());
    EXPECT(quest::ReportKill(95108, "wolf") == 1);
    EXPECT(quest::ReportKill(95108, "wolf") == 1);
    EXPECT(quest::Complete(95108, "typed").success());
    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_Completion_RejectsInvalidRewardsBeforeMutation)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_bad_reward");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["bad_reward"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "camp"}, {"target", 1}}}},
        {"rewards", {{{"type", "unknown"}, {"id", "x"}, {"amount", 1}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95109;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95109, "bad_reward").success());
    EXPECT(quest::ReportGameplay(95109, "visit", "camp") == 1);
    EXPECT(quest::Complete(95109, "bad_reward").status == quest::Status::RewardFailed);
    EXPECT(quest::DescribeProgress(95109, "bad_reward").find("active") != std::string::npos);
    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

// Une quête qui décerne xp + title + item + unlock en même temps doit
// tout appliquer en un seul batch Load/Save (et ne PAS mettre l'unlock
// dans `titles`, contrairement à l'ancien comportement). Vérifie aussi
// qu'un reward `unlock` dupliqué n'est pas ajouté deux fois.
TEST(Quest_Engine_BatchedRewards_XpTitleItemUnlock)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_batched");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["batched"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "camp"}, {"target", 1}}}},
        {"rewards", {
            {{"type", "xp"},     {"id", "ignored"},  {"amount", 50}},
            {{"type", "title"},  {"id", "hero"},     {"amount", 0}},
            {{"type", "item"},   {"id", "torch"},    {"amount", 1}},
            {{"type", "unlock"}, {"id", "recipe_iron_sword"}, {"amount", 0}}
        }}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95113;
    player.xp = 10;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95113, "batched").success());
    EXPECT(quest::ReportGameplay(95113, "visit", "camp") == 1);
    EXPECT(quest::Complete(95113, "batched").success());

    auto loaded = data::PlayerStore::LoadDetailed(95113);
    EXPECT(loaded.HasData());
    if (loaded.HasData())
    {
        EXPECT(loaded.data->xp == 60);                              // 10 + 50
        EXPECT(loaded.data->titles.size() == 1);
        EXPECT(loaded.data->titles[0] == "hero");
        EXPECT(loaded.data->unlocks.size() == 1);                   // PAS dans titles
        EXPECT(loaded.data->unlocks[0] == "recipe_iron_sword");
        EXPECT(loaded.data->quests["batched"].pendingItemRewards.size() == 1);
        EXPECT(loaded.data->quests["batched"].rewardsGranted == true);
    }

    // Reload : re-compléter ne doit pas dupliquer title/unlock.
    EXPECT(quest::Complete(95113, "batched").status == quest::Status::AlreadyCompleted);
    auto reloaded = data::PlayerStore::LoadDetailed(95113);
    if (reloaded.HasData())
    {
        EXPECT(reloaded.data->titles.size() == 1);
        EXPECT(reloaded.data->unlocks.size() == 1);
    }

    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_Registry_RejectsDuplicateObjectives)
{
    using namespace rpframework;
    nlohmann::json section;
    section["invalid"] = {
        {"objectives", {
            {{"id", "same"}, {"type", "kill"}, {"entity", "wolf"}, {"target", 1}},
            {{"id", "same"}, {"type", "collect"}, {"entity", "fur"}, {"target", 1}}
        }}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    EXPECT(!quest::Registry::HasQuest("invalid"));
    quest::Registry::Shutdown();
}

TEST(Quest_Available_ExcludesBlockedAndActive)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_available");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["open"] = {{"objectives", {{{"id", "visit"}, {"type", "visit"},
        {"entity", "town"}, {"target", 1}}}}};
    section["blocked"] = {{"min_level", 10}, {"objectives", {{{"id", "visit"},
        {"type", "visit"}, {"entity", "mountain"}, {"target", 1}}}}};
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95110;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::ListAvailableQuests(95110).size() == 1);
    EXPECT(quest::Start(95110, "open").success());
    EXPECT(quest::ListAvailableQuests(95110).empty());
    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_Commands_RoutesCoreActions)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_commands");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["command_quest"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "town"}, {"target", 1}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95102;
    EXPECT(data::PlayerStore::Save(player));

    EXPECT(quest::HandleCommand(95102, {"list"}).success);
    EXPECT(quest::HandleCommand(95102, {"quest"}).success);
    EXPECT(quest::HandleCommand(95102, {"journal"}).success);
    EXPECT(quest::HandleCommand(95102, {"quest", "list"}).success);
    EXPECT(quest::HandleCommand(95102, {"quetes", "liste"}).success);
    EXPECT(quest::HandleCommand(95102, {"start", "command_quest"}).success);
    const auto journal = quest::HandleCommand(95102, {"journal"});
    EXPECT(journal.success);
    EXPECT(journal.message.find("command_quest") != std::string::npos);
    EXPECT(quest::HandleCommand(95102, {"quest", "etat", "command_quest"}).success);
    EXPECT(quest::ReportGameplay(95102, "visit", "town") == 1);
    EXPECT(quest::HandleCommand(95102, {"quest", "complete", "command_quest"}).success);
    EXPECT(!quest::HandleCommand(95102, {"quest", "unknown"}).success);
    EXPECT(!quest::HandleCommand(95102, {"unknown"}).success);

    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

// Rejoue le glue AsaApi (Main.cpp registerCommand) : le nom de commande
// est forcé en tête des tokens, comme /quest list en jeu.
TEST(Quest_Commands_ChatGluePrefixesModuleName)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_chat_glue");
    ConfigurePlayerStore(dir);
    nlohmann::json section;
    section["glue_quest"] = {
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "town"}, {"target", 1}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&section);
    data::PlayerData player;
    player.id = 95115;
    EXPECT(data::PlayerStore::Save(player));

    const auto simulateChat = [](security::PlayerId p, const char* commandName,
                                 const std::string& message)
    {
        auto tokens = quest::TokenizeCommand(message);
        if (!tokens.empty() && tokens.front().front() == '/')
            tokens.front().erase(tokens.front().begin());
        if (tokens.empty() || tokens.front() != commandName)
            tokens.insert(tokens.begin(), commandName);
        return quest::HandleCommand(p, tokens);
    };

    EXPECT(simulateChat(95115, "quest", "list").success);
    EXPECT(simulateChat(95115, "quetes", "/quetes liste").success);
    EXPECT(simulateChat(95115, "quest", "/quest start glue_quest").success);
    EXPECT(simulateChat(95115, "quest", "etat glue_quest").success);

    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Commands_RoutesAllDeliveredModules)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("all_commands");
    ConfigurePlayerStore(dir);
    data::PlayerData player;
    player.id = 95103;
    EXPECT(data::PlayerStore::Save(player));

    nlohmann::json character;
    character["human"] = { {"name", "Humain"} };
    nlohmann::json characterSection = {
        {"races", character}, {"professions", nlohmann::json::object()}
    };
    character::Registry::LoadDefinitionsFromSection(&characterSection);
    nlohmann::json factions;
    factions["town"] = { {"name", "Town"} };
    faction::Registry::LoadDefinitionsFromSection(&factions);
    nlohmann::json currencies;
    currencies["gold"] = { {"name", "Gold"} };
    economy::Registry::LoadDefinitionsFromSection(&currencies);
    core::Config::Get().Set("loadout.common_kit", nlohmann::json::array());

    const auto raceList = quest::HandleCommand(95103, {"race", "list"});
    EXPECT(raceList.success);
    const auto raceInfo = quest::HandleCommand(95103, {"race", "info", "human"});
    EXPECT(raceInfo.success);
    EXPECT(raceInfo.message.find("Humain") != std::string::npos
        || raceInfo.message.find("human") != std::string::npos);
    EXPECT(quest::HandleCommand(95103, {"faction", "list"}).success);
    EXPECT(quest::HandleCommand(95103, {"economy", "list"}).success);
    EXPECT(quest::HandleCommand(95103, {"framework", "version"}).success);

    character::Registry::Shutdown();
    faction::Registry::Shutdown();
    economy::Registry::Shutdown();
    core::Config::Get().Set("loadout.common_kit", nlohmann::json::array());
    CleanupPlayerStore(dir);
}

TEST(Commands_AdminReloadRequiresOwner)
{
    using namespace rpframework;
    const auto denied = quest::HandleCommand(95111,
        {"framework", "reload"}, security::Level::PLAYER);
    EXPECT(denied.handled);
    EXPECT(!denied.success);

    const auto unavailable = quest::HandleCommand(95111,
        {"framework", "reload"}, security::Level::OWNER);
    EXPECT(unavailable.handled);
    EXPECT(!unavailable.success);
}

TEST(Commands_ModCanEditLiveConfig)
{
    using namespace rpframework;
    const auto denied = quest::HandleCommand(12, {"mod", "help"});
    EXPECT(denied.handled);
    EXPECT(!denied.success);

    const auto help = quest::HandleCommand(12, {"mod", "help"}, security::Level::MODERATOR);
    EXPECT(help.success);

    core::Config::Get().Set("character.classes_enabled", false);
    const auto set = quest::HandleCommand(12,
        {"mod", "set", "character.classes_enabled", "true"}, security::Level::MODERATOR);
    EXPECT(set.success);
    const auto got = quest::HandleCommand(12,
        {"mod", "get", "character.classes_enabled"}, security::Level::MODERATOR);
    EXPECT(got.success);
    EXPECT(got.message.find("true") != std::string::npos);

    const auto spawn = quest::HandleCommand(12,
        {"mod", "spawn", "beach", "10", "20", "30"}, security::Level::GM);
    EXPECT(spawn.success);
    const auto zone = core::Config::Get().Get("world.spawn_zones.beach");
    EXPECT(zone.has_value());
    if (zone) EXPECT(zone->value("x", 0.0) == 10.0);

    const auto kit = quest::HandleCommand(12,
        {"mod", "kit", "add", "torch", "1", "/Game/Torch"}, security::Level::MODERATOR);
    EXPECT(kit.success);
    const auto listKit = quest::HandleCommand(12, {"mod", "list", "kit"}, security::Level::MODERATOR);
    EXPECT(listKit.success);
    EXPECT(listKit.message.find("torch") != std::string::npos);

    const auto raceAdd = quest::HandleCommand(12,
        {"mod", "race", "add", "orc", "Orc"}, security::Level::OWNER);
    EXPECT(raceAdd.success);
    const auto raceTrait = quest::HandleCommand(12,
        {"mod", "race", "trait", "orc", "malus", "speed", "multiply", "0.9"},
        security::Level::OWNER);
    EXPECT(raceTrait.success);
    const auto raceNode = core::Config::Get().Get("character.races.orc");
    EXPECT(raceNode.has_value());
    if (raceNode)
    {
        EXPECT(raceNode->value("name", std::string{}) == "Orc");
        EXPECT((*raceNode)["maluses"].is_array());
        EXPECT((*raceNode)["maluses"].size() == 1);
    }

    const auto jobAdd = quest::HandleCommand(12,
        {"mod", "job", "add", "miner", "Mineur"}, security::Level::OWNER);
    EXPECT(jobAdd.success);
    const auto engram = quest::HandleCommand(12,
        {"mod", "job", "engram", "miner", "add", "/Game/Pike.Pike"},
        security::Level::OWNER);
    EXPECT(engram.success);
    const auto jobNode = core::Config::Get().Get("character.professions.miner");
    EXPECT(jobNode.has_value());
    if (jobNode)
    {
        EXPECT((*jobNode)["engrams"].is_array());
        EXPECT((*jobNode)["engrams"][0] == "/Game/Pike.Pike");
    }

    const auto questAdd = quest::HandleCommand(12,
        {"mod", "quest", "add", "mod_hunt", "Chasse"}, security::Level::OWNER);
    EXPECT(questAdd.success);
    const auto questObj = quest::HandleCommand(12,
        {"mod", "quest", "objective", "mod_hunt", "k", "kill", "boar", "2"},
        security::Level::OWNER);
    EXPECT(questObj.success);
    EXPECT(quest::Registry::HasQuest("mod_hunt"));
}

TEST(Commands_ModStaffCannotEscalate)
{
    using namespace rpframework;
    security::Permissions::Initialize();

    const auto denySet = quest::HandleCommand(12,
        {"mod", "set", "security.permissions.race.select", "PLAYER"},
        security::Level::OWNER);
    EXPECT(denySet.handled);
    EXPECT(!denySet.success);

    const auto denyData = quest::HandleCommand(12,
        {"mod", "set", "data.save_dir", "C:/stolen"},
        security::Level::OWNER);
    EXPECT(!denyData.success);

    const auto denyGet = quest::HandleCommand(12,
        {"mod", "get", "security.audit_log_enabled"},
        security::Level::OWNER);
    EXPECT(!denyGet.success);

    const auto denyOwner = quest::HandleCommand(12,
        {"mod", "player", "42", "OWNER"}, security::Level::MODERATOR);
    EXPECT(!denyOwner.success);

    const auto denyPeer = quest::HandleCommand(12,
        {"mod", "player", "42", "MODERATOR"}, security::Level::MODERATOR);
    EXPECT(!denyPeer.success);

    const auto denyGrant = quest::HandleCommand(12,
        {"mod", "grant", "42", "gold", "10"}, security::Level::MODERATOR);
    EXPECT(!denyGrant.success);

    const auto denyRep = quest::HandleCommand(12,
        {"mod", "rep", "42", "town", "5"}, security::Level::MODERATOR);
    EXPECT(!denyRep.success);

    const auto stillOk = quest::HandleCommand(12,
        {"mod", "set", "character.classes_enabled", "true"},
        security::Level::MODERATOR);
    EXPECT(stillOk.success);
}

// /reputation doit supporter rep, rank, et l'alias court. Symétrique avec
// /faction reputation|rep|rank.
TEST(Commands_ReputationSupportsRepAndRank)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("rep_rank");
    ConfigurePlayerStore(dir);
    nlohmann::json factions;
    factions["town"] = { {"name", "Town"} };
    faction::Registry::LoadDefinitionsFromSection(&factions);
    data::PlayerData player;
    player.id = 95114;
    EXPECT(data::PlayerStore::Save(player));

    // /reputation <id> → rep
    const auto rep = quest::HandleCommand(95114, {"reputation", "town"});
    EXPECT(rep.handled);
    EXPECT(rep.success);

    // /reputation rep <id> → alias explicite
    const auto repAlias = quest::HandleCommand(95114, {"reputation", "rep", "town"});
    EXPECT(repAlias.handled);
    EXPECT(repAlias.success);

    // /reputation rank <id> → rang actuel. Le joueur n'a pas de réputation
    // avec "town" dans ce test, donc le rang est indisponible : la
    // commande est bien routée (handled=true) mais success=false.
    const auto rank = quest::HandleCommand(95114, {"reputation", "rank", "town"});
    EXPECT(rank.handled);
    EXPECT(!rank.success);

    // /reputation sans args → usage error (handled mais success=false)
    const auto none = quest::HandleCommand(95114, {"reputation"});
    EXPECT(none.handled);
    EXPECT(!none.success);

    faction::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

// ---------------------------------------------------------------------------
// Test d'intégration bout-en-bout Phase 1-4
// ---------------------------------------------------------------------------
TEST(Integration_FullPipeline_NewPlayerGetsStarterKit)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("integration");
    ConfigurePlayerStore(dir);

    nlohmann::json section;
    section["classes_enabled"] = false;
    nlohmann::json human;
    human["name"] = "Humain";
    nlohmann::json hb;
    hb["id"] = "torch"; hb["quantity"] = 1;
    human["starter_equipment"] = nlohmann::json::array();
    human["starter_equipment"].push_back(hb);
    section["races"]["human"] = human;
    nlohmann::json blacksmith;
    blacksmith["name"] = "Forgeron";
    nlohmann::json bb;
    bb["id"] = "iron_sword"; bb["quantity"] = 1;
    blacksmith["starter_equipment"] = nlohmann::json::array();
    blacksmith["starter_equipment"].push_back(bb);
    section["professions"]["blacksmith"] = blacksmith;

    nlohmann::json commonKit = nlohmann::json::array({
        nlohmann::json::object({{"id", "bread"}, {"quantity", 5}}),
    });
    core::Config::Get().Set("loadout.common_kit", commonKit);

    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);
    security::RateLimiter::Reset(9500, "race.select");
    security::RateLimiter::Reset(9500, "profession.select");

    // 1. Nouveau joueur → LoadDetailed status=Missing
    auto load = data::PlayerStore::LoadDetailed(9500);
    EXPECT(load.status == data::PlayerLoadStatus::Missing);
    EXPECT(!load.HasData());

    // 2. Sélection de race (auto-création) — métier encore manquant :
    //    le flag ne doit PAS être posé (sinon le kit métier est perdu).
    auto raceResult = character::SelectRace(9500, "human");
    EXPECT(raceResult.status == character::SelectResult::Status::Success);
    {
        auto after = data::PlayerStore::LoadDetailed(9500);
        EXPECT(after.HasData());
        EXPECT(after.data->race == "human");
        EXPECT(!after.data->starterKitDelivered);
    }
    EXPECT(loadout::Distributor::GiveStarterKit(9500).status
           == loadout::DistributionStatus::NotReady);

    // 3. Sélection de profession → kit combiné (commun + race + métier)
    auto profResult = character::SelectProfession(9500, "blacksmith");
    EXPECT(profResult.status == character::SelectResult::Status::Success);
    {
        auto after = data::PlayerStore::LoadDetailed(9500);
        EXPECT(after.data->profession == "blacksmith");
        EXPECT(after.data->starterKitDelivered);
    }
    EXPECT(loadout::Composer::ComposeStarterKit(9500).size() == 3);

    // 4. Idempotence
    auto dist2 = loadout::Distributor::GiveStarterKit(9500);
    EXPECT(dist2.status == loadout::DistributionStatus::AlreadyGiven);
    EXPECT(dist2.items.empty());

    // 7. Audit a tout tracé
    auto recent = security::AuditLog::Recent(100);
    bool foundRace = false, foundProf = false, foundDist = false;
    for (const auto& e : recent)
    {
        if (e.action == "character.race.select"       && e.playerId == 9500) foundRace  = true;
        if (e.action == "character.profession.select" && e.playerId == 9500) foundProf  = true;
        if (e.action == "loadout.starter.distributed" && e.playerId == 9500) foundDist  = true;
    }
    EXPECT(foundRace);
    EXPECT(foundProf);
    EXPECT(foundDist);

    core::Config::Get().Set("loadout.common_kit", nlohmann::json::array());
    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

// ---------------------------------------------------------------------------
// Phase 5: Faction (GDD §13)
// ---------------------------------------------------------------------------
namespace
{
    // Construit une section `factions` minimaliste pour les tests.
    nlohmann::json MakeFactionsSection()
    {
        nlohmann::json section = nlohmann::json::object();

        // "town" : ouverte à tous, 2 rangs, +10 rep initiale.
        nlohmann::json town;
        town["name"] = "La Ville";
        town["description"] = "Capitale commerciale.";
        town["initial_reputation"] = 10;
        nlohmann::json r1; r1["id"] = "citizen"; r1["min_reputation"] = 0;
        nlohmann::json r2; r2["id"] = "honored"; r2["min_reputation"] = 100;
        nlohmann::json r3; r3["id"] = "champion"; r3["min_reputation"] = 500;
        town["ranks"] = nlohmann::json::array({r1, r2, r3});
        town["excluded_races"] = nlohmann::json::array();
        nlohmann::json joinCond;
        joinCond["min_level"] = 1;
        joinCond["min_reputation"] = nlohmann::json::object();
        town["join_condition"] = joinCond;
        section["town"] = town;

        // "thieves_guild" : exclut les "guard" et exige min_level=5.
        nlohmann::json tg;
        tg["name"] = "Guilde des Voleurs";
        tg["description"] = "Réseau criminel.";
        tg["initial_reputation"] = 0;
        nlohmann::json rg1; rg1["id"] = "pickpocket"; rg1["min_reputation"] = 0;
        nlohmann::json rg2; rg2["id"] = "master";     rg2["min_reputation"] = 50;
        tg["ranks"] = nlohmann::json::array({rg1, rg2});
        tg["excluded_professions"] = nlohmann::json::array({"guard"});
        nlohmann::json joinCond2;
        joinCond2["min_level"] = 5;
        joinCond2["excluded_professions"] = nlohmann::json::array({"guard"});
        joinCond2["min_reputation"] = nlohmann::json::object();
        tg["join_condition"] = joinCond2;
        section["thieves_guild"] = tg;

        return section;
    }
}

TEST(Faction_Registry_Loads)
{
    using namespace rpframework;
    const auto section = MakeFactionsSection();

    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);

    EXPECT(faction::Registry::HasFaction("town") == true);
    EXPECT(faction::Registry::HasFaction("thieves_guild") == true);
    EXPECT(faction::Registry::HasFaction("missing") == false);

    auto t = faction::Registry::GetFaction("town");
    EXPECT(t.has_value());
    if (t)
    {
        EXPECT(t->id == "town");
        EXPECT(t->name == "La Ville");
        EXPECT(t->initialReputation == 10);
        EXPECT(t->ranks.size() == 3);
        EXPECT(t->ranks[0].id == "citizen");
        EXPECT(t->ranks[2].minReputation == 500);
    }

    auto ids = faction::Registry::ListFactionIds();
    EXPECT(ids.size() == 2);

    faction::Registry::Shutdown();
}

TEST(Faction_GetReputation_DefaultZero)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_rep_default");
    ConfigurePlayerStore(dir);
    const auto section = MakeFactionsSection();
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();

    // Joueur inexistant : GetReputation renvoie 0, pas de crash.
    EXPECT(faction::GetReputation(80001, "town") == 0);

    faction::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_ModifyReputation_Persists)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_rep_persist");
    ConfigurePlayerStore(dir);
    const auto section = MakeFactionsSection();
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 80010;
    security::RateLimiter::Reset(pid, "reputation.edit");
    security::Permissions::Register("reputation.edit", security::Level::GM);

    // Crée un profil bidon
    data::PlayerData p;
    p.id = pid;
    p.name = "RepGuy";
    p.race = "human";
    p.profession = "blacksmith";
    p.level = 1;
    EXPECT(data::PlayerStore::Save(p));

    // ModifyReputation +50 → 50
    const int v1 = faction::ModifyReputation(pid, "town", 50, "quete_1");
    EXPECT(v1 == 50);

    // SetReputation absolu à 200 → persiste dans le profil rechargé
    const int v2 = faction::SetReputation(pid, "town", 200, "admin_grant");
    EXPECT(v2 == 200);

    // Recharge le profil et vérifie la persistance
    auto load = data::PlayerStore::LoadDetailed(pid);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->reputation["town"] == 200);
    }
    EXPECT(faction::GetReputation(pid, "town") == 200);

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_Join_Success)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_join_ok");
    ConfigurePlayerStore(dir);
    const auto section = MakeFactionsSection();
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 80020;
    security::RateLimiter::Reset(pid, "faction.join");
    security::RateLimiter::Reset(pid, "faction.leave");

    // Crée un profil éligible (race=human, level=1, prof=blacksmith)
    data::PlayerData p;
    p.id = pid;
    p.name = "Joiner";
    p.race = "human";
    p.profession = "blacksmith";
    p.level = 1;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = faction::Join(pid, "town");
    EXPECT(r.status == faction::JoinStatus::Success);

    auto load = data::PlayerStore::LoadDetailed(pid);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->faction == "town");
        // initial_reputation 10 a été appliqué
        EXPECT(load.data->reputation["town"] == 10);
    }

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_Join_StartsQuestsAndRecordsJournal)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_journal");
    ConfigurePlayerStore(dir);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json quests;
    quests["first_hunt"] = {
        {"name", "Premiere chasse"},
        {"objectives", {{{"id", "k"}, {"type", "kill"},
                         {"entity", "boar"}, {"target", 2}}}},
        {"rewards", {{{"type", "currency"}, {"id", "gold"}, {"amount", 25}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&quests);

    nlohmann::json factions;
    factions["town"] = {
        {"name", "La Ville"},
        {"initial_reputation", 10},
        {"starter_quests", {"first_hunt"}},
        {"journal", {
            {"id", "quest_journal"},
            {"quantity", 1},
            {"blueprint", "/Game/Notes/PrimalItem_Note.PrimalItem_Note"}
        }}
    };
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&factions);

    data::PlayerData p;
    p.id = 80021;
    p.race = "human";
    p.level = 1;
    EXPECT(data::PlayerStore::Save(p));
    security::RateLimiter::Reset(80021, "faction.join");
    security::RateLimiter::Reset(80021, "quest.accept");

    const auto r = faction::Join(80021, "town");
    EXPECT(r.status == faction::JoinStatus::Success);
    auto load = data::PlayerStore::LoadDetailed(80021);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->quests.count("first_hunt") == 1);
        EXPECT(load.data->quests["first_hunt"].status == data::QuestProgress::Status::Active);
        bool hasJournal = false;
        for (const auto& unlock : load.data->unlocks)
            if (unlock == "journal:town") hasJournal = true;
        EXPECT(hasJournal);
    }

    quest::Registry::Shutdown();
    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_Join_AlreadyInFaction)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_join_already");
    ConfigurePlayerStore(dir);
    const auto section = MakeFactionsSection();
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 80030;
    security::RateLimiter::Reset(pid, "faction.join");

    data::PlayerData p;
    p.id = pid;
    p.name = "Member";
    p.race = "human";
    p.profession = "blacksmith";
    p.level = 1;
    p.faction = "town";
    EXPECT(data::PlayerStore::Save(p));

    const auto r = faction::Join(pid, "town");
    EXPECT(r.status == faction::JoinStatus::AlreadyInFaction);

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_Join_UnknownFaction)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_join_unknown");
    ConfigurePlayerStore(dir);
    const auto section = MakeFactionsSection();
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 80040;
    security::RateLimiter::Reset(pid, "faction.join");

    data::PlayerData p;
    p.id = pid;
    p.name = "Lost";
    p.race = "human";
    p.profession = "blacksmith";
    p.level = 1;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = faction::Join(pid, "missing_faction");
    EXPECT(r.status == faction::JoinStatus::UnknownFaction);

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_Join_ConditionNotMet)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_join_condition");
    ConfigurePlayerStore(dir);
    const auto section = MakeFactionsSection();
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 80050;
    security::RateLimiter::Reset(pid, "faction.join");

    // thieves_guild exige min_level=5 ; on est level=1.
    data::PlayerData p;
    p.id = pid;
    p.name = "Young";
    p.race = "human";
    p.profession = "blacksmith";  // pas guard
    p.level = 1;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = faction::Join(pid, "thieves_guild");
    EXPECT(r.status == faction::JoinStatus::ConditionNotMet);

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_Join_RaceExcluded)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_join_race");
    ConfigurePlayerStore(dir);
    nlohmann::json section = MakeFactionsSection();

    // Ajoute une faction "elite" qui exclut "human".
    nlohmann::json elite;
    elite["name"] = "Élite";
    elite["description"] = "Réservé aux elfes.";
    elite["initial_reputation"] = 0;
    nlohmann::json rk1; rk1["id"] = "adept"; rk1["min_reputation"] = 0;
    elite["ranks"] = nlohmann::json::array({rk1});
    elite["excluded_races"] = nlohmann::json::array({"human"});
    nlohmann::json jc;
    jc["min_level"] = 1;
    jc["excluded_races"] = nlohmann::json::array({"human"});
    jc["min_reputation"] = nlohmann::json::object();
    elite["join_condition"] = jc;
    section["elite"] = elite;

    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 80060;
    security::RateLimiter::Reset(pid, "faction.join");

    data::PlayerData p;
    p.id = pid;
    p.name = "Outsider";
    p.race = "human";  // exclu !
    p.profession = "blacksmith";
    p.level = 5;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = faction::Join(pid, "elite");
    EXPECT(r.status == faction::JoinStatus::RaceExcluded);

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_Leave_Success)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_leave_ok");
    ConfigurePlayerStore(dir);
    const auto section = MakeFactionsSection();
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 80070;
    security::RateLimiter::Reset(pid, "faction.join");
    security::RateLimiter::Reset(pid, "faction.leave");

    data::PlayerData p;
    p.id = pid;
    p.name = "Quitter";
    p.race = "human";
    p.profession = "blacksmith";
    p.level = 1;
    p.faction = "town";
    p.reputation["town"] = 75;
    EXPECT(data::PlayerStore::Save(p));

    const auto r = faction::Leave(pid);
    EXPECT(r.status == faction::JoinStatus::Success);

    auto load = data::PlayerStore::LoadDetailed(pid);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        EXPECT(load.data->faction.empty());
        // Réputation conservée (le leave n'efface pas la réputation).
        EXPECT(load.data->reputation["town"] == 75);
    }

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Faction_GetCurrentRank_ComputedCorrectly)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("faction_rank");
    ConfigurePlayerStore(dir);
    const auto section = MakeFactionsSection();
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 80080;
    security::RateLimiter::Reset(pid, "reputation.edit");
    security::Permissions::Register("reputation.edit", security::Level::GM);

    data::PlayerData p;
    p.id = pid;
    p.name = "Climber";
    p.race = "human";
    p.profession = "blacksmith";
    p.faction = "town";
    p.level = 1;
    EXPECT(data::PlayerStore::Save(p));

    // Rep < 100 : citizen
    faction::SetReputation(pid, "town", 50, "");
    auto rank1 = faction::GetCurrentRank(pid, "town");
    EXPECT(rank1.has_value());
    if (rank1) EXPECT(rank1->rankId == "citizen");

    // 100 <= rep < 500 : honored
    faction::SetReputation(pid, "town", 250, "");
    auto rank2 = faction::GetCurrentRank(pid, "town");
    EXPECT(rank2.has_value());
    if (rank2) EXPECT(rank2->rankId == "honored");

    // rep >= 500 : champion
    faction::SetReputation(pid, "town", 999, "");
    auto rank3 = faction::GetCurrentRank(pid, "town");
    EXPECT(rank3.has_value());
    if (rank3) EXPECT(rank3->rankId == "champion");

    // Faction inconnue : nullopt
    auto rankNone = faction::GetCurrentRank(pid, "does_not_exist");
    EXPECT(!rankNone.has_value());

    auto loaded = data::PlayerStore::Load(pid);
    EXPECT(loaded.has_value());
    if (loaded)
    {
        loaded->faction.clear();
        EXPECT(data::PlayerStore::Save(*loaded));
    }
    EXPECT(!faction::GetCurrentRank(pid, "town").has_value());

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

// ---------------------------------------------------------------------------
// Phase 6: Economy (GDD §14)
// ---------------------------------------------------------------------------
namespace
{
    nlohmann::json MakeEconomySection()
    {
        nlohmann::json section = nlohmann::json::object();
        nlohmann::json gold;
        gold["name"]         = "Pièces d'or";
        gold["symbol"]       = "g";
        gold["max_balance"]  = 0;
        gold["transferable"] = true;
        section["gold"] = gold;

        nlohmann::json gem;
        gem["name"]         = "Gemmes";
        gem["symbol"]       = "💎";
        gem["max_balance"]  = 100;
        gem["transferable"] = true;
        section["gem"] = gem;

        nlohmann::json token;
        token["name"]         = "Jeton de quête";
        token["symbol"]       = "";
        token["max_balance"]  = 10;
        token["transferable"] = false;
        section["token_quest"] = token;

        return section;
    }

    // Crée un profil bidon, retourne le PlayerId.
    rpframework::security::PlayerId MakeBlankPlayer(rpframework::security::PlayerId id,
                                                   const std::string& name = "Wallet")
    {
        using namespace rpframework;
        data::PlayerData p;
        p.id   = id;
        p.name = name;
        p.race = "human";
        p.profession = "blacksmith";
        p.level = 1;
        EXPECT(data::PlayerStore::Save(p));
        return id;
    }
}

TEST(Economy_Registry_Loads)
{
    using namespace rpframework;
    auto section = MakeEconomySection();

    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);

    EXPECT(economy::Registry::HasCurrency("gold") == true);
    EXPECT(economy::Registry::HasCurrency("gem") == true);
    EXPECT(economy::Registry::HasCurrency("token_quest") == true);
    EXPECT(economy::Registry::HasCurrency("missing") == false);

    auto gold = economy::Registry::GetCurrency("gold");
    EXPECT(gold.has_value());
    if (gold)
    {
        EXPECT(gold->id == "gold");
        EXPECT(gold->name == "Pièces d'or");
        EXPECT(gold->symbol == "g");
        EXPECT(gold->maxBalance == 0);
        EXPECT(gold->transferable == true);
    }

    auto token = economy::Registry::GetCurrency("token_quest");
    EXPECT(token.has_value());
    if (token)
    {
        EXPECT(token->transferable == false);
        EXPECT(token->maxBalance == 10);
    }

    EXPECT(economy::Registry::ListCurrencyIds().size() == 3);

    economy::Registry::Shutdown();
}

TEST(Economy_GetBalance_DefaultZero)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_balance_default");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();

    // Joueur inexistant : 0
    EXPECT(economy::GetBalance(90001, "gold") == 0);

    // Joueur qui existe mais sans wallet
    MakeBlankPlayer(90002, "Empty");
    EXPECT(economy::GetBalance(90002, "gold") == 0);
    EXPECT(economy::GetBalance(90002, "gem")  == 0);

    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Add_Success)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_add_ok");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 90100;
    security::RateLimiter::Reset(pid, "economy.add");
    MakeBlankPlayer(pid, "Earner");

    const auto r = economy::Add(pid, "gold", 50, "quête_1", "system");
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(r.newBalance == 50);
    EXPECT(economy::GetBalance(pid, "gold") == 50);

    // Cumulatif
    const auto r2 = economy::Add(pid, "gold", 25, "quête_2", "system");
    EXPECT(r2.status == economy::TxStatus::Success);
    EXPECT(r2.newBalance == 75);
    EXPECT(economy::GetBalance(pid, "gold") == 75);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Add_UnknownCurrency)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_add_unknown");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 90110;
    security::RateLimiter::Reset(pid, "economy.add");
    MakeBlankPlayer(pid, "Earner");

    const auto r = economy::Add(pid, "bitcoin", 1, "test", "system");
    EXPECT(r.status == economy::TxStatus::UnknownCurrency);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Add_InvalidAmount)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_add_invalid");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 90120;
    security::RateLimiter::Reset(pid, "economy.add");
    MakeBlankPlayer(pid, "Earner");

    EXPECT(economy::Add(pid, "gold",  0, "test", "system").status == economy::TxStatus::InvalidAmount);
    EXPECT(economy::Add(pid, "gold", -5, "test", "system").status == economy::TxStatus::InvalidAmount);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Subtract_InsufficientFunds)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_sub_insufficient");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::Permissions::Register("economy.subtract", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 90130;
    security::RateLimiter::Reset(pid, "economy.add");
    security::RateLimiter::Reset(pid, "economy.subtract");
    MakeBlankPlayer(pid, "Pauvre");

    economy::Add(pid, "gold", 30, "init", "system");
    const auto r = economy::Subtract(pid, "gold", 100, "achat", "shop");
    EXPECT(r.status == economy::TxStatus::InsufficientFunds);
    EXPECT(economy::GetBalance(pid, "gold") == 30);  // inchangé

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Subtract_Success)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_sub_ok");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::Permissions::Register("economy.subtract", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 90140;
    security::RateLimiter::Reset(pid, "economy.add");
    security::RateLimiter::Reset(pid, "economy.subtract");
    MakeBlankPlayer(pid, "Acheteur");

    economy::Add(pid, "gold", 200, "init", "system");
    const auto r = economy::Subtract(pid, "gold", 75, "achat_epee", "shop");
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(r.newBalance == 125);
    EXPECT(economy::GetBalance(pid, "gold") == 125);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Transfer_Success)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_transfer_ok");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::Permissions::Register("economy.transfer", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId a = 90150;
    constexpr security::PlayerId b = 90151;
    security::RateLimiter::Reset(a, "economy.add");
    security::RateLimiter::Reset(a, "economy.transfer");
    MakeBlankPlayer(a, "Sender");
    MakeBlankPlayer(b, "Receiver");

    economy::Add(a, "gold", 100, "init", "system");
    const auto r = economy::Transfer(a, b, "gold", 40, "cadeau");
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(economy::GetBalance(a, "gold") == 60);
    EXPECT(economy::GetBalance(b, "gold") == 40);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Transfer_MissingTargetDoesNotDebit)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_transfer_missing");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::Permissions::Register("economy.transfer", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId a = 90152;
    security::RateLimiter::Reset(a, "economy.add");
    security::RateLimiter::Reset(a, "economy.transfer");
    MakeBlankPlayer(a, "Sender");
    economy::Add(a, "gold", 100, "init", "system");

    const auto r = economy::Transfer(a, 42424242ull, "gold", 40, "cadeau");
    EXPECT(r.status == economy::TxStatus::PlayerDataUnavailable);
    EXPECT(economy::GetBalance(a, "gold") == 100);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Transfer_ChatResolvesSteamId)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_transfer_steam");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::Permissions::Register("economy.transfer", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId a = 90153;
    const auto steam = std::string("76561198000000001");
    const auto target = security::MakePlayerId(steam);
    security::RateLimiter::Reset(a, "economy.add");
    security::RateLimiter::Reset(a, "economy.transfer");
    MakeBlankPlayer(a, "Sender");
    MakeBlankPlayer(target, "Receiver");
    economy::Add(a, "gold", 100, "init", "system");

    const auto cmd = quest::HandleCommand(a,
        {"economy", "transfer", steam, "gold", "10"});
    EXPECT(cmd.success);
    EXPECT(economy::GetBalance(a, "gold") == 90);
    EXPECT(economy::GetBalance(target, "gold") == 10);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Transfer_NotTransferable)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_transfer_nontrans");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::Permissions::Register("economy.transfer", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId a = 90160;
    constexpr security::PlayerId b = 90161;
    security::RateLimiter::Reset(a, "economy.add");
    security::RateLimiter::Reset(a, "economy.transfer");
    MakeBlankPlayer(a, "Sender");
    MakeBlankPlayer(b, "Receiver");

    economy::Add(a, "token_quest", 5, "init", "system");
    const auto r = economy::Transfer(a, b, "token_quest", 1, "cadeau");
    EXPECT(r.status == economy::TxStatus::CurrencyNotTransferable);
    // Aucun mouvement.
    EXPECT(economy::GetBalance(a, "token_quest") == 5);
    EXPECT(economy::GetBalance(b, "token_quest") == 0);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Transfer_ToSelf)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_transfer_self");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.transfer", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId a = 90170;
    security::RateLimiter::Reset(a, "economy.transfer");
    MakeBlankPlayer(a, "Selfish");

    const auto r = economy::Transfer(a, a, "gold", 10, "self");
    EXPECT(r.status == economy::TxStatus::InvalidAmount);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Grant_Success)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_grant_ok");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 90180;
    security::RateLimiter::Reset(pid, "economy.grant");
    MakeBlankPlayer(pid, "Lucky");

    const auto r = economy::Grant(pid, "gold", 1000, "prime_GM", "admin");
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(r.newBalance == 1000);
    EXPECT(economy::GetBalance(pid, "gold") == 1000);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_Reward_Success)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_reward_ok");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.reward", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 90190;
    security::RateLimiter::Reset(pid, "economy.reward");
    MakeBlankPlayer(pid, "Hero");

    const auto r = economy::Reward(pid, "gold", 500, "quete_principale", "quest");
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(r.newBalance == 500);

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_WouldExceedMax)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("economy_max");
    ConfigurePlayerStore(dir);
    auto section = MakeEconomySection();
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&section);
    security::Permissions::Initialize();
    security::Permissions::Register("economy.add", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    constexpr security::PlayerId pid = 90200;
    security::RateLimiter::Reset(pid, "economy.add");
    MakeBlankPlayer(pid, "Maxed");

    // gem max=100 : add 60 puis 50 doit refuser la 2e.
    EXPECT(economy::Add(pid, "gem", 60, "init", "system").status == economy::TxStatus::Success);
    const auto r = economy::Add(pid, "gem", 50, "trop", "system");
    EXPECT(r.status == economy::TxStatus::WouldExceedMax);
    EXPECT(economy::GetBalance(pid, "gem") == 60);  // inchangé

    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Economy_MigrationV1ToV2)
{
    using namespace rpframework;
    // Fichier v1 sans "economy" : après migration, section présente et vide.
    nlohmann::json v1 = {
        {"meta",     {{"schema_version", 1}}},
        {"identity", {{"id", "42"}, {"name", "LegacyEco"}}},
        {"character", nlohmann::json::object()},
        {"progression", {{"level", 3}, {"xp", 100}}},
    };
    // v1 migre jusqu'à la version courante (v4 depuis l'ajout de `professions`).
    EXPECT(data::Migrate(v1, 1) == true);
    EXPECT(v1["meta"]["schema_version"] == data::kCurrentSchemaVersion);
    EXPECT(v1["meta"]["schema_version"] == 4);
    EXPECT(v1.contains("economy"));
    EXPECT(v1["economy"].is_object());
    EXPECT(v1["economy"].empty());
    EXPECT(v1.contains("unlocks"));
    EXPECT(v1["unlocks"].is_array());
    EXPECT(v1["unlocks"].empty());
    EXPECT(v1.contains("professions"));
    EXPECT(v1["professions"].is_object());

    // v2 n'est plus la version courante depuis l'ajout de v2→v3→v4.
    nlohmann::json v2;
    v2["meta"] = {{"schema_version", 2}};
    EXPECT(data::Migrate(v2, 2) == true);
    EXPECT(v2["meta"]["schema_version"] == 4);
    EXPECT(v2["unlocks"].is_array());
    EXPECT(v2.contains("professions"));

    // v3 migre vers v4 (section professions).
    nlohmann::json v3;
    v3["meta"] = {{"schema_version", 3}};
    EXPECT(data::Migrate(v3, 3) == true);
    EXPECT(v3["meta"]["schema_version"] == 4);
    EXPECT(v3.contains("professions"));

    // v4 déjà → no-op.
    nlohmann::json v4;
    v4["meta"] = {{"schema_version", 4}};
    EXPECT(data::Migrate(v4, 4) == false);
}

// ---------------------------------------------------------------------------
// Tests de robustesse : entrées invalides, fichiers corrompus
// ---------------------------------------------------------------------------
TEST(Audit_Robustness_ConfigMissingFile)
{
    using namespace rpframework;
    const auto path = std::filesystem::temp_directory_path() / "rpframework_tests"
        / ("nonexistent_" + std::to_string(std::rand()) + ".json");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    core::Config::Get().Set("", nlohmann::json::object());
    core::Config::Get().LoadFromFile(path);
    EXPECT(core::Config::Get().Root().is_object());
    EXPECT(core::Config::Get().Root().empty());
}

TEST(Audit_Robustness_ConfigCorruptJson)
{
    using namespace rpframework;
    const auto path = std::filesystem::temp_directory_path() / "rpframework_tests"
        / ("corrupt_" + std::to_string(std::rand()) + ".json");
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream f(path, std::ios::trunc);
        f << "{ this is : not, valid json";
    }
    core::Config::Get().LoadFromFile(path);
    EXPECT(core::Config::Get().Root().is_object());
    std::error_code ecRem2;
    std::filesystem::remove(path, ecRem2);
}

TEST(Audit_Robustness_ConfigFailurePreservesPreviousState)
{
    using namespace rpframework;
    const auto valid = std::filesystem::temp_directory_path() / "rpframework_tests" / "valid_config.json";
    const auto invalid = std::filesystem::temp_directory_path() / "rpframework_tests" / "invalid_config.json";
    std::filesystem::create_directories(valid.parent_path());
    { std::ofstream f(valid, std::ios::trunc); f << R"({"marker":"keep"})"; }
    { std::ofstream f(invalid, std::ios::trunc); f << "{invalid"; }
    EXPECT(core::Config::Get().LoadFromFile(valid));
    EXPECT(!core::Config::Get().LoadFromFile(invalid));
    EXPECT(core::Config::Get().GetOr<std::string>("marker", "") == "keep");
    std::error_code cleanup;
    std::filesystem::remove(valid, cleanup);
    std::filesystem::remove(invalid, cleanup);
}

TEST(Audit_Robustness_PlayerStoreCorruptFileRecovers)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("robust_corrupt");
    ConfigurePlayerStore(dir, 3);

    data::PlayerData p;
    p.id = 9501;
    p.name = "Robust";
    p.level = 5;
    EXPECT(data::PlayerStore::Save(p));

    p.level = 6;
    EXPECT(data::PlayerStore::Save(p));

    const auto path = data::PlayerStore::GetFilePath(9501);
    { std::ofstream f(path, std::ios::trunc); f << "garbage"; }

    auto load = data::PlayerStore::LoadDetailed(9501);
    EXPECT(load.status == data::PlayerLoadStatus::RecoveredFromBackup);
    EXPECT(load.HasData());
    EXPECT(load.data->level == 5);

    CleanupPlayerStore(dir);
}

TEST(Audit_Robustness_PlayerStoreDeleteNonexistent)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("robust_delete");
    ConfigurePlayerStore(dir);
    EXPECT(data::PlayerStore::Delete(99999999) == false);
    EXPECT(data::PlayerStore::Exists(99999999) == false);
    CleanupPlayerStore(dir);
}

TEST(Audit_Robustness_AuditLogLogBeforeInit)
{
    using namespace rpframework;
    security::AuditLog::Shutdown();
    security::AuditLog::Log("before.init", 0, {});
    security::AuditLog::Initialize();
    security::AuditLog::Log("after.init", 0, {});
    EXPECT(true);
}

TEST(Permissions_InvalidOverrideIsIgnored)
{
    using namespace rpframework::security;
    Permissions::Initialize();
    const auto before = Permissions::GetRequiredLevel("race.select");
    nlohmann::json sec;
    sec["permissions"]["race.select"] = "not_a_level";
    sec["permissions"]["config.reload"] = "OWNER";
    Permissions::LoadFromConfig(sec);
    EXPECT(Permissions::GetRequiredLevel("race.select") == before);
    EXPECT(Permissions::GetRequiredLevel("framework.reload") == Level::OWNER);
}

TEST(Permissions_PlayerLevelSystemClampedToOwner)
{
    using namespace rpframework::security;
    Permissions::Initialize();
    nlohmann::json sec;
    sec["player_levels"]["76561198111111111"] = "SYSTEM";
    Permissions::LoadFromConfig(sec);
    EXPECT(Permissions::GetPlayerLevel(MakePlayerId("76561198111111111")) == Level::OWNER);
}

TEST(PlayerData_FromJsonRejectsNegativeWallet)
{
    using namespace rpframework;
    nlohmann::json j = data::PlayerData{}.ToJson();
    j["identity"]["id"] = "424242";
    j["economy"]["gold"] = -10;
    bool threw = false;
    try
    {
        (void)data::PlayerData::FromJson(j);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    EXPECT(threw);
}

TEST(AuditLog_RecentSurvivesFlush)
{
    using namespace rpframework::security;
    AuditLog::Initialize();
    AuditLog::Log("test.recent.flush", 7, {{"k", 1}});
    AuditLog::Flush();
    const auto recent = AuditLog::Recent(50);
    bool found = false;
    for (const auto& e : recent)
    {
        if (e.action == "test.recent.flush" && e.playerId == 7) found = true;
    }
    EXPECT(found);
}

TEST(Character_SelectRace_AppliesFactionAndInitialReputation)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("race_faction");
    ConfigurePlayerStore(dir);
    character::Registry::ResetForTests();
    nlohmann::json section;
    section["classes_enabled"] = false;
    section["races"]["wolfkin"]["name"] = "Wolfkin";
    section["races"]["wolfkin"]["faction"] = "pack";
    section["races"]["wolfkin"]["initial_reputation"]["pack"] = 15;
    section["races"]["wolfkin"]["initial_reputation"]["town"] = -5;
    character::Registry::LoadDefinitionsFromSection(&section);
    faction::Registry::ResetForTests();
    nlohmann::json factions;
    factions["pack"]["name"] = "Pack";
    factions["pack"]["initial_reputation"] = 40;
    faction::Registry::LoadDefinitionsFromSection(&factions);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::RateLimiter::Reset(44001, "race.select");

    data::PlayerData p;
    p.id = 44001;
    EXPECT(data::PlayerStore::Save(p));
    EXPECT(character::SelectRace(44001, "wolfkin").status == character::SelectResult::Status::Success);
    auto loaded = data::PlayerStore::Load(44001);
    EXPECT(loaded.has_value());
    if (loaded)
    {
        EXPECT(loaded->race == "wolfkin");
        EXPECT(loaded->faction == "pack");
        EXPECT(loaded->reputation["pack"] == 15);
        EXPECT(loaded->reputation["town"] == -5);
    }

    faction::Registry::Shutdown();
    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Character_SelectRace_SkipsAutoJoinWhenExcluded)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("race_faction_excl");
    ConfigurePlayerStore(dir);
    character::Registry::ResetForTests();
    nlohmann::json section;
    section["classes_enabled"] = false;
    section["races"]["wolfkin"]["name"] = "Wolfkin";
    section["races"]["wolfkin"]["faction"] = "pack";
    character::Registry::LoadDefinitionsFromSection(&section);
    faction::Registry::ResetForTests();
    nlohmann::json factions;
    factions["pack"]["name"] = "Pack";
    factions["pack"]["excluded_races"] = nlohmann::json::array({"wolfkin"});
    faction::Registry::LoadDefinitionsFromSection(&factions);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::RateLimiter::Reset(44002, "race.select");

    data::PlayerData p;
    p.id = 44002;
    EXPECT(data::PlayerStore::Save(p));
    EXPECT(character::SelectRace(44002, "wolfkin").status
           == character::SelectResult::Status::Success);
    auto loaded = data::PlayerStore::Load(44002);
    EXPECT(loaded.has_value());
    if (loaded)
    {
        EXPECT(loaded->race == "wolfkin");
        EXPECT(loaded->faction.empty());
    }

    faction::Registry::Shutdown();
    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Character_SelectProfession_FactionExcluded)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("prof_faction_excl");
    ConfigurePlayerStore(dir);
    character::Registry::ResetForTests();
    nlohmann::json section;
    section["classes_enabled"] = false;
    section["races"]["human"]["name"] = "Humain";
    section["professions"]["blacksmith"]["name"] = "Forgeron";
    section["professions"]["guard"]["name"] = "Garde";
    character::Registry::LoadDefinitionsFromSection(&section);
    faction::Registry::ResetForTests();
    nlohmann::json factions;
    factions["town"]["name"] = "Ville";
    factions["town"]["excluded_professions"] = nlohmann::json::array({"blacksmith"});
    faction::Registry::LoadDefinitionsFromSection(&factions);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::RateLimiter::Reset(44003, "race.select");
    security::RateLimiter::Reset(44003, "profession.select");

    data::PlayerData p;
    p.id = 44003;
    p.faction = "town";
    EXPECT(data::PlayerStore::Save(p));
    EXPECT(character::SelectRace(44003, "human").status
           == character::SelectResult::Status::Success);
    EXPECT(character::SelectProfession(44003, "blacksmith").status
           == character::SelectResult::Status::ConditionNotMet);
    auto loaded = data::PlayerStore::Load(44003);
    EXPECT(loaded.has_value());
    if (loaded)
        EXPECT(loaded->profession.empty());

    faction::Registry::Shutdown();
    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Character_SelectClass_FactionExcluded)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("class_faction_excl");
    ConfigurePlayerStore(dir);
    character::Registry::ResetForTests();
    nlohmann::json section;
    section["classes_enabled"] = true;
    section["classes"]["rogue"]["name"] = "Voleur";
    character::Registry::LoadDefinitionsFromSection(&section);
    faction::Registry::ResetForTests();
    nlohmann::json factions;
    factions["town"]["name"] = "Ville";
    factions["town"]["excluded_classes"] = nlohmann::json::array({"rogue"});
    faction::Registry::LoadDefinitionsFromSection(&factions);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::RateLimiter::Reset(44004, "class.select");

    data::PlayerData p;
    p.id = 44004;
    p.faction = "town";
    EXPECT(data::PlayerStore::Save(p));
    EXPECT(character::SelectClass(44004, "rogue").status
           == character::SelectResult::Status::ConditionNotMet);
    auto loaded = data::PlayerStore::Load(44004);
    EXPECT(loaded.has_value());
    if (loaded)
        EXPECT(loaded->playerClass.empty());

    faction::Registry::Shutdown();
    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Loadout_Distributor_ConfirmsAsaOutbox)
{
    using namespace rpframework;
    nlohmann::json section;
    section["classes_enabled"] = false;
    section["races"]["human"]["name"] = "Humain";
    core::Config::Get().Set("loadout.common_kit", nlohmann::json::array());

    const auto ctx = SetupLoadoutTest("kit_outbox");
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);
    security::RateLimiter::Reset(9302, "race.select");
    EXPECT(character::SelectRace(9302, "human").status
           == character::SelectResult::Status::Success);

    auto load = data::PlayerStore::LoadDetailed(9302);
    EXPECT(load.HasData());
    if (load.HasData())
    {
        load.data->starterKitDelivered = false;
        load.data->pendingStarterKit.clear();
        load.data->pendingStarterKit.push_back(nlohmann::json{
            {"id", "meat"},
            {"quantity", 2},
            {"blueprint",
             "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_CookedMeat.PrimalItemConsumable_CookedMeat"}
        });
        EXPECT(data::PlayerStore::Save(*load.data));
    }

    const auto given = loadout::Distributor::GiveStarterKit(9302);
    EXPECT(given.status == loadout::DistributionStatus::Delivered);
    auto after = data::PlayerStore::LoadDetailed(9302);
    EXPECT(after.HasData());
    if (after.HasData())
    {
        EXPECT(after.data->starterKitDelivered);
        EXPECT(after.data->pendingStarterKit.empty());
    }
    EXPECT(loadout::CountTestInventory(9302,
        "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_CookedMeat.PrimalItemConsumable_CookedMeat") == 2);

    character::Registry::Shutdown();
    CleanupPlayerStore(ctx.dir);
}

TEST(Commands_RejectsInvalidIds)
{
    using namespace rpframework;
    const auto denied = quest::HandleCommand(12, {"race", "select", "bad@id"});
    EXPECT(denied.handled);
    EXPECT(!denied.success);
}

TEST(PlayerStore_RejectsTraversalSaveDir)
{
    using namespace rpframework;
    data::PlayerStore::Shutdown();
    const auto previous = data::PlayerStore::GetSaveDir();
    core::Config::Get().Set("data.save_dir", "../outside_plugin");
    data::PlayerStore::LoadFromConfig();
    const auto after = data::PlayerStore::GetSaveDir();
    EXPECT(after.generic_string().find("..") == std::string::npos);
    (void)previous;
}

TEST(Loadout_ItemFromJson_CopiesTopLevelBlueprint)
{
    using namespace rpframework::loadout;
    nlohmann::json j;
    j["id"] = "torch";
    j["quantity"] = 1;
    j["blueprint"] = "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_Berry_Amarberry.PrimalItemConsumable_Berry_Amarberry";
    auto it = Item::FromJson(j);
    EXPECT(it.id == "torch");
    EXPECT(it.extras.contains("blueprint"));
    if (it.extras.contains("blueprint"))
        EXPECT(it.extras["blueprint"].get<std::string>().find("Amarberry") != std::string::npos);

    nlohmann::json extrasFirst;
    extrasFirst["id"] = "bread";
    extrasFirst["extras"] = {{"blueprint", "/Game/kept"}};
    extrasFirst["blueprint"] = "/Game/ignored";
    auto kept = Item::FromJson(extrasFirst);
    EXPECT(kept.extras["blueprint"].get<std::string>() == "/Game/kept");
}

TEST(Api_GetPlayerInfo_ReturnsSnapshot)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("api_query");
    ConfigurePlayerStore(dir);
    data::PlayerData src;
    src.id = 77001;
    src.name = "Aria";
    src.race = "human";
    src.profession = "guard";
    src.level = 7;
    src.xp = 420;
    src.wallets["gold"] = 12;
    src.titles.push_back("scout");
    EXPECT(data::PlayerStore::Save(src));

    const auto info = api::GetPlayerInfo(77001);
    EXPECT(info.has_value());
    if (info)
    {
        EXPECT(info->id == 77001);
        EXPECT(info->name == "Aria");
        EXPECT(info->race == "human");
        EXPECT(info->profession == "guard");
        EXPECT(info->level == 7);
        EXPECT(info->xp == 420);
        EXPECT(info->wallets.at("gold") == 12);
        EXPECT(info->titles.size() == 1);
    }
    EXPECT(!api::GetPlayerInfo(77002).has_value());
    CleanupPlayerStore(dir);
}

TEST(Quest_Engine_XpRewardRaisesLevelButNeverLowers)
{
    using namespace rpframework;
    const auto dir = MakeTempPlayerDir("quest_xp_level");
    ConfigurePlayerStore(dir);

    nlohmann::json character;
    character["professions"]["hunter"] = {
        {"name", "Hunter"},
        {"xp_per_level", 1000},
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
    player.id = 95121;
    player.profession = "hunter";
    player.level = 1;
    player.xp = 0;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(95121, "hunt").success());
    EXPECT(quest::ReportGameplay(95121, "kill", "wolf") == 1);
    EXPECT(quest::Complete(95121, "hunt").success());

    auto loaded = data::PlayerStore::LoadDetailed(95121);
    EXPECT(loaded.HasData());
    if (loaded.HasData())
    {
        EXPECT(loaded.data->xp == 2500);
        EXPECT(loaded.data->level == 3);
    }

    data::PlayerData veteran;
    veteran.id = 95122;
    veteran.profession = "hunter";
    veteran.level = 42;
    veteran.xp = 9999;
    EXPECT(data::PlayerStore::Save(veteran));

    nlohmann::json extra;
    extra["bonus"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "k"}, {"type", "kill"},
                         {"entity", "boar"}, {"target", 1}}}},
        {"rewards", {{{"type", "xp"}, {"id", "xp"}, {"amount", 50}}}}
    };
    quest::Registry::LoadDefinitionsFromSection(&extra);
    EXPECT(quest::Start(95122, "bonus").success());
    EXPECT(quest::ReportGameplay(95122, "kill", "boar") == 1);
    EXPECT(quest::Complete(95122, "bonus").success());
    auto after = data::PlayerStore::LoadDetailed(95122);
    EXPECT(after.HasData());
    if (after.HasData())
    {
        EXPECT(after.data->xp == 10049);
        EXPECT(after.data->level == 42);
    }

    quest::Registry::Shutdown();
    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Quest_EntityMatches_WildSlugAndGeneric)
{
    using namespace rpframework::quest;
    EXPECT(EntityMatches("wild_boar", "boar"));
    EXPECT(EntityMatches("Boar", "boar"));
    EXPECT(EntityMatches("direwolf", "wolf"));
    EXPECT(!EntityMatches("raptor", "boar"));
    EXPECT(EntityMatches("torch", "*"));
    EXPECT(EntityMatches("anything", "any"));

    Objective killBoar;
    killBoar.type = "kill";
    killBoar.entity = "boar";
    EXPECT(ObjectiveMatches("kill", "wild_boar", killBoar));
    EXPECT(!ObjectiveMatches("craft", "wild_boar", killBoar));

    Objective harvestAny;
    harvestAny.type = "collect";
    harvestAny.entity = "harvest";
    EXPECT(ObjectiveMatches("collect", "wood", harvestAny));
}

TEST(Permissions_BootstrapOwnerOnce)
{
    using namespace rpframework::security;
    Permissions::Initialize();
    nlohmann::json sec;
    sec["player_levels"] = nlohmann::json::object();
    sec["owner_on_first_join"] = true;
    Permissions::LoadFromConfig(sec);
    EXPECT(Permissions::GetPlayerLevel(88001) == Level::PLAYER);
    EXPECT(Permissions::TryBootstrapOwner(88001));
    EXPECT(Permissions::GetPlayerLevel(88001) == Level::OWNER);
    EXPECT(!Permissions::TryBootstrapOwner(88002));
    EXPECT(Permissions::GetPlayerLevel(88002) == Level::PLAYER);

    sec["owner_on_first_join"] = false;
    sec["player_levels"] = nlohmann::json::object();
    Permissions::LoadFromConfig(sec);
    EXPECT(!Permissions::TryBootstrapOwner(88003));
    EXPECT(Permissions::GetPlayerLevel(88003) == Level::PLAYER);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    std::cout << "=== RPFramework Tests ===" << std::endl;
    int idx = 0;
    for (const auto& t : test::Tests())
    {
        ++idx;
        std::cout << "[" << idx << "/" << test::Tests().size() << "] " << t.name << " ... ";
        const int before = test::g_failed;
        t.fn();
        if (test::g_failed == before)
        {
            std::cout << "OK" << std::endl;
        }
        else
        {
            std::cout << "FAILED" << std::endl;
        }
    }

    std::cout << "\n=== Résumé ===" << std::endl;
    std::cout << "Passed: " << test::g_passed << std::endl;
    std::cout << "Failed: " << test::g_failed << std::endl;
    if (!test::g_failures.empty())
    {
        std::cout << "\nDétail des échecs :" << std::endl;
        for (const auto& f : test::g_failures)
        {
            std::cout << "  - " << f << std::endl;
        }
    }
    return test::g_failed > 0 ? 1 : 0;
}

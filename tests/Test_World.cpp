// ============================================================================
// RPFramework - Tests E2 / E3 : crimes, accès, hostilité, canal rpf
// ============================================================================
#include "TestHarness.h"

#include "Core/Config.h"
#include "Character/Registry.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Faction/Registry.h"
#include "Faction/Reputation.h"
#include "Faction/World.h"
#include "Mod/Bridge.h"
#include "Progression/Professions.h"
#include "Quest/Engine.h"
#include "Quest/Registry.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace rpframework;

namespace
{
    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_e2_" + tag + "_" + std::to_string(std::rand()));
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
        quest::Registry::Shutdown();
        faction::ResetForTests();
        faction::Registry::Shutdown();
        security::AuditLog::Shutdown();
        data::PlayerStore::Shutdown();
        mod::ResetForTests();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    nlohmann::json SampleReputation()
    {
        return nlohmann::json::parse(R"({
            "tiers": [
                { "id": "hated", "name": "Hai", "min": -2147483648, "max": -61,
                  "buy_mult": 1.0, "sell_mult": 1.0, "can_trade": false,
                  "attack_on_sight": true },
                { "id": "hostile", "name": "Hostile", "min": -60, "max": -31,
                  "buy_mult": 1.0, "sell_mult": 1.0, "can_trade": false,
                  "attack_on_sight": false },
                { "id": "unfriendly", "name": "Inamical", "min": -30, "max": -11,
                  "buy_mult": 1.25, "sell_mult": 0.80, "can_trade": true },
                { "id": "neutral", "name": "Neutre", "min": -10, "max": 10,
                  "buy_mult": 1.0, "sell_mult": 1.0, "can_trade": true },
                { "id": "friendly", "name": "Amical", "min": 11, "max": 30,
                  "buy_mult": 0.90, "sell_mult": 1.10, "can_trade": true }
            ]
        })");
    }

    void LoadWorld()
    {
        nlohmann::json factions;
        factions["town"] = {{"name", "La Ville"}};
        faction::Registry::ResetForTests();
        faction::Registry::LoadDefinitionsFromSection(&factions);
        const auto reputation = SampleReputation();
        faction::Registry::LoadReputationFromSection(&reputation);

        nlohmann::json crimes;
        crimes["theft"] = {
            {"faction", "town"},
            {"delta", -40},
            {"require_witness", true},
            {"unseen_profession", "thief"},
            {"unseen_xp", 8}
        };
        nlohmann::json locations;
        locations["town_gates"] = {
            {"faction", "town"},
            {"denied_tiers", {"hated", "hostile"}}
        };
        locations["noble_quarter"] = {
            {"faction", "town"},
            {"min_standing", "friendly"}
        };
        faction::ResetForTests();
        faction::LoadCrimesFromSection(&crimes);
        faction::LoadLocationsFromSection(&locations);
    }
}

TEST(E2_ReportCrime_UnseenDoesNotStain)
{
    const auto dir = MakeTempPlayerDir("unseen");
    ConfigurePlayerStore(dir);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();
    LoadWorld();

    nlohmann::json professions;
    professions["thief"] = {{"name", "Voleur"}, {"xp_per_level", 100}, {"max_level", 10}};
    nlohmann::json character;
    character["professions"] = professions;
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&character);

    data::PlayerData p;
    p.id = 77001;
    p.reputation["town"] = 10;
    EXPECT(data::PlayerStore::Save(p));

    const auto unseen = faction::ReportCrime(77001, "theft", false);
    EXPECT(unseen.ok);
    EXPECT(!unseen.reputationApplied);
    EXPECT(faction::GetReputation(77001, "town") == 10);

    bool sawUnseen = false;
    for (const auto& e : security::AuditLog::Recent(20))
    {
        if (e.action == "faction.crime.unseen" && e.playerId == 77001)
            sawUnseen = true;
    }
    EXPECT(sawUnseen);

    character::Registry::Shutdown();
    CleanupAll(dir);
}

TEST(E2_ReportCrime_WitnessedAppliesDelta)
{
    const auto dir = MakeTempPlayerDir("seen");
    ConfigurePlayerStore(dir);
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();
    LoadWorld();

    data::PlayerData p;
    p.id = 77002;
    p.reputation["town"] = 10;
    EXPECT(data::PlayerStore::Save(p));

    const auto seen = faction::ReportCrime(77002, "theft", true);
    EXPECT(seen.ok);
    EXPECT(seen.reputationApplied);
    EXPECT(faction::GetReputation(77002, "town") == -30);

    bool sawSeen = false;
    for (const auto& e : security::AuditLog::Recent(20))
    {
        if (e.action == "faction.crime.seen" && e.playerId == 77002)
            sawSeen = true;
    }
    EXPECT(sawSeen);

    CleanupAll(dir);
}

TEST(E3_CanEnter_AndIsHostileTo)
{
    const auto dir = MakeTempPlayerDir("access");
    ConfigurePlayerStore(dir);
    LoadWorld();

    data::PlayerData p;
    p.id = 77003;
    p.reputation["town"] = 0;
    EXPECT(data::PlayerStore::Save(p));

    const auto gates = faction::CanEnter(77003, "town_gates");
    EXPECT(gates.allowed);
    EXPECT(gates.standingId == "neutral");

    const auto unknown = faction::CanEnter(77003, "missing_place");
    EXPECT(unknown.allowed);

    EXPECT(!faction::IsHostileTo(77003, "town"));

    EXPECT(faction::SetReputation(77003, "town", -80, "test") == -80);
    const auto hatedGates = faction::CanEnter(77003, "town_gates");
    EXPECT(!hatedGates.allowed);
    EXPECT(hatedGates.standingId == "hated");
    EXPECT(faction::IsHostileTo(77003, "town"));

    const auto quarter = faction::CanEnter(77003, "noble_quarter");
    EXPECT(!quarter.allowed);

    CleanupAll(dir);
}

TEST(E2E3_RpfBridge_Player)
{
    const auto dir = MakeTempPlayerDir("rpf");
    ConfigurePlayerStore(dir);
    mod::ResetForTests();
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();
    LoadWorld();

    data::PlayerData p;
    p.id = 77004;
    p.reputation["town"] = 10;
    EXPECT(data::PlayerStore::Save(p));

    const auto unseen = mod::Execute(77004, "rpf crime report theft 0");
    EXPECT(unseen.handled);
    EXPECT(unseen.success);
    EXPECT(faction::GetReputation(77004, "town") == 10);

    const auto seen = mod::Execute(77004, "rpf crime report theft 1");
    EXPECT(seen.success);
    EXPECT(faction::GetReputation(77004, "town") == -30);

    const auto enter = mod::Execute(77004, "rpf location canenter town_gates");
    EXPECT(enter.success);
    EXPECT(enter.message.find("allowed") != std::string::npos);

    EXPECT(faction::SetReputation(77004, "town", -80, "test") == -80);
    const auto denied = mod::Execute(77004, "rpf location canenter town_gates");
    EXPECT(!denied.success);
    EXPECT(denied.message.find("denied") != std::string::npos);

    const auto hostile = mod::Execute(77004, "rpf npc hostile town");
    EXPECT(hostile.success);
    EXPECT(hostile.message == "hostile");

    CleanupAll(dir);
}

// ============================================================================
// RPFramework - Tests standing / relations / prix (chantier E1)
// ============================================================================
#include "TestHarness.h"

#include "Core/Config.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Economy/Merchant.h"
#include "Economy/Registry.h"
#include "Economy/Wallet.h"
#include "Faction/Registry.h"
#include "Faction/Reputation.h"
#include "Quest/Commands.h"
#include "Quest/Engine.h"
#include "Quest/Registry.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>

using namespace rpframework;

namespace
{
    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_e1_" + tag + "_" + std::to_string(std::rand()));
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

    void CleanupPlayerStore(const std::filesystem::path& dir)
    {
        data::PlayerStore::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    nlohmann::json SampleReputation()
    {
        return nlohmann::json::parse(R"({
            "tiers": [
                { "id": "hated", "name": "Hai", "min": -2147483648, "max": -61,
                  "buy_mult": 1.0, "sell_mult": 1.0, "can_trade": false },
                { "id": "hostile", "name": "Hostile", "min": -60, "max": -31,
                  "buy_mult": 1.0, "sell_mult": 1.0, "can_trade": false },
                { "id": "unfriendly", "name": "Inamical", "min": -30, "max": -11,
                  "buy_mult": 1.25, "sell_mult": 0.80, "can_trade": true },
                { "id": "neutral", "name": "Neutre", "min": -10, "max": 10,
                  "buy_mult": 1.0, "sell_mult": 1.0, "can_trade": true },
                { "id": "friendly", "name": "Amical", "min": 11, "max": 30,
                  "buy_mult": 0.90, "sell_mult": 1.10, "can_trade": true },
                { "id": "honored", "name": "Honore", "min": 31, "max": 60,
                  "buy_mult": 0.80, "sell_mult": 1.20, "can_trade": true },
                { "id": "exalted", "name": "Exalte", "min": 61, "max": 2147483647,
                  "buy_mult": 0.70, "sell_mult": 1.30, "can_trade": true }
            ],
            "relation_effects": {
                "ally": { "share_percent": 50 },
                "friendly": { "share_percent": 25 },
                "neutral": { "share_percent": 0 },
                "unfriendly": { "share_percent": 0 },
                "hostile": { "share_percent": -25 },
                "at_war": { "share_percent": -100 }
            }
        })");
    }

    nlohmann::json SampleFactions()
    {
        nlohmann::json factions;
        factions["town"] = {
            {"name", "La Ville"},
            {"relations", {{"thieves_guild", "at_war"}, {"nobles", "ally"}}}
        };
        factions["thieves_guild"] = {
            {"name", "Voleurs"},
            {"relations", {{"town", "at_war"}}}
        };
        factions["nobles"] = {
            {"name", "Nobles"},
            {"relations", {{"town", "ally"}}}
        };
        return factions;
    }

    void LoadWorld()
    {
        faction::Registry::ResetForTests();
        const auto rep = SampleReputation();
        faction::Registry::LoadReputationFromSection(&rep);
        const auto factions = SampleFactions();
        faction::Registry::LoadDefinitionsFromSection(&factions);
    }
}

TEST(E1_ResolveStanding_Bornes)
{
    LoadWorld();
    const auto hated = faction::ResolveStanding(-100);
    EXPECT(hated.has_value() && hated->id == "hated");
    const auto neutral = faction::ResolveStanding(0);
    EXPECT(neutral.has_value() && neutral->id == "neutral");
    const auto honored = faction::ResolveStanding(42);
    EXPECT(honored.has_value() && honored->id == "honored");
    const auto exalted = faction::ResolveStanding(999);
    EXPECT(exalted.has_value() && exalted->id == "exalted");
    faction::Registry::ResetForTests();
}

TEST(E1_StandingOverlap_RejeteAuLoad)
{
    faction::Registry::ResetForTests();
    nlohmann::json section;
    section["tiers"] = nlohmann::json::array({
        {{"id", "a"}, {"min", 0}, {"max", 20}},
        {{"id", "b"}, {"min", 10}, {"max", 30}}
    });
    faction::Registry::LoadReputationFromSection(&section);
    EXPECT(faction::Registry::ListStandings().size() == 1);
    EXPECT(faction::Registry::ListStandings()[0].id == "a");
    const auto mid = faction::ResolveStanding(15);
    EXPECT(mid.has_value() && mid->id == "a");
    faction::Registry::ResetForTests();
}

TEST(E1_ApplySansPropagate_UneFaction)
{
    LoadWorld();
    data::PlayerData data;
    data.id = 96101;
    auto r = faction::ApplyReputationDelta(data, "thieves_guild", 40, false);
    EXPECT(r.ok);
    EXPECT(r.changes.size() == 1);
    EXPECT(data.reputation["thieves_guild"] == 40);
    EXPECT(data.reputation.count("town") == 0);
    faction::Registry::ResetForTests();
}

TEST(E1_AtWar_UnSaut_PasDeRebond)
{
    LoadWorld();
    data::PlayerData data;
    data.id = 96102;
    auto r = faction::ApplyReputationDelta(data, "thieves_guild", 40, true);
    EXPECT(r.ok);
    EXPECT(r.changes.size() == 2);
    EXPECT(data.reputation["thieves_guild"] == 40);
    EXPECT(data.reputation["town"] == -40);
    // Pas de rebond : nobles n'est allié que de town, le secondaire ne fuit pas.
    EXPECT(data.reputation.count("nobles") == 0);
    faction::Registry::ResetForTests();
}

TEST(E1_Ally_ShareVersZero)
{
    LoadWorld();
    data::PlayerData data;
    data.id = 96103;
    auto r = faction::ApplyReputationDelta(data, "town", 15, true);
    EXPECT(r.ok);
    EXPECT(data.reputation["town"] == 15);
    // ally 50% : 15/2 = 7 (vers 0). at_war thieves : 15 * -100% = -15.
    EXPECT(data.reputation["nobles"] == 7);
    EXPECT(data.reputation["thieves_guild"] == -15);
    faction::Registry::ResetForTests();
}

TEST(E1_SetReputation_NePropagePas)
{
    const auto dir = MakeTempPlayerDir("set_noprop");
    ConfigurePlayerStore(dir);
    LoadWorld();
    security::AuditLog::Initialize();
    data::PlayerData player;
    player.id = 96104;
    EXPECT(data::PlayerStore::Save(player));
    const int after = faction::SetReputation(96104, "thieves_guild", 40, "admin");
    EXPECT(after == 40);
    EXPECT(faction::GetReputation(96104, "thieves_guild") == 40);
    EXPECT(faction::GetReputation(96104, "town") == 0);
    CleanupPlayerStore(dir);
    faction::Registry::ResetForTests();
}

TEST(E1_QuestReward_PropageGuerre)
{
    const auto dir = MakeTempPlayerDir("quest_war");
    ConfigurePlayerStore(dir);
    LoadWorld();
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json section;
    section["help_thieves"] = {
        {"auto_complete", false},
        {"objectives", {{{"id", "visit"}, {"type", "visit"},
                         {"entity", "den"}, {"target", 1}}}},
        {"rewards", {{{"type", "reputation"}, {"id", "thieves_guild"}, {"amount", 40}}}}
    };
    quest::Registry::ResetForTests();
    quest::Registry::LoadDefinitionsFromSection(&section);

    data::PlayerData player;
    player.id = 96105;
    EXPECT(data::PlayerStore::Save(player));
    EXPECT(quest::Start(96105, "help_thieves").success());
    EXPECT(quest::AddProgress(96105, "help_thieves", "visit", "den").success());
    EXPECT(quest::Complete(96105, "help_thieves").success());

    EXPECT(faction::GetReputation(96105, "thieves_guild") == 40);
    EXPECT(faction::GetReputation(96105, "town") == -40);

    quest::Registry::Shutdown();
    CleanupPlayerStore(dir);
    faction::Registry::ResetForTests();
}

TEST(E1_Merchant_StandingPrixEtRefus)
{
    const auto dir = MakeTempPlayerDir("merchant_stand");
    ConfigurePlayerStore(dir);
    LoadWorld();
    security::Permissions::Initialize();
    security::Permissions::Register("economy.merchant", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json currencies;
    currencies["gold"] = {{"name", "Or"}, {"max_balance", 0}, {"transferable", true}};
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&currencies);

    nlohmann::json merchants;
    merchants["town_blacksmith"] = {
        {"name", "Forgeron"},
        {"currency", "gold"},
        {"faction", "town"},
        {"conditions", {{"min_level", 1}}},
        {"sells", {{{"id", "torch"}, {"blueprint",
            "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponTorch.PrimalItem_WeaponTorch"},
            {"price", 40}, {"stock", 0}}}}
    };
    economy::Merchant::ResetForTests();
    economy::Merchant::LoadDefinitionsFromSection(&merchants);

    data::PlayerData unfriendly;
    unfriendly.id = 96106;
    unfriendly.level = 1;
    unfriendly.reputation["town"] = -20;
    EXPECT(data::PlayerStore::Save(unfriendly));
    security::RateLimiter::Reset(96106, "economy.add");
    security::RateLimiter::Reset(96106, "economy.merchant");
    EXPECT(economy::Add(96106, "gold", 100, "init", "test").status == economy::TxStatus::Success);
    const auto buy = economy::Merchant::Buy(96106, "town_blacksmith", "torch", 1);
    EXPECT(buy.status == economy::TxStatus::Success);
    EXPECT(buy.newBalance == 50); // 40 * 1.25 = 50

    data::PlayerData hated;
    hated.id = 96107;
    hated.level = 1;
    hated.reputation["town"] = -80;
    EXPECT(data::PlayerStore::Save(hated));
    security::RateLimiter::Reset(96107, "economy.add");
    security::RateLimiter::Reset(96107, "economy.merchant");
    EXPECT(economy::Add(96107, "gold", 100, "init", "test").status == economy::TxStatus::Success);
    const auto refused = economy::Merchant::Buy(96107, "town_blacksmith", "torch", 1);
    EXPECT(refused.status == economy::TxStatus::PermissionDenied);
    EXPECT(economy::GetBalance(96107, "gold") == 100);

    economy::Merchant::ResetForTests();
    economy::Registry::ResetForTests();
    CleanupPlayerStore(dir);
    faction::Registry::ResetForTests();
}

TEST(E1_Merchant_SansFaction_PrixInchange)
{
    const auto dir = MakeTempPlayerDir("merchant_nofac");
    ConfigurePlayerStore(dir);
    LoadWorld();
    security::Permissions::Initialize();
    security::Permissions::Register("economy.merchant", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json currencies;
    currencies["gold"] = {{"name", "Or"}, {"max_balance", 0}, {"transferable", true}};
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&currencies);

    nlohmann::json merchants;
    merchants["independent"] = {
        {"name", "Colporteur"},
        {"currency", "gold"},
        {"conditions", {{"min_level", 1}}},
        {"sells", {{{"id", "torch"}, {"blueprint",
            "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponTorch.PrimalItem_WeaponTorch"},
            {"price", 40}, {"stock", 0}}}}
    };
    economy::Merchant::ResetForTests();
    economy::Merchant::LoadDefinitionsFromSection(&merchants);

    data::PlayerData hated;
    hated.id = 96108;
    hated.level = 1;
    hated.reputation["town"] = -80;
    EXPECT(data::PlayerStore::Save(hated));
    security::RateLimiter::Reset(96108, "economy.add");
    security::RateLimiter::Reset(96108, "economy.merchant");
    EXPECT(economy::Add(96108, "gold", 100, "init", "test").status == economy::TxStatus::Success);
    const auto buy = economy::Merchant::Buy(96108, "independent", "torch", 1);
    EXPECT(buy.status == economy::TxStatus::Success);
    EXPECT(buy.newBalance == 60);

    economy::Merchant::ResetForTests();
    economy::Registry::ResetForTests();
    CleanupPlayerStore(dir);
    faction::Registry::ResetForTests();
}

TEST(E1_Overflow_AucuneEcriture)
{
    LoadWorld();
    data::PlayerData data;
    data.id = 96109;
    data.reputation["thieves_guild"] = std::numeric_limits<int>::max();
    data.reputation["town"] = 10;
    auto r = faction::ApplyReputationDelta(data, "thieves_guild", 1, true);
    EXPECT(!r.ok);
    EXPECT(r.changes.empty());
    EXPECT(data.reputation["thieves_guild"] == std::numeric_limits<int>::max());
    EXPECT(data.reputation["town"] == 10);
    faction::Registry::ResetForTests();
}

TEST(E1_RelationCibleInconnue_Skip)
{
    faction::Registry::ResetForTests();
    const auto rep = SampleReputation();
    faction::Registry::LoadReputationFromSection(&rep);
    nlohmann::json factions;
    factions["town"] = {
        {"name", "Ville"},
        {"relations", {{"ghosts", "at_war"}, {"thieves_guild", "at_war"}}}
    };
    factions["thieves_guild"] = {{"name", "Voleurs"}};
    faction::Registry::LoadDefinitionsFromSection(&factions);
    const auto town = faction::Registry::GetFaction("town");
    EXPECT(town.has_value());
    EXPECT(town->relations.count("ghosts") == 0);
    EXPECT(town->relations.count("thieves_guild") == 1);
    EXPECT(faction::Registry::HasFaction("town"));
    faction::Registry::ResetForTests();
}

TEST(E1_ChatReputation_Handled)
{
    const auto dir = MakeTempPlayerDir("chat_rep");
    ConfigurePlayerStore(dir);
    LoadWorld();
    security::Permissions::Initialize();
    data::PlayerData player;
    player.id = 96110;
    player.reputation["town"] = 42;
    EXPECT(data::PlayerStore::Save(player));
    const auto rep = quest::HandleCommand(96110, {"reputation", "town"});
    EXPECT(rep.handled);
    EXPECT(rep.success);
    EXPECT(rep.message.find("42") != std::string::npos);
    EXPECT(rep.message.find("Honore") != std::string::npos);
    CleanupPlayerStore(dir);
    faction::Registry::ResetForTests();
}

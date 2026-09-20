// ============================================================================
// RPFramework - Tests de durcissement (audit NEXT STEP)
// ============================================================================
#include "TestHarness.h"

#include "Asa/BlueprintPath.h"
#include "Character/Registry.h"
#include "Character/Stats.h"
#include "Core/Config.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Economy/Merchant.h"
#include "Economy/Registry.h"
#include "Economy/Wallet.h"
#include "Faction/Join.h"
#include "Faction/Registry.h"
#include "Loadout/AsaDeliver.h"
#include "Loadout/Distribute.h"
#include "Mod/Bridge.h"
#include "Quest/Commands.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace rpframework;

namespace
{
    constexpr const char* kMeatBp =
        "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_CookedMeat.PrimalItemConsumable_CookedMeat";
    constexpr const char* kTorchBp =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponTorch.PrimalItem_WeaponTorch";
    constexpr const char* kSwordBp =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword";
    constexpr const char* kNoteBp =
        "/Game/Notes/PrimalItem_Note.PrimalItem_Note";

    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_hard_" + tag + "_" + std::to_string(std::rand()));
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
        loadout::ClearTestInventory();
        data::PlayerStore::Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
}

TEST(Hardening_SkillApplyRejected)
{
    security::Permissions::Initialize();
    const auto denied = quest::HandleCommand(99001, {"skill", "apply", "focus"},
                                             security::Level::PLAYER);
    EXPECT(denied.handled);
    EXPECT(!denied.success);
    EXPECT(denied.message.find("list|unlock") != std::string::npos);
}

TEST(Hardening_RpfModRejectsMutations)
{
    mod::ResetForTests();
    security::Permissions::Initialize();
    core::Config::Get().Set("debug", true);

    const auto set = mod::Execute(99002, "rpf mod set debug false");
    EXPECT(set.handled);
    EXPECT(!set.success);
    EXPECT(set.message.find("rpf mod get") != std::string::npos);

    const auto select = mod::Execute(99002, "rpf mod select character.races.human");
    EXPECT(select.handled);
    EXPECT(!select.success);

    mod::ResetForTests();
}

TEST(Hardening_FoldFromBase_EmptyModsKeepsBaseline)
{
    const std::vector<character::StatModifier> none;
    EXPECT(character::FoldFromBase(100.0f, none, "health") == 100.0f);

    bool sawHealth = false;
    for (const auto target : character::RpgStatTargets())
    {
        if (target == "health") sawHealth = true;
    }
    EXPECT(sawHealth);
}

TEST(Hardening_FoldFromBase_AddAndMultiply)
{
    std::vector<character::StatModifier> mods;
    character::StatModifier add;
    add.target = "health";
    add.op = character::StatModifier::Op::Add;
    add.value = 10.0f;
    mods.push_back(add);

    character::StatModifier mul;
    mul.target = "health";
    mul.op = character::StatModifier::Op::Multiply;
    mul.value = 1.1f;
    mods.push_back(mul);

    EXPECT(character::FoldFromBase(100.0f, mods, "health") == (100.0f + 10.0f) * 1.1f);
    EXPECT(character::FoldFromBase(100.0f, mods, "stamina") == 100.0f);
}

TEST(Hardening_KitCrashWindow_PartialGiveKeepsOutbox)
{
    const auto dir = MakeTempPlayerDir("kit_partial");
    ConfigurePlayerStore(dir);
    loadout::ClearTestInventory();

    nlohmann::json section;
    section["classes_enabled"] = false;
    section["races"]["human"]["name"] = "Humain";
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);

    data::PlayerData p;
    p.id = 99010;
    p.race = "human";
    p.starterKitDelivered = false;
    p.pendingStarterKit.push_back(nlohmann::json{
        {"id", "meat"}, {"quantity", 2}, {"blueprint", kMeatBp}
    });
    p.pendingStarterKit.push_back(nlohmann::json{
        {"id", "torch"}, {"quantity", 1}, {"blueprint", kTorchBp}
    });
    EXPECT(data::PlayerStore::Save(p));

    loadout::BlockedGiveKeys().insert(asa::BlueprintKey(kTorchBp));
    const auto given = loadout::Distributor::GiveStarterKit(99010);
    EXPECT(given.status == loadout::DistributionStatus::Delivered);
    EXPECT(loadout::CountTestInventory(99010, kMeatBp) == 2);
    EXPECT(loadout::CountTestInventory(99010, kTorchBp) == 0);

    auto after = data::PlayerStore::LoadDetailed(99010);
    EXPECT(after.HasData());
    if (after.HasData())
    {
        EXPECT(!after.data->starterKitDelivered);
        EXPECT(after.data->pendingStarterKit.size() == 1);
        EXPECT(after.data->pendingStarterKit[0].value("id", std::string{}) == "torch");
    }

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Hardening_KitCrashWindow_ReentrantGiveIsOnce)
{
    const auto dir = MakeTempPlayerDir("kit_reenter");
    ConfigurePlayerStore(dir);
    loadout::ClearTestInventory();

    nlohmann::json section;
    section["classes_enabled"] = false;
    section["races"]["human"]["name"] = "Humain";
    character::Registry::ResetForTests();
    character::Registry::LoadDefinitionsFromSection(&section);

    data::PlayerData p;
    p.id = 99011;
    p.race = "human";
    p.starterKitDelivered = false;
    p.pendingStarterKit.push_back(nlohmann::json{
        {"id", "meat"}, {"quantity", 1}, {"blueprint", kMeatBp}
    });
    EXPECT(data::PlayerStore::Save(p));

    loadout::OnTestGiveItems() = [](security::PlayerId player, const loadout::Item&) {
        loadout::Distributor::GiveStarterKit(player);
    };
    const auto given = loadout::Distributor::GiveStarterKit(99011);
    EXPECT(given.status == loadout::DistributionStatus::Delivered);
    EXPECT(loadout::CountTestInventory(99011, kMeatBp) == 1);

    auto after = data::PlayerStore::LoadDetailed(99011);
    EXPECT(after.HasData());
    if (after.HasData())
    {
        EXPECT(after.data->starterKitDelivered);
        EXPECT(after.data->pendingStarterKit.empty());
    }

    character::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Hardening_MerchantBuyRefundsWhenGiveFails)
{
    const auto dir = MakeTempPlayerDir("buy_refund");
    ConfigurePlayerStore(dir);
    loadout::ClearTestInventory();
    security::Permissions::Initialize();
    security::Permissions::Register("economy.merchant", security::Level::PLAYER);
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json currencies;
    currencies["gold"] = {
        {"name", "Pieces d'or"}, {"symbol", "g"},
        {"max_balance", 0}, {"transferable", true}
    };
    nlohmann::json merchants;
    merchants["town_blacksmith"] = nlohmann::json::parse(R"({
        "name": "Forgeron",
        "currency": "gold",
        "sells": [{
            "id": "sword",
            "blueprint": "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword",
            "price": 120,
            "stock": 5
        }]
    })");
    economy::Registry::ResetForTests();
    economy::Registry::LoadDefinitionsFromSection(&currencies);
    economy::Merchant::ResetForTests();
    economy::Merchant::LoadDefinitionsFromSection(&merchants);

    data::PlayerData p;
    p.id = 99020;
    p.level = 1;
    EXPECT(data::PlayerStore::Save(p));
    security::RateLimiter::Reset(99020, "economy.add");
    EXPECT(economy::Add(99020, "gold", 500, "init", "test").status == economy::TxStatus::Success);

    loadout::BlockedGiveKeys().insert(asa::BlueprintKey(kSwordBp));
    const auto r = economy::Merchant::Buy(99020, "town_blacksmith", "sword", 1);
    EXPECT(r.status == economy::TxStatus::InsufficientItems);
    EXPECT(economy::GetBalance(99020, "gold") == 500);
    EXPECT(loadout::CountTestInventory(99020, kSwordBp) == 0);

    const auto info = economy::Merchant::Get("town_blacksmith");
    EXPECT(info.has_value());
    if (info)
    {
        for (const auto& listing : info->sells)
        {
            if (listing.id == "sword")
                EXPECT(listing.stock == 5);
        }
    }

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Hardening_JournalFlagOnlyAfterGive)
{
    const auto dir = MakeTempPlayerDir("journal_after");
    ConfigurePlayerStore(dir);
    loadout::ClearTestInventory();
    security::Permissions::Initialize();
    security::RateLimiter::Initialize();
    security::AuditLog::Initialize();

    nlohmann::json factions;
    factions["town"] = {
        {"name", "La Ville"},
        {"initial_reputation", 10},
        {"journal", {
            {"id", "quest_journal"},
            {"quantity", 1},
            {"blueprint", kNoteBp}
        }}
    };
    faction::Registry::ResetForTests();
    faction::Registry::LoadDefinitionsFromSection(&factions);

    data::PlayerData p;
    p.id = 99030;
    p.race = "human";
    p.level = 1;
    EXPECT(data::PlayerStore::Save(p));
    security::RateLimiter::Reset(99030, "faction.join");

    loadout::BlockedGiveKeys().insert(asa::BlueprintKey(kNoteBp));
    const auto r = faction::Join(99030, "town");
    EXPECT(r.status == faction::JoinStatus::Success);
    auto blocked = data::PlayerStore::LoadDetailed(99030);
    EXPECT(blocked.HasData());
    if (blocked.HasData())
    {
        bool hasJournal = false;
        bool hasPending = false;
        for (const auto& unlock : blocked.data->unlocks)
        {
            if (unlock == "journal:town") hasJournal = true;
            if (unlock == "journal-pending:town") hasPending = true;
        }
        EXPECT(!hasJournal);
        EXPECT(hasPending);
    }
    EXPECT(loadout::CountTestInventory(99030, kNoteBp) == 0);
    EXPECT(r.message.find("journal donne") == std::string::npos);

    loadout::BlockedGiveKeys().clear();
    faction::OnJoined(99030, "town");
    auto given = data::PlayerStore::LoadDetailed(99030);
    EXPECT(given.HasData());
    if (given.HasData())
    {
        bool hasJournal = false;
        bool hasPending = false;
        for (const auto& unlock : given.data->unlocks)
        {
            if (unlock == "journal:town") hasJournal = true;
            if (unlock == "journal-pending:town") hasPending = true;
        }
        EXPECT(hasJournal);
        EXPECT(!hasPending);
    }
    EXPECT(loadout::CountTestInventory(99030, kNoteBp) == 1);

    faction::Registry::Shutdown();
    security::AuditLog::Shutdown();
    CleanupPlayerStore(dir);
}

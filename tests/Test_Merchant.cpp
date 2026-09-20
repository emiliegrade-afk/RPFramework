// ============================================================================
// RPFramework - Tests marchands (chantier B2)
//
// Couvre : chargement data-driven, rejet des entrées invalides, lookup
// blueprint (NormalizeBlueprintPath / BlueprintKey), transaction atomique
// via Wallet, conditions, permission, commande /marchand, audit.
// ============================================================================
#include "TestHarness.h"

#include "Asa/BlueprintPath.h"
#include "Core/Config.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Economy/Merchant.h"
#include "Economy/Registry.h"
#include "Economy/Wallet.h"
#include "Loadout/AsaDeliver.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace rpframework;
using rpframework::asa::BlueprintKey;
using rpframework::asa::NormalizeBlueprintPath;

namespace
{
    constexpr const char* kSwordBp =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword";
    constexpr const char* kTorchBp =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponTorch.PrimalItem_WeaponTorch";
    constexpr const char* kMeatBp =
        "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_CookedMeat.PrimalItemConsumable_CookedMeat";
    constexpr const char* kIngotBp =
        "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot";

    std::filesystem::path MakeTempPlayerDir(const std::string& tag)
    {
        const auto dir = std::filesystem::temp_directory_path() / "rpframework_tests"
            / ("players_b2_" + tag + "_" + std::to_string(std::rand()));
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

    nlohmann::json ValidCurrencies()
    {
        nlohmann::json currencies;
        currencies["gold"] = {
            {"name", "Pieces d'or"},
            {"symbol", "g"},
            {"max_balance", 0},
            {"transferable", true}
        };
        currencies["gem"] = {
            {"name", "Gemmes"},
            {"max_balance", 50},
            {"transferable", true}
        };
        return currencies;
    }

    nlohmann::json ValidMerchants()
    {
        nlohmann::json merchants;
        merchants["town_blacksmith"] = nlohmann::json::parse(R"({
            "name": "Forgeron de la ville",
            "currency": "gold",
            "conditions": { "min_level": 1 },
            "sells": [
                {
                    "id": "torch",
                    "blueprint": "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponTorch.PrimalItem_WeaponTorch",
                    "price": 15,
                    "stock": 0
                },
                {
                    "id": "sword",
                    "blueprint": "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword",
                    "price": 120,
                    "stock": 5
                }
            ],
            "buys": [
                {
                    "id": "cooked_meat",
                    "blueprint": "/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_CookedMeat.PrimalItemConsumable_CookedMeat",
                    "price": 5
                },
                {
                    "id": "metal_ingot",
                    "blueprint": "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot",
                    "price": 30
                }
            ]
        })");
        return merchants;
    }

    void LoadEconomyAndMerchants(const nlohmann::json& merchants = ValidMerchants())
    {
        const auto currencies = ValidCurrencies();
        economy::Registry::ResetForTests();
        economy::Registry::LoadDefinitionsFromSection(&currencies);
        economy::Merchant::ResetForTests();
        economy::Merchant::LoadDefinitionsFromSection(&merchants);
    }

    void PrepareSecurity()
    {
        security::Permissions::Initialize();
        security::Permissions::Register("economy.merchant", security::Level::PLAYER);
        security::RateLimiter::Initialize();
        security::AuditLog::Initialize();
    }

    data::PlayerData MakePlayer(security::PlayerId pid, int level = 1)
    {
        data::PlayerData p;
        p.id = pid;
        p.level = level;
        p.race = "human";
        p.profession = "blacksmith";
        return p;
    }

    bool SavePlayer(data::PlayerData player)
    {
        return data::PlayerStore::Save(player);
    }

    void Fund(security::PlayerId pid, int64_t gold)
    {
        security::RateLimiter::Reset(pid, "economy.add");
        security::RateLimiter::Reset(pid, "economy.subtract");
        security::RateLimiter::Reset(pid, "economy.merchant");
        const auto r = economy::Add(pid, "gold", gold, "init", "test");
        EXPECT(r.status == economy::TxStatus::Success);
    }

    bool HasAudit(std::string_view action, security::PlayerId pid)
    {
        for (const auto& e : security::AuditLog::Recent(50))
        {
            if (e.action == action && e.playerId == pid) return true;
        }
        return false;
    }
}

TEST(Merchant_LoadsFromMemoryJson)
{
    LoadEconomyAndMerchants();

    EXPECT(economy::Merchant::Has("town_blacksmith") == true);
    EXPECT(economy::Merchant::Has("missing") == false);

    const auto info = economy::Merchant::Get("town_blacksmith");
    EXPECT(info.has_value());
    if (info)
    {
        EXPECT(info->name == "Forgeron de la ville");
        EXPECT(info->currency == "gold");
        EXPECT(info->conditions.minLevel == 1);
        EXPECT(info->sells.size() == 2);
        EXPECT(info->buys.size() == 2);
        bool sawTorch = false;
        bool sawSword = false;
        for (const auto& listing : info->sells)
        {
            if (listing.id == "torch")
            {
                sawTorch = true;
                EXPECT(listing.unlimited == true);
                EXPECT(listing.price == 15);
                EXPECT(listing.blueprint == kTorchBp);
            }
            if (listing.id == "sword")
            {
                sawSword = true;
                EXPECT(listing.unlimited == false);
                EXPECT(listing.stock == 5);
                EXPECT(listing.price == 120);
                EXPECT(listing.blueprint == kSwordBp);
            }
        }
        EXPECT(sawTorch);
        EXPECT(sawSword);
    }

    const auto ids = economy::Merchant::ListIds();
    EXPECT(ids.size() == 1);
    EXPECT(!ids.empty() && ids.front() == "town_blacksmith");

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
}

TEST(Merchant_RejectsInvalidListingAndMerchant)
{
    auto merchants = ValidMerchants();
    merchants["town_blacksmith"]["sells"].push_back({
        {"id", "bad"},
        {"blueprint", ""},
        {"price", 10},
        {"stock", 1}
    });
    merchants["broken"] = {
        {"name", "Casse"},
        {"currency", ""},
        {"sells", nlohmann::json::array()},
        {"buys", nlohmann::json::array()}
    };
    merchants["unknown_money"] = {
        {"name", "Banquier"},
        {"currency", "bitcoin"},
        {"sells", nlohmann::json::array()},
        {"buys", nlohmann::json::array()}
    };

    LoadEconomyAndMerchants(merchants);

    EXPECT(economy::Merchant::Has("broken") == false);
    EXPECT(economy::Merchant::Has("unknown_money") == false);
    EXPECT(economy::Merchant::Has("town_blacksmith") == true);

    const auto info = economy::Merchant::Get("town_blacksmith");
    EXPECT(info.has_value());
    if (info)
    {
        EXPECT(info->sells.size() == 2);
        for (const auto& listing : info->sells)
            EXPECT(listing.id != "bad");
    }

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
}

TEST(Merchant_NormalizesBlueprintAndLookup)
{
    nlohmann::json merchants;
    merchants["shop"] = {
        {"name", "Boutique"},
        {"currency", "gold"},
        {"conditions", {{"min_level", 1}}},
        {"sells", nlohmann::json::array({
            nlohmann::json{
                {"blueprint", "Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword_C'"},
                {"price", 50},
                {"stock", 2}
            }
        })},
        {"buys", nlohmann::json::array()}
    };
    LoadEconomyAndMerchants(merchants);

    const auto info = economy::Merchant::Get("shop");
    EXPECT(info.has_value());
    if (info && !info->sells.empty())
    {
        EXPECT(info->sells[0].blueprint == kSwordBp);
        EXPECT(info->sells[0].id == "PrimalItem_WeaponSword");
        EXPECT(BlueprintKey(info->sells[0].blueprint) == BlueprintKey(kSwordBp));
        EXPECT(NormalizeBlueprintPath(
            "Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword_C'")
            == kSwordBp);
    }

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
}

TEST(Merchant_BuySuccessDecrementsFiniteStock)
{
    const auto dir = MakeTempPlayerDir("buy_ok");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96201;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 500);

    const auto r = economy::Merchant::Buy(pid, "town_blacksmith", "sword", 2);
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(economy::GetBalance(pid, "gold") == 260);

    const auto info = economy::Merchant::Get("town_blacksmith");
    EXPECT(info.has_value());
    if (info)
    {
        for (const auto& listing : info->sells)
        {
            if (listing.id == "sword")
            {
                EXPECT(listing.stock == 3);
                EXPECT(listing.unlimited == false);
            }
        }
    }

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_BuyUnlimitedStockStaysUnlimited)
{
    const auto dir = MakeTempPlayerDir("buy_unlimited");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96202;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 100);

    const auto r = economy::Merchant::Buy(pid, "town_blacksmith", "torch", 3);
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(economy::GetBalance(pid, "gold") == 55);

    const auto info = economy::Merchant::Get("town_blacksmith");
    EXPECT(info.has_value());
    if (info)
    {
        for (const auto& listing : info->sells)
        {
            if (listing.id == "torch")
            {
                EXPECT(listing.unlimited == true);
                EXPECT(listing.stock == 0);
            }
        }
    }

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_BuyInsufficientFundsIsAtomic)
{
    const auto dir = MakeTempPlayerDir("buy_broke");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96203;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 50);

    const auto r = economy::Merchant::Buy(pid, "town_blacksmith", "sword", 1);
    EXPECT(r.status == economy::TxStatus::InsufficientFunds);
    EXPECT(economy::GetBalance(pid, "gold") == 50);

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
    CleanupPlayerStore(dir);
}

TEST(Merchant_BuyOutOfStockAndUnknowns)
{
    const auto dir = MakeTempPlayerDir("buy_stock");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96204;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 5000);

    EXPECT(economy::Merchant::Buy(pid, "ghost", "sword", 1).status
        == economy::TxStatus::InvalidAmount);
    EXPECT(economy::Merchant::Buy(pid, "town_blacksmith", "unicorn", 1).status
        == economy::TxStatus::InvalidAmount);
    EXPECT(economy::Merchant::Buy(pid, "town_blacksmith", "sword", 0).status
        == economy::TxStatus::InvalidAmount);
    EXPECT(economy::Merchant::Buy(pid, "town_blacksmith", "sword", -2).status
        == economy::TxStatus::InvalidAmount);

    const auto drained = economy::Merchant::Buy(pid, "town_blacksmith", "sword", 5);
    EXPECT(drained.status == economy::TxStatus::Success);

    const auto empty = economy::Merchant::Buy(pid, "town_blacksmith", "sword", 1);
    EXPECT(empty.status == economy::TxStatus::InsufficientFunds);
    EXPECT(economy::GetBalance(pid, "gold") == 5000 - (120 * 5));

    const auto after = economy::Merchant::Get("town_blacksmith");
    EXPECT(after.has_value());
    if (after)
    {
        for (const auto& listing : after->sells)
        {
            if (listing.id == "sword")
            {
                EXPECT(listing.stock == 0);
                EXPECT(listing.unlimited == false);
            }
        }
    }

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_BuyByBlueprintKey)
{
    const auto dir = MakeTempPlayerDir("buy_bp");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96205;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 200);

    const auto wrapped =
        "Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponTorch.PrimalItem_WeaponTorch_C'";
    const auto r = economy::Merchant::Buy(pid, "town_blacksmith", wrapped, 1);
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(economy::GetBalance(pid, "gold") == 185);

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_BuyConditionNotMet)
{
    const auto dir = MakeTempPlayerDir("buy_cond");
    ConfigurePlayerStore(dir);
    PrepareSecurity();

    auto merchants = ValidMerchants();
    merchants["town_blacksmith"]["conditions"] = {
        {"min_level", 10},
        {"races", nlohmann::json::array({"human"})}
    };
    LoadEconomyAndMerchants(merchants);

    const security::PlayerId pid = 96206;
    auto player = MakePlayer(pid, 3);
    EXPECT(data::PlayerStore::Save(player));
    Fund(pid, 500);

    const auto r = economy::Merchant::Buy(pid, "town_blacksmith", "torch", 1);
    EXPECT(r.status == economy::TxStatus::PermissionDenied);
    EXPECT(economy::GetBalance(pid, "gold") == 500);

    auto loaded = data::PlayerStore::Load(pid);
    EXPECT(loaded.has_value());
    if (loaded)
    {
        loaded->level = 10;
        EXPECT(data::PlayerStore::Save(*loaded));
    }
    security::RateLimiter::Reset(pid, "economy.merchant");
    const auto ok = economy::Merchant::Buy(pid, "town_blacksmith", "torch", 1);
    EXPECT(ok.status == economy::TxStatus::Success);
    EXPECT(economy::GetBalance(pid, "gold") == 485);

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_BuyPermissionDenied)
{
    const auto dir = MakeTempPlayerDir("buy_perm");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    security::Permissions::Register("economy.merchant", security::Level::ADMIN);
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96207;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 200);

    const auto r = economy::Merchant::Buy(pid, "town_blacksmith", "torch", 1);
    EXPECT(r.status == economy::TxStatus::PermissionDenied);
    EXPECT(economy::GetBalance(pid, "gold") == 200);

    security::Permissions::Register("economy.merchant", security::Level::PLAYER);
    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_SellSuccessAndUnknownItem)
{
    const auto dir = MakeTempPlayerDir("sell_ok");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96208;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 10);
    loadout::SeedTestInventory(pid, kMeatBp, 4);
    loadout::SeedTestInventory(pid, kIngotBp, 1);

    const auto r = economy::Merchant::Sell(pid, "town_blacksmith", "cooked_meat", 4);
    EXPECT(r.status == economy::TxStatus::Success);
    EXPECT(economy::GetBalance(pid, "gold") == 30);
    EXPECT(loadout::CountTestInventory(pid, kMeatBp) == 0);

    const auto unknown = economy::Merchant::Sell(pid, "town_blacksmith", "sword", 1);
    EXPECT(unknown.status == economy::TxStatus::InvalidAmount);
    EXPECT(economy::GetBalance(pid, "gold") == 30);

    const auto ingot = economy::Merchant::Sell(pid, "town_blacksmith", kIngotBp, 1);
    EXPECT(ingot.status == economy::TxStatus::Success);
    EXPECT(economy::GetBalance(pid, "gold") == 60);

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_SellRejectsWithoutItems)
{
    const auto dir = MakeTempPlayerDir("sell_no_items");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96213;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 10);

    const auto r = economy::Merchant::Sell(pid, "town_blacksmith", "cooked_meat", 1);
    EXPECT(r.status == economy::TxStatus::InsufficientItems);
    EXPECT(economy::GetBalance(pid, "gold") == 10);

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_SellWouldExceedMaxIsAtomic)
{
    const auto dir = MakeTempPlayerDir("sell_max");
    ConfigurePlayerStore(dir);
    PrepareSecurity();

    nlohmann::json merchants;
    merchants["jeweler"] = {
        {"name", "Joaillier"},
        {"currency", "gem"},
        {"conditions", {{"min_level", 1}}},
        {"sells", nlohmann::json::array()},
        {"buys", nlohmann::json::array({
            nlohmann::json{
                {"id", "cooked_meat"},
                {"blueprint", kMeatBp},
                {"price", 40}
            }
        })}
    };
    LoadEconomyAndMerchants(merchants);

    const security::PlayerId pid = 96209;
    EXPECT(SavePlayer(MakePlayer(pid)));
    security::RateLimiter::Reset(pid, "economy.add");
    security::RateLimiter::Reset(pid, "economy.merchant");
    EXPECT(economy::Add(pid, "gem", 20, "init", "test").status == economy::TxStatus::Success);
    loadout::SeedTestInventory(pid, kMeatBp, 1);

    const auto r = economy::Merchant::Sell(pid, "jeweler", "cooked_meat", 1);
    EXPECT(r.status == economy::TxStatus::WouldExceedMax);
    EXPECT(economy::GetBalance(pid, "gem") == 20);
    EXPECT(loadout::CountTestInventory(pid, kMeatBp) == 1);

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_CommandListInfoBuySell)
{
    const auto dir = MakeTempPlayerDir("cmd");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96210;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 200);

    const auto listed = economy::HandleMerchantCommand(pid, {"marchand", "list"});
    EXPECT(listed.handled);
    EXPECT(listed.success);
    EXPECT(listed.message.find("town_blacksmith") != std::string::npos);

    const auto info = economy::HandleMerchantCommand(pid, {"info", "town_blacksmith"});
    EXPECT(info.success);
    EXPECT(info.message.find("Forgeron") != std::string::npos);
    EXPECT(info.message.find("torch") != std::string::npos);
    EXPECT(info.message.find("cooked_meat") != std::string::npos);

    const auto bought = economy::HandleMerchantCommand(pid,
        {"marchand", "acheter", "town_blacksmith", "torch", "2"});
    EXPECT(bought.success);
    EXPECT(economy::GetBalance(pid, "gold") == 170);

    loadout::SeedTestInventory(pid, kIngotBp, 1);
    const auto sold = economy::HandleMerchantCommand(pid,
        {"vendre", "town_blacksmith", "metal_ingot"});
    EXPECT(sold.success);
    EXPECT(economy::GetBalance(pid, "gold") == 200);

    const auto usage = economy::HandleMerchantCommand(pid, {"marchand", "foo"});
    EXPECT(usage.handled);
    EXPECT(!usage.success);
    EXPECT(usage.message.find("Usage") != std::string::npos);

    const auto badQty = economy::HandleMerchantCommand(pid,
        {"acheter", "town_blacksmith", "torch", "nope"});
    EXPECT(!badQty.success);

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_AuditBuyAndSell)
{
    const auto dir = MakeTempPlayerDir("audit");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96211;
    EXPECT(SavePlayer(MakePlayer(pid)));
    Fund(pid, 200);

    EXPECT(economy::Merchant::Buy(pid, "town_blacksmith", "torch", 1).status
        == economy::TxStatus::Success);
    EXPECT(HasAudit("economy.merchant.buy", pid));

    loadout::SeedTestInventory(pid, kMeatBp, 1);
    EXPECT(economy::Merchant::Sell(pid, "town_blacksmith", "cooked_meat", 1).status
        == economy::TxStatus::Success);
    EXPECT(HasAudit("economy.merchant.sell", pid));

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_MissingPlayerData)
{
    const auto dir = MakeTempPlayerDir("missing");
    ConfigurePlayerStore(dir);
    PrepareSecurity();
    LoadEconomyAndMerchants();

    const security::PlayerId pid = 96212;
    security::RateLimiter::Reset(pid, "economy.merchant");
    const auto r = economy::Merchant::Buy(pid, "town_blacksmith", "torch", 1);
    EXPECT(r.status == economy::TxStatus::PlayerDataUnavailable);

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
    CleanupPlayerStore(dir);
}

TEST(Merchant_NegativeStockRejected)
{
    auto merchants = ValidMerchants();
    merchants["town_blacksmith"]["sells"].push_back({
        {"id", "cursed"},
        {"blueprint", kIngotBp},
        {"price", 1},
        {"stock", -3}
    });
    LoadEconomyAndMerchants(merchants);

    const auto info = economy::Merchant::Get("town_blacksmith");
    EXPECT(info.has_value());
    if (info)
    {
        for (const auto& listing : info->sells)
            EXPECT(listing.id != "cursed");
        EXPECT(info->sells.size() == 2);
    }

    economy::Merchant::ResetForTests();
    economy::Registry::Shutdown();
}

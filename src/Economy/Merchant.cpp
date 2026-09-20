// ============================================================================
// RPFramework - Economy / Merchant - implémentation
// ============================================================================
#include "Economy/Merchant.h"

#include "Asa/BlueprintPath.h"
#include "Core/Config.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/PluginContext.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Economy/Registry.h"
#include "Faction/Registry.h"
#include "Faction/Reputation.h"
#include "Loadout/AsaDeliver.h"
#include "Loadout/Item.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace rpframework::economy
{
    using rpframework::asa::BlueprintKey;
    using rpframework::asa::NormalizeBlueprintPath;

    namespace
    {
        constexpr const char* kPermissionKey = "economy.merchant";
        constexpr const char* kAuditBuy      = "economy.merchant.buy";
        constexpr const char* kAuditSell     = "economy.merchant.sell";

        std::string DefaultListingId(const std::string& normalizedBlueprint)
        {
            const auto dot = normalizedBlueprint.rfind('.');
            if (dot != std::string::npos && dot + 1 < normalizedBlueprint.size())
                return normalizedBlueprint.substr(dot + 1);
            const auto slash = normalizedBlueprint.rfind('/');
            if (slash != std::string::npos)
                return normalizedBlueprint.substr(slash + 1);
            return normalizedBlueprint;
        }

        bool SameListingToken(const MerchantListing& listing, const std::string& token)
        {
            if (token.empty()) return false;
            if (listing.id == token) return true;
            const auto key = BlueprintKey(token);
            if (!key.empty() && key == BlueprintKey(listing.blueprint)) return true;
            if (!key.empty() && key == BlueprintKey(listing.id)) return true;
            return false;
        }

        MerchantListing* FindListing(std::vector<MerchantListing>& list, const std::string& token)
        {
            for (auto& item : list)
            {
                if (SameListingToken(item, token)) return &item;
            }
            return nullptr;
        }

        const MerchantListing* FindListing(const std::vector<MerchantListing>& list,
                                           const std::string& token)
        {
            for (const auto& item : list)
            {
                if (SameListingToken(item, token)) return &item;
            }
            return nullptr;
        }

        std::vector<std::string> ReadStringArray(const nlohmann::json& j, const char* key)
        {
            std::vector<std::string> out;
            if (!j.contains(key) || !j[key].is_array()) return out;
            for (const auto& v : j[key])
            {
                if (v.is_string()) out.push_back(v.get<std::string>());
            }
            return out;
        }

        std::unordered_map<std::string, int> ReadReputationMap(const nlohmann::json& j,
                                                               const char* key)
        {
            std::unordered_map<std::string, int> out;
            if (!j.contains(key) || !j[key].is_object()) return out;
            for (auto it = j[key].begin(); it != j[key].end(); ++it)
            {
                if (it->is_number_integer()) out[it.key()] = it->get<int>();
            }
            return out;
        }

        character::SelectionCondition ReadConditions(const nlohmann::json& j)
        {
            character::SelectionCondition c;
            if (!j.is_object()) return c;
            c.minLevel = j.value("min_level", 1);
            c.maxLevel = j.value("max_level", std::numeric_limits<int>::max());

            auto races = ReadStringArray(j, "races");
            if (races.empty()) races = ReadStringArray(j, "required_races");
            c.requiredRaces = std::move(races);
            c.excludedRaces = ReadStringArray(j, "excluded_races");

            auto professions = ReadStringArray(j, "professions");
            if (professions.empty()) professions = ReadStringArray(j, "required_professions");
            c.requiredProfessions = std::move(professions);
            c.excludedProfessions = ReadStringArray(j, "excluded_professions");

            auto classes = ReadStringArray(j, "classes");
            if (classes.empty()) classes = ReadStringArray(j, "required_classes");
            c.requiredClasses = std::move(classes);
            c.excludedClasses = ReadStringArray(j, "excluded_classes");

            c.minReputation = ReadReputationMap(j, "min_reputation");
            return c;
        }

        MerchantListing ListingFromJson(const nlohmann::json& j, const std::string& context)
        {
            if (!j.is_object())
                throw std::runtime_error(context + " : payload n'est pas un objet");

            MerchantListing listing;
            listing.blueprint = NormalizeBlueprintPath(j.value("blueprint", std::string{}));
            if (listing.blueprint.empty())
                throw std::runtime_error(context + " : blueprint vide");

            listing.price = j.value("price", static_cast<int64_t>(0));
            if (listing.price <= 0)
                throw std::runtime_error(context + " : price doit etre > 0");

            const int stock = j.value("stock", 0);
            if (stock < 0)
                throw std::runtime_error(context + " : stock ne peut pas etre negatif");
            // Config : 0 = illimité. Un stock positif est fini.
            listing.unlimited = (stock == 0);
            listing.stock     = listing.unlimited ? 0 : stock;

            listing.id = j.value("id", std::string{});
            if (listing.id.empty())
                listing.id = DefaultListingId(listing.blueprint);
            if (listing.id.empty())
                throw std::runtime_error(context + " : id vide");

            return listing;
        }

        std::vector<MerchantListing> ReadListings(const nlohmann::json& merchant,
                                                  const char* key,
                                                  const std::string& merchantId)
        {
            std::vector<MerchantListing> out;
            if (!merchant.contains(key) || !merchant[key].is_array()) return out;
            int index = 0;
            for (const auto& entry : merchant[key])
            {
                const std::string ctx = "marchand '" + merchantId + "' / " + key
                    + "[" + std::to_string(index) + "]";
                ++index;
                try
                {
                    out.push_back(ListingFromJson(entry, ctx));
                }
                catch (const std::exception& ex)
                {
                    rpframework::core::LogError("Merchant: article rejete: {}", ex.what());
                }
            }
            return out;
        }

        MerchantInfo MerchantFromJson(const std::string& id, const nlohmann::json& j)
        {
            if (!j.is_object())
                throw std::runtime_error("payload n'est pas un objet");
            if (id.empty())
                throw std::runtime_error("id vide");

            MerchantInfo info;
            info.id       = id;
            info.name     = j.value("name", id);
            info.currency = j.value("currency", std::string{});
            if (info.currency.empty())
                throw std::runtime_error("currency vide");
            if (!Registry::HasCurrency(info.currency))
                throw std::runtime_error("monnaie inconnue : " + info.currency);

            info.conditions = ReadConditions(j.value("conditions", nlohmann::json::object()));
            info.faction    = j.value("faction", std::string{});
            if (!info.faction.empty()
                && !rpframework::faction::Registry::HasFaction(info.faction))
            {
                rpframework::core::LogError(
                    "Merchant: faction '{}' inconnue pour '{}', champ vide.",
                    info.faction, id);
                info.faction.clear();
            }
            info.sells      = ReadListings(j, "sells", id);
            info.buys       = ReadListings(j, "buys", id);
            return info;
        }

        bool PlayerMeetsConditions(PlayerId player,
                                   const character::SelectionCondition& conditions,
                                   std::string& violation)
        {
            auto load = rpframework::data::PlayerStore::LoadDetailed(player);
            if (!load.HasData())
            {
                violation = "profil joueur indisponible";
                return false;
            }
            const auto& data = *load.data;
            if (!conditions.IsSatisfiedBy(data.race, data.profession, data.playerClass,
                                          data.level, data.reputation))
            {
                violation = conditions.DescribeViolation(
                    data.race, data.profession, data.playerClass,
                    data.level, data.reputation);
                return false;
            }
            return true;
        }

        std::optional<TxResult> RequirePositiveQty(int qty)
        {
            if (qty <= 0)
                return TxResult::Make(TxStatus::InvalidAmount,
                    "quantite invalide : " + std::to_string(qty));
            return std::nullopt;
        }

        std::optional<TxResult> TotalCost(int64_t price, int qty, int64_t& outTotal)
        {
            if (price <= 0)
                return TxResult::Make(TxStatus::InvalidAmount, "prix invalide");
            if (qty > 0 && price > (std::numeric_limits<int64_t>::max() / qty))
                return TxResult::Make(TxStatus::WouldExceedMax, "montant trop eleve");
            outTotal = price * static_cast<int64_t>(qty);
            return std::nullopt;
        }

        // Prix unitaire après standing. `buying` = le joueur achète au marchand.
        std::optional<TxResult> StandingUnitPrice(PlayerId player,
                                                  const MerchantInfo& merchant,
                                                  bool buying,
                                                  int64_t basePrice,
                                                  int64_t& outUnit,
                                                  std::string& standingId)
        {
            outUnit = basePrice;
            standingId.clear();
            if (merchant.faction.empty()) return std::nullopt;

            const auto standing = rpframework::faction::GetStanding(player, merchant.faction);
            if (standing)
            {
                standingId = standing->id;
                if (!standing->canTrade)
                {
                    return TxResult::Make(TxStatus::PermissionDenied,
                        "standing insuffisant (" + standing->name + ")");
                }
                const double mult = buying ? standing->buyMult : standing->sellMult;
                long long rounded = std::llround(static_cast<double>(basePrice) * mult);
                if (rounded < 1) rounded = 1;
                outUnit = static_cast<int64_t>(rounded);
            }
            return std::nullopt;
        }

        bool StockAvailable(const MerchantListing& listing, int qty)
        {
            if (listing.unlimited) return true;
            return listing.stock >= qty;
        }

        void ReserveStock(MerchantListing& listing, int qty)
        {
            if (listing.unlimited) return;
            listing.stock -= qty;
        }

        void ReleaseStock(MerchantListing& listing, int qty)
        {
            if (listing.unlimited) return;
            listing.stock += qty;
        }

        std::string ClipChat(std::string text)
        {
            if (text.size() > 220) text = text.substr(0, 217) + "...";
            return text;
        }

        bool ValidMerchantId(const std::string& id)
        {
            if (id.empty() || id.size() > 64) return false;
            for (unsigned char c : id)
            {
                if (!std::isalnum(c) && c != '_' && c != '-') return false;
            }
            return true;
        }

        MerchantCommandResult FromMessage(bool success, std::string message)
        {
            return {true, success, ClipChat(std::move(message))};
        }

        std::string FormatListing(const MerchantListing& listing)
        {
            std::ostringstream oss;
            oss << listing.id << " " << listing.price;
            if (!listing.unlimited)
                oss << " x" << listing.stock;
            else
                oss << " illimite";
            return oss.str();
        }

        std::string JoinListings(const std::vector<MerchantListing>& list)
        {
            std::string out;
            for (const auto& item : list)
            {
                if (!out.empty()) out += ", ";
                out += FormatListing(item);
            }
            return out.empty() ? "-" : out;
        }

        int DeliverBoughtItems(PlayerId player, const MerchantListing& listing, int qty)
        {
            rpframework::loadout::Item item;
            item.id       = listing.id;
            item.quantity = qty;
            item.extras   = nlohmann::json::object();
            item.extras["blueprint"] = listing.blueprint;
            return rpframework::loadout::TryGiveItems(player, {item});
        }
    }

    Merchant& Merchant::Instance()
    {
        static Merchant inst;
        return inst;
    }

    void Merchant::Load()
    {
        Instance().LoadFromConfigImpl();
    }

    void Merchant::LoadFromConfig()
    {
        Instance().LoadFromConfigImpl();
    }

    void Merchant::ResetForTests()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.merchants_.clear();
        self.stockOverlay_.clear();
        self.initialized_ = false;
#ifdef RPFRAMEWORK_TESTS
        rpframework::loadout::ClearTestInventory();
#endif
    }

    void Merchant::LoadFromConfigImpl()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        const auto cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* section = nullptr;
        if (cfg.is_object() && cfg.contains("merchants") && cfg["merchants"].is_object())
            section = &cfg["merchants"];
        self.stockOverlay_.clear();
        self.LoadStockOverlayFileLocked();
        self.LoadDefinitionsFromSectionLocked(section);
        self.initialized_ = true;
    }

    void Merchant::LoadDefinitionsFromSection(const nlohmann::json* section)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.LoadDefinitionsFromSectionLocked(section);
        self.initialized_ = true;
    }

    void Merchant::LoadDefinitionsFromSectionLocked(const nlohmann::json* section)
    {
        merchants_.clear();

        if (section == nullptr || !section->is_object())
        {
            rpframework::core::LogInfo(
                "Merchant: aucune section 'merchants' dans la config ; registre vide.");
            return;
        }

        for (auto it = section->begin(); it != section->end(); ++it)
        {
            try
            {
                MerchantInfo info = MerchantFromJson(it.key(), it.value());
                merchants_.emplace(info.id, std::move(info));
            }
            catch (const std::exception& ex)
            {
                rpframework::core::LogError("Merchant: marchand '{}' invalide: {}",
                    it.key(), ex.what());
            }
        }

        rpframework::core::LogInfo("Merchant: {} marchands charges.", merchants_.size());
        ApplyStockOverlayLocked();
    }

    void Merchant::ApplyStockOverlayLocked()
    {
        for (auto& [id, merchant] : merchants_)
        {
            auto it = stockOverlay_.find(id);
            if (it == stockOverlay_.end()) continue;
            for (auto& listing : merchant.sells)
            {
                if (listing.unlimited) continue;
                auto jt = it->second.find(listing.id);
                if (jt != it->second.end() && jt->second >= 0)
                    listing.stock = jt->second;
            }
        }
    }

    void Merchant::RememberStockLocked(const std::string& merchantId,
                                       const MerchantListing& listing)
    {
        if (listing.unlimited) return;
        stockOverlay_[merchantId][listing.id] = listing.stock;
        SaveStockOverlayFileLocked();
    }

    void Merchant::LoadStockOverlayFileLocked()
    {
        const auto path = rpframework::core::GetPluginDir() / "merchant_stock.json";
        std::ifstream in(path);
        if (!in.is_open()) return;
        try
        {
            nlohmann::json parsed;
            in >> parsed;
            if (!parsed.is_object()) return;
            for (auto merchant = parsed.begin(); merchant != parsed.end(); ++merchant)
            {
                if (!merchant->is_object() || merchant.key().empty()) continue;
                for (auto listing = merchant->begin(); listing != merchant->end(); ++listing)
                {
                    if (!listing->is_number_integer() || listing.key().empty()) continue;
                    const int remaining = listing->get<int>();
                    if (remaining < 0) continue;
                    stockOverlay_[merchant.key()][listing.key()] = remaining;
                }
            }
        }
        catch (const std::exception& ex)
        {
            rpframework::core::LogWarn("Merchant: overlay stock illisible: {}", ex.what());
        }
    }

    void Merchant::SaveStockOverlayFileLocked() const
    {
#ifndef RPFRAMEWORK_TESTS
        nlohmann::json root = nlohmann::json::object();
        for (const auto& [merchantId, listings] : stockOverlay_)
        {
            nlohmann::json node = nlohmann::json::object();
            for (const auto& [listingId, remaining] : listings)
                node[listingId] = remaining;
            root[merchantId] = std::move(node);
        }
        const auto dir = rpframework::core::GetPluginDir();
        if (!rpframework::core::EnsureDirectoryExists(dir)) return;
        const auto path = dir / "merchant_stock.json";
        const auto tmp = dir / "merchant_stock.json.tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out.is_open()) return;
            out << root.dump(2);
            out.flush();
            if (!out.good()) return;
        }
#ifdef _WIN32
        if (!MoveFileExW(tmp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            rpframework::core::LogWarn("Merchant: impossible d'ecrire merchant_stock.json ({})",
                static_cast<unsigned long>(GetLastError()));
        }
#else
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec)
        {
            std::filesystem::remove(tmp, ec);
            rpframework::core::LogWarn("Merchant: impossible d'ecrire merchant_stock.json: {}",
                ec.message());
        }
#endif
#endif
    }

    bool Merchant::Has(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.merchants_.find(id) != self.merchants_.end();
    }

    std::optional<MerchantInfo> Merchant::Get(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.merchants_.find(id);
        if (it == self.merchants_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<MerchantInfo> Merchant::List()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<MerchantInfo> out;
        out.reserve(self.merchants_.size());
        for (const auto& [_, v] : self.merchants_) out.push_back(v);
        std::sort(out.begin(), out.end(),
                  [](const MerchantInfo& a, const MerchantInfo& b) { return a.id < b.id; });
        return out;
    }

    std::vector<std::string> Merchant::ListIds()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<std::string> out;
        out.reserve(self.merchants_.size());
        for (const auto& [k, _] : self.merchants_) out.push_back(k);
        std::sort(out.begin(), out.end());
        return out;
    }

    TxResult Merchant::Buy(PlayerId player, const std::string& merchantId,
                           const std::string& item, int qty)
    {
        using namespace rpframework::security;

        if (!Permissions::CheckFor(player, kPermissionKey))
        {
            AuditLog::LogDenied(kAuditBuy, player, "permission",
                {{"merchant", merchantId}, {"item", item}});
            return TxResult::Make(TxStatus::PermissionDenied,
                "permission refusee pour economy.merchant");
        }
        if (!RateLimiter::Allow(player, kPermissionKey))
        {
            AuditLog::LogDenied(kAuditBuy, player, "rate_limit",
                {{"merchant", merchantId}, {"item", item}});
            return TxResult::Make(TxStatus::RateLimited,
                "rate limit atteint pour economy.merchant");
        }
        if (auto err = RequirePositiveQty(qty)) return *err;

        int64_t total = 0;
        std::string currency;
        MerchantListing snapshot;
        std::string standingId;
        std::string merchantFaction;
        {
            auto& self = Instance();
            std::lock_guard<std::mutex> lock(self.mutex_);

            auto it = self.merchants_.find(merchantId);
            if (it == self.merchants_.end())
                return TxResult::Make(TxStatus::InvalidAmount, "marchand inconnu : " + merchantId);

            auto& merchant = it->second;
            auto* listing = FindListing(merchant.sells, item);
            if (listing == nullptr)
                return TxResult::Make(TxStatus::InvalidAmount, "article inconnu : " + item);
            if (!StockAvailable(*listing, qty))
                return TxResult::Make(TxStatus::InsufficientFunds, "stock insuffisant");

            std::string violation;
            if (!PlayerMeetsConditions(player, merchant.conditions, violation))
            {
                if (violation == "profil joueur indisponible")
                    return TxResult::Make(TxStatus::PlayerDataUnavailable, violation);
                AuditLog::LogDenied(kAuditBuy, player, "conditions",
                    {{"merchant", merchantId}, {"detail", violation}});
                return TxResult::Make(TxStatus::PermissionDenied,
                    "conditions non remplies : " + violation);
            }

            int64_t unitPrice = listing->price;
            if (auto err = StandingUnitPrice(player, merchant, true, listing->price,
                                             unitPrice, standingId))
            {
                AuditLog::LogDenied(kAuditBuy, player, "standing",
                    {{"merchant", merchantId}, {"faction", merchant.faction},
                     {"standing", standingId}, {"detail", err->message}});
                return *err;
            }
            merchantFaction = merchant.faction;
            if (auto err = TotalCost(unitPrice, qty, total)) return *err;
            currency = merchant.currency;

            const int64_t balance = GetBalance(player, currency);
            if (balance < total)
            {
                AuditLog::LogDenied(kAuditBuy, player, "insufficient_funds",
                    {{"merchant", merchantId}, {"have", balance}, {"want", total}});
                return TxResult::Make(TxStatus::InsufficientFunds, "solde insuffisant");
            }

            // Toutes les conditions sont OK : on réserve le stock AVANT le
            // débit Wallet, et on le rend si Wallet échoue.
            ReserveStock(*listing, qty);
            snapshot = *listing;
            self.RememberStockLocked(merchantId, *listing);
        }

        const auto paid = Subtract(player, currency, total, kAuditBuy, merchantId);
        if (paid.status != TxStatus::Success)
        {
            auto& self = Instance();
            std::lock_guard<std::mutex> lock(self.mutex_);
            auto it = self.merchants_.find(merchantId);
            if (it != self.merchants_.end())
            {
                if (auto* listing = FindListing(it->second.sells, item))
                {
                    ReleaseStock(*listing, qty);
                    self.RememberStockLocked(merchantId, *listing);
                }
            }
            return paid;
        }

        const int given = DeliverBoughtItems(player, snapshot, qty);
        if (given <= 0)
        {
            security::RateLimiter::Reset(player, "economy.add");
            const auto refunded = Add(player, currency, total, "merchant.buy.refund", merchantId);
            {
                auto& self = Instance();
                std::lock_guard<std::mutex> lock(self.mutex_);
                auto it = self.merchants_.find(merchantId);
                if (it != self.merchants_.end())
                {
                    if (auto* listing = FindListing(it->second.sells, item))
                    {
                        ReleaseStock(*listing, qty);
                        self.RememberStockLocked(merchantId, *listing);
                    }
                }
            }
            if (refunded.status != TxStatus::Success)
            {
                rpframework::core::LogError(
                    "Merchant: livraison ASA echouee et remboursement impossible pour {} ({}).",
                    player, refunded.message);
                AuditLog::LogDenied(kAuditBuy, player, "give_failed_refund_failed",
                    {{"merchant", merchantId}, {"item", snapshot.id},
                     {"amount", total}, {"detail", refunded.message}});
                return TxResult::Make(TxStatus::InsufficientItems,
                    "livraison echouee, remboursement impossible");
            }
            AuditLog::LogDenied(kAuditBuy, player, "give_failed_refunded",
                {{"merchant", merchantId}, {"item", snapshot.id},
                 {"qty", qty}, {"amount", total}, {"after", refunded.newBalance}});
            return TxResult::Make(TxStatus::InsufficientItems,
                "livraison echouee, remboursement effectue");
        }

        AuditLog::Log(kAuditBuy, player, {
            {"merchant", merchantId},
            {"item",     snapshot.id},
            {"blueprint", snapshot.blueprint},
            {"qty",      qty},
            {"currency", currency},
            {"amount",   total},
            {"after",    paid.newBalance},
            {"faction",  merchantFaction},
            {"standing", standingId},
        }, audit_severity::kTransaction);

        return TxResult::MakeSuccess(paid.newBalance,
            "achete " + std::to_string(qty) + " " + snapshot.id
            + " pour " + std::to_string(total) + " " + currency);
    }

    TxResult Merchant::Sell(PlayerId player, const std::string& merchantId,
                            const std::string& item, int qty)
    {
        using namespace rpframework::security;

        if (!Permissions::CheckFor(player, kPermissionKey))
        {
            AuditLog::LogDenied(kAuditSell, player, "permission",
                {{"merchant", merchantId}, {"item", item}});
            return TxResult::Make(TxStatus::PermissionDenied,
                "permission refusee pour economy.merchant");
        }
        if (!RateLimiter::Allow(player, kPermissionKey))
        {
            AuditLog::LogDenied(kAuditSell, player, "rate_limit",
                {{"merchant", merchantId}, {"item", item}});
            return TxResult::Make(TxStatus::RateLimited,
                "rate limit atteint pour economy.merchant");
        }
        if (auto err = RequirePositiveQty(qty)) return *err;

        int64_t total = 0;
        std::string currency;
        MerchantListing snapshot;
        std::string standingId;
        std::string merchantFaction;
        {
            auto& self = Instance();
            std::lock_guard<std::mutex> lock(self.mutex_);

            auto it = self.merchants_.find(merchantId);
            if (it == self.merchants_.end())
                return TxResult::Make(TxStatus::InvalidAmount, "marchand inconnu : " + merchantId);

            auto& merchant = it->second;
            auto* listing = FindListing(merchant.buys, item);
            if (listing == nullptr)
                return TxResult::Make(TxStatus::InvalidAmount, "article non rachete : " + item);

            std::string violation;
            if (!PlayerMeetsConditions(player, merchant.conditions, violation))
            {
                if (violation == "profil joueur indisponible")
                    return TxResult::Make(TxStatus::PlayerDataUnavailable, violation);
                AuditLog::LogDenied(kAuditSell, player, "conditions",
                    {{"merchant", merchantId}, {"detail", violation}});
                return TxResult::Make(TxStatus::PermissionDenied,
                    "conditions non remplies : " + violation);
            }

            int64_t unitPrice = listing->price;
            if (auto err = StandingUnitPrice(player, merchant, false, listing->price,
                                             unitPrice, standingId))
            {
                AuditLog::LogDenied(kAuditSell, player, "standing",
                    {{"merchant", merchantId}, {"faction", merchant.faction},
                     {"standing", standingId}, {"detail", err->message}});
                return *err;
            }
            merchantFaction = merchant.faction;
            if (auto err = TotalCost(unitPrice, qty, total)) return *err;
            currency = merchant.currency;
            snapshot = *listing;
        }

        if (!rpframework::loadout::TryTakeItems(player, snapshot.blueprint, qty))
        {
            return TxResult::Make(TxStatus::InsufficientItems, "objets insuffisants");
        }

        const auto credited = Add(player, currency, total, kAuditSell, merchantId);
        if (credited.status != TxStatus::Success)
        {
            loadout::Item refund;
            refund.id = snapshot.id;
            refund.quantity = qty;
            refund.extras["blueprint"] = snapshot.blueprint;
            rpframework::loadout::TryGiveItems(player, {refund});
            return credited;
        }

        AuditLog::Log(kAuditSell, player, {
            {"merchant", merchantId},
            {"item",     snapshot.id},
            {"blueprint", snapshot.blueprint},
            {"qty",      qty},
            {"currency", currency},
            {"amount",   total},
            {"after",    credited.newBalance},
            {"faction",  merchantFaction},
            {"standing", standingId},
        }, audit_severity::kTransaction);

        return TxResult::MakeSuccess(credited.newBalance,
            "vendu " + std::to_string(qty) + " " + snapshot.id
            + " pour " + std::to_string(total) + " " + currency);
    }

    MerchantCommandResult HandleMerchantCommand(PlayerId player,
                                                const std::vector<std::string>& args)
    {
        if (rpframework::core::PluginContext::GetState()
            == rpframework::core::PluginContext::State::Failed)
        {
            return FromMessage(false, "plugin indisponible");
        }

        std::vector<std::string> rest = args;
        if (!rest.empty() && rest.front() == "marchand")
            rest.erase(rest.begin());

        if (rest.empty() || rest[0] == "list" || rest[0] == "liste")
        {
            using namespace rpframework::security;
            if (!Permissions::CheckFor(player, kPermissionKey))
            {
                AuditLog::LogDenied("economy.merchant", player, "permission");
                return FromMessage(false, "permission refusee");
            }
            std::string message = "marchands: ";
            bool first = true;
            for (const auto& id : Merchant::ListIds())
            {
                if (!first) message += ", ";
                message += id;
                first = false;
            }
            if (first) message += "(aucun)";
            return FromMessage(true, message);
        }

        if (rest[0] == "info")
        {
            using namespace rpframework::security;
            if (!Permissions::CheckFor(player, kPermissionKey))
            {
                AuditLog::LogDenied("economy.merchant", player, "permission");
                return FromMessage(false, "permission refusee");
            }
            if (rest.size() < 2 || !ValidMerchantId(rest[1]))
                return FromMessage(false, "Usage: /marchand info <id>");
            const auto info = Merchant::Get(rest[1]);
            if (!info)
                return FromMessage(false, "marchand inconnu");
            std::ostringstream oss;
            oss << info->name << " (" << info->currency << ") vend: "
                << JoinListings(info->sells) << " achete: "
                << JoinListings(info->buys);
            return FromMessage(true, oss.str());
        }

        if (rest[0] == "acheter")
        {
            if (rest.size() < 3)
                return FromMessage(false, "Usage: /marchand acheter <id> <item> [qty]");
            if (!ValidMerchantId(rest[1]))
                return FromMessage(false, "id invalide");
            int qty = 1;
            if (rest.size() >= 4)
            {
                try { qty = std::stoi(rest[3]); }
                catch (...) { return FromMessage(false, "quantite invalide"); }
            }
            const auto result = Merchant::Buy(player, rest[1], rest[2], qty);
            return FromMessage(result.status == TxStatus::Success, result.message);
        }

        if (rest[0] == "vendre")
        {
            if (rest.size() < 3)
                return FromMessage(false, "Usage: /marchand vendre <id> <item> [qty]");
            if (!ValidMerchantId(rest[1]))
                return FromMessage(false, "id invalide");
            int qty = 1;
            if (rest.size() >= 4)
            {
                try { qty = std::stoi(rest[3]); }
                catch (...) { return FromMessage(false, "quantite invalide"); }
            }
            const auto result = Merchant::Sell(player, rest[1], rest[2], qty);
            return FromMessage(result.status == TxStatus::Success, result.message);
        }

        return FromMessage(false,
            "Usage: /marchand list|info <id>|acheter <id> <item> [qty]|vendre <id> <item> [qty]");
    }
}

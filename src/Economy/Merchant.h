// ============================================================================
// RPFramework - Economy / Merchant
//
// Marchands data-driven (GDD §46, phase 15). Tout vient de `config.merchants.*`.
// Aucun marchand baked-in. Une entrée invalide est rejetée au chargement.
//
// Transaction atomique :
//   1. permission + rate limit (`economy.merchant`)
//   2. conditions, stock, solde, inventaire  —  AVANT tout débit
//   3. Wallet::Subtract / Wallet::Add  (jamais PlayerData.wallets)
//      Sell consomme l'objet ASA (TryTakeItems) avant le crédit.
//      Buy : si GiveItem échoue → refund or + stock (pas d'outbox schéma).
//   4. audit `economy.merchant.buy` / `economy.merchant.sell`
//
// `stock: 0` dans la config = illimité. Un stock épuisé (runtime = 0 avec
// drapeau illimité à false) n'est PAS relisé en illimité.
// ============================================================================
#pragma once

#include "Character/Definitions.h"
#include "Economy/Wallet.h"
#include "Security/Types.h"

#include "json.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpframework::economy
{
    using PlayerId = rpframework::security::PlayerId;

    // -------------------------------------------------------------------------
    // Article d'un catalogue (vendu AU joueur, ou acheté AU joueur).
    // `blueprint` est stocké NORMALISÉ (asa::NormalizeBlueprintPath).
    // -------------------------------------------------------------------------
    struct MerchantListing
    {
        std::string id;
        std::string blueprint;
        int64_t     price = 0;
        int         stock = 0;       // restant ; ignoré si unlimited
        bool        unlimited = false;
    };

    struct MerchantInfo
    {
        std::string                         id;
        std::string                         name;
        std::string                         currency;
        std::string                         faction;  // optionnel : standing pour les prix
        character::SelectionCondition       conditions;
        std::vector<MerchantListing>        sells;  // le marchand vend au joueur
        std::vector<MerchantListing>        buys;   // le marchand achète au joueur
    };

    class Merchant
    {
    public:
        // Cycle de vie -----------------------------------------------------
        static void Load();
        static void LoadFromConfig();
        // Charge depuis un sous-objet `merchants` brut (tests).
        static void LoadDefinitionsFromSection(const nlohmann::json* section);
        static void ResetForTests();

        // API lecture ------------------------------------------------------
        static bool                      Has(const std::string& id);
        static std::optional<MerchantInfo> Get(const std::string& id);
        static std::vector<MerchantInfo> List();
        static std::vector<std::string>  ListIds();

        // Mutations : achat (joueur paie, reçoit l'article) / vente
        // (joueur cède l'article, reçoit la monnaie). Qty défaut = 1.
        // Réutilise economy::TxResult / TxStatus.
        static TxResult Buy (PlayerId player, const std::string& merchantId,
                             const std::string& item, int qty = 1);
        static TxResult Sell(PlayerId player, const std::string& merchantId,
                             const std::string& item, int qty = 1);

    private:
        Merchant() = default;
        static Merchant& Instance();
        void LoadFromConfigImpl();
        void LoadDefinitionsFromSectionLocked(const nlohmann::json* section);

        mutable std::mutex                               mutex_;
        std::unordered_map<std::string, MerchantInfo>    merchants_;
        std::unordered_map<std::string, std::unordered_map<std::string, int>> stockOverlay_;
        bool                                             initialized_ = false;

        void ApplyStockOverlayLocked();
        void RememberStockLocked(const std::string& merchantId, const MerchantListing& listing);
        void LoadStockOverlayFileLocked();
        void SaveStockOverlayFileLocked() const;
    };

    // Routeur chat `/marchand`. `args[0]` peut être "marchand" (collé par
    // AsaApi) ou directement le verbe (`list`, `info`, …).
    struct MerchantCommandResult
    {
        bool        handled = false;
        bool        success = false;
        std::string message;
    };

    MerchantCommandResult HandleMerchantCommand(PlayerId player,
                                                const std::vector<std::string>& args);
}

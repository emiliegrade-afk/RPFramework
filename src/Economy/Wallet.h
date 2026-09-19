// ============================================================================
// RPFramework - Economy / Wallet
//
// API centrale pour les soldes des joueurs (GDD §14).
// Tout ajout/retrait/transfert DOIT passer par cette API :
//   - le code externe ne doit pas modifier PlayerData.wallets directement.
//   - chaque opération est validée (currency existe, montant > 0,
//     solde suffisant, max_balance respecté) puis auditée.
//
// Types d'opérations :
//   Add       : ajout "système" (récolte, récompense quête).
//   Subtract  : retrait "système" (consommation, taxe).
//   Transfer  : entre deux joueurs (peer-to-peer).
//   Grant     : action GM/ADMIN (don, primes, dédommagement).
//   Reward    : synonyme d'Add, destiné à un module "récompense" (quêtes).
// ============================================================================
#pragma once

#include "Security/Types.h"  // PlayerId

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rpframework::data { struct PlayerData; }

namespace rpframework::economy
{
    using PlayerId = rpframework::security::PlayerId;

    enum class TxStatus
    {
        Success,
        UnknownCurrency,       // id n'existe pas dans le registry
        InvalidAmount,         // amount <= 0
        InsufficientFunds,     // solde < amount
        WouldExceedMax,        // addition dépasserait max_balance
        CurrencyNotTransferable,// tentative de Transfer sur une monnaie non-transférable
        PlayerDataUnavailable, // load/save a échoué
        RateLimited,
        PermissionDenied,
    };

    struct TxResult
    {
        TxStatus    status = TxStatus::Success;
        int64_t     newBalance = 0;   // solde APRÈS opération (un seul joueur)
        std::string message;

        static TxResult MakeSuccess(int64_t bal, std::string msg = "ok")
        {
            return {TxStatus::Success, bal, std::move(msg)};
        }
        static TxResult Make(TxStatus s, std::string msg)
        {
            return {s, 0, std::move(msg)};
        }
    };

    // Lecture pure : 0 si le joueur n'a pas de profil ou pas d'entrée pour
    // cette monnaie. N'audite pas, ne touche pas au disque.
    int64_t GetBalance(PlayerId player, std::string_view currency);

    // ----- Mutations centralisées -------------------------------------------
    // Chaque opération :
    //   1. permission (Permissions::Check)
    //   2. rate limit (RateLimiter::Allow)
    //   3. validation (currency existe, amount > 0, etc.)
    //   4. load (LoadDetailed)
    //   5. mutation de PlayerData.wallets
    //   6. save
    //   7. audit (audit.action = "economy.{op}")

    TxResult Add     (PlayerId player, std::string_view currency, int64_t amount,
                      std::string_view reason, std::string_view source = "system");

    TxResult Subtract(PlayerId player, std::string_view currency, int64_t amount,
                      std::string_view reason, std::string_view source = "system");

    TxResult Transfer(PlayerId from, PlayerId to, std::string_view currency, int64_t amount,
                      std::string_view reason);

    TxResult Grant   (PlayerId player, std::string_view currency, int64_t amount,
                      std::string_view reason, std::string_view source = "gm");

    TxResult Reward  (PlayerId player, std::string_view currency, int64_t amount,
                      std::string_view reason, std::string_view source = "system");

    // Crédit sur un PlayerData déjà chargé. Pas de Load/Save : l'appelant
    // (Quest::Complete) commit ensuite dans une seule transaction.
    TxResult CreditInPlace(data::PlayerData& data, std::string_view currency, int64_t amount);
}

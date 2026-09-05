// ============================================================================
// RPFramework - Economy / Wallet - implémentation
// ============================================================================
#include "Economy/Wallet.h"

#include "Economy/Registry.h"

#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"

#include "Core/Logger.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include <algorithm>

namespace rpframework::economy
{
    // -------------------------------------------------------------------------
    // Helpers internes
    // -------------------------------------------------------------------------
    namespace
    {
        // Charge un profil ou renvoie nullopt.
        // Refuse les statuts partiels (Corrupt, Unavailable) pour ne pas
        // muter par-dessus une donnée endommagée.
        std::optional<rpframework::data::PlayerData> LoadOrNull(PlayerId player)
        {
            auto load = rpframework::data::PlayerStore::LoadDetailed(player);
            if (!load.HasData()) return std::nullopt;
            return std::move(load.data);
        }

        // Vérifie que la monnaie existe. Renvoie un TxResult d'échec sinon.
        std::optional<TxResult> RequireCurrency(std::string_view currency, Currency& outCur)
        {
            auto c = Registry::GetCurrency(std::string(currency));
            if (!c) return TxResult::Make(TxStatus::UnknownCurrency,
                "monnaie inconnue : " + std::string(currency));
            outCur = *c;
            return std::nullopt;
        }

        // Vérifie amount > 0. Renvoie un TxResult d'échec sinon.
        std::optional<TxResult> RequirePositive(int64_t amount)
        {
            if (amount <= 0)
            {
                return TxResult::Make(TxStatus::InvalidAmount,
                    "montant invalide : " + std::to_string(amount));
            }
            return std::nullopt;
        }

        // Calcule le nouveau solde en respectant max_balance.
        // Renvoie un TxResult d'échec si dépassement.
        std::optional<TxResult> CheckMaxBalance(const Currency& cur, int64_t targetBalance)
        {
            if (cur.maxBalance > 0 && targetBalance > cur.maxBalance)
            {
                return TxResult::Make(TxStatus::WouldExceedMax,
                    "solde dépasserait le max (" + std::to_string(cur.maxBalance) + ")");
            }
            return std::nullopt;
        }
    }

    // -------------------------------------------------------------------------
    // Lecture
    // -------------------------------------------------------------------------

    int64_t GetBalance(PlayerId player, std::string_view currency)
    {
        auto data = LoadOrNull(player);
        if (!data) return 0;
        const auto it = data->wallets.find(std::string(currency));
        if (it == data->wallets.end()) return 0;
        return it->second;
    }

    // -------------------------------------------------------------------------
    // Add (système : récolte, récompense)
    // -------------------------------------------------------------------------

    TxResult Add(PlayerId player, std::string_view currency, int64_t amount,
                 std::string_view reason, std::string_view source)
    {
        using namespace rpframework::security;

        if (!Permissions::Check(Level::SYSTEM, "economy.add"))
        {
            AuditLog::LogDenied("economy.add", player, "permission");
            return TxResult::Make(TxStatus::PermissionDenied, "permission refusée pour economy.add");
        }
        if (!RateLimiter::Allow(player, "economy.add"))
        {
            AuditLog::LogDenied("economy.add", player, "rate_limit");
            return TxResult::Make(TxStatus::RateLimited, "rate limit atteint pour economy.add");
        }
        if (auto err = RequirePositive(amount)) return *err;

        Currency cur;
        if (auto err = RequireCurrency(currency, cur)) return *err;

        auto data = LoadOrNull(player);
        if (!data)
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "profil joueur indisponible");
        }

        const int64_t before = data->wallets.count(cur.id) ? data->wallets[cur.id] : 0;
        const int64_t after  = before + amount;
        if (auto err = CheckMaxBalance(cur, after)) return *err;

        data->wallets[cur.id] = after;
        if (!rpframework::data::PlayerStore::Save(*data))
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "sauvegarde échouée");
        }

        AuditLog::Log("economy.add", player, {
            {"currency", cur.id},
            {"amount",   static_cast<int64_t>(amount)},
            {"before",   before},
            {"after",    after},
            {"reason",   std::string(reason)},
            {"source",   std::string(source)},
        });
        return TxResult::MakeSuccess(after, "ajouté " + std::to_string(amount) + " " + cur.id);
    }

    // -------------------------------------------------------------------------
    // Subtract (système : consommation, taxe)
    // -------------------------------------------------------------------------

    TxResult Subtract(PlayerId player, std::string_view currency, int64_t amount,
                      std::string_view reason, std::string_view source)
    {
        using namespace rpframework::security;

        if (!Permissions::Check(Level::SYSTEM, "economy.subtract"))
        {
            AuditLog::LogDenied("economy.subtract", player, "permission");
            return TxResult::Make(TxStatus::PermissionDenied, "permission refusée pour economy.subtract");
        }
        if (!RateLimiter::Allow(player, "economy.subtract"))
        {
            AuditLog::LogDenied("economy.subtract", player, "rate_limit");
            return TxResult::Make(TxStatus::RateLimited, "rate limit atteint pour economy.subtract");
        }
        if (auto err = RequirePositive(amount)) return *err;

        Currency cur;
        if (auto err = RequireCurrency(currency, cur)) return *err;

        auto data = LoadOrNull(player);
        if (!data)
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "profil joueur indisponible");
        }

        const int64_t before = data->wallets.count(cur.id) ? data->wallets[cur.id] : 0;
        if (before < amount)
        {
            AuditLog::LogDenied("economy.subtract", player, "insufficient_funds",
                {{"currency", cur.id}, {"have", before}, {"want", static_cast<int64_t>(amount)}});
            return TxResult::Make(TxStatus::InsufficientFunds, "solde insuffisant");
        }
        const int64_t after = before - amount;

        data->wallets[cur.id] = after;
        if (!rpframework::data::PlayerStore::Save(*data))
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "sauvegarde échouée");
        }

        AuditLog::Log("economy.subtract", player, {
            {"currency", cur.id},
            {"amount",   static_cast<int64_t>(amount)},
            {"before",   before},
            {"after",    after},
            {"reason",   std::string(reason)},
            {"source",   std::string(source)},
        });
        return TxResult::MakeSuccess(after, "retiré " + std::to_string(amount) + " " + cur.id);
    }

    // -------------------------------------------------------------------------
    // Transfer (peer-to-peer)
    // -------------------------------------------------------------------------

    TxResult Transfer(PlayerId from, PlayerId to, std::string_view currency, int64_t amount,
                      std::string_view reason)
    {
        using namespace rpframework::security;

        if (from == to)
        {
            return TxResult::Make(TxStatus::InvalidAmount, "transfert vers soi-même interdit");
        }
        if (!Permissions::Check(Level::PLAYER, "economy.transfer"))
        {
            AuditLog::LogDenied("economy.transfer", from, "permission",
                {{"target", static_cast<int64_t>(to)}});
            return TxResult::Make(TxStatus::PermissionDenied, "permission refusée pour economy.transfer");
        }
        // Rate limit partagé entre from et to pour qu'un joueur ne puisse
        // pas s'en servir comme d'un sink illimité.
        if (!RateLimiter::Allow(from, "economy.transfer"))
        {
            AuditLog::LogDenied("economy.transfer", from, "rate_limit");
            return TxResult::Make(TxStatus::RateLimited, "rate limit atteint pour economy.transfer");
        }
        if (auto err = RequirePositive(amount)) return *err;

        Currency cur;
        if (auto err = RequireCurrency(currency, cur)) return *err;
        if (!cur.transferable)
        {
            AuditLog::LogDenied("economy.transfer", from, "not_transferable",
                {{"currency", cur.id}});
            return TxResult::Make(TxStatus::CurrencyNotTransferable,
                "monnaie non transférable : " + cur.id);
        }

        auto dataFrom = LoadOrNull(from);
        if (!dataFrom)
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "profil émetteur indisponible");
        }
        const int64_t beforeFrom = dataFrom->wallets.count(cur.id) ? dataFrom->wallets[cur.id] : 0;
        if (beforeFrom < amount)
        {
            AuditLog::LogDenied("economy.transfer", from, "insufficient_funds",
                {{"currency", cur.id}, {"have", beforeFrom}, {"want", static_cast<int64_t>(amount)},
                 {"target",  static_cast<int64_t>(to)}});
            return TxResult::Make(TxStatus::InsufficientFunds, "solde insuffisant");
        }
        const int64_t afterFrom = beforeFrom - amount;
        dataFrom->wallets[cur.id] = afterFrom;
        if (!rpframework::data::PlayerStore::Save(*dataFrom))
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "save émetteur échoué");
        }

        // Étape 2 : créditer le destinataire. Best-effort : si le 2e save
        // échoue, on logue l'échec (l'émetteur a été débité). Un vrai
        // système transactionnel le ferait en 2 phases, hors scope Phase 6.
        auto dataTo = LoadOrNull(to);
        if (!dataTo)
        {
            rpframework::core::LogError("Economy: transfert {} -> {} de {} {} partiellement appliqué (cible indisponible).",
                from, to, amount, cur.id);
            AuditLog::Log("economy.transfer_partial", from, {
                {"currency", cur.id}, {"amount", static_cast<int64_t>(amount)},
                {"target",   static_cast<int64_t>(to)},
            });
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "cible indisponible (émetteur débité)");
        }
        const int64_t beforeTo = dataTo->wallets.count(cur.id) ? dataTo->wallets[cur.id] : 0;
        const int64_t afterTo  = beforeTo + amount;
        if (auto err = CheckMaxBalance(cur, afterTo))
        {
            // Le destinataire ne peut pas recevoir. On rembourse l'émetteur.
            dataFrom->wallets[cur.id] = beforeFrom;
            (void)rpframework::data::PlayerStore::Save(*dataFrom);
            AuditLog::LogDenied("economy.transfer", from, "would_exceed_max_target",
                {{"currency", cur.id}, {"target", static_cast<int64_t>(to)},
                 {"max",     cur.maxBalance}});
            return *err;
        }
        dataTo->wallets[cur.id] = afterTo;
        if (!rpframework::data::PlayerStore::Save(*dataTo))
        {
            // Rembourse l'émetteur.
            dataFrom->wallets[cur.id] = beforeFrom;
            (void)rpframework::data::PlayerStore::Save(*dataFrom);
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "save cible échoué (remboursé)");
        }

        AuditLog::Log("economy.transfer", from, {
            {"currency", cur.id},
            {"amount",   static_cast<int64_t>(amount)},
            {"target",   static_cast<int64_t>(to)},
            {"from_before", beforeFrom}, {"from_after", afterFrom},
            {"to_before",   beforeTo},   {"to_after",   afterTo},
            {"reason",      std::string(reason)},
        });
        return TxResult::MakeSuccess(afterFrom, "transféré " + std::to_string(amount) + " " + cur.id);
    }

    // -------------------------------------------------------------------------
    // Grant (action GM/ADMIN : don, prime, dédommagement)
    // -------------------------------------------------------------------------

    TxResult Grant(PlayerId player, std::string_view currency, int64_t amount,
                   std::string_view reason, std::string_view source)
    {
        using namespace rpframework::security;

        if (!Permissions::Check(Level::GM, "economy.grant"))
        {
            AuditLog::LogDenied("economy.grant", player, "permission");
            return TxResult::Make(TxStatus::PermissionDenied, "permission refusée pour economy.grant");
        }
        if (!RateLimiter::Allow(player, "economy.grant"))
        {
            AuditLog::LogDenied("economy.grant", player, "rate_limit");
            return TxResult::Make(TxStatus::RateLimited, "rate limit atteint pour economy.grant");
        }
        if (auto err = RequirePositive(amount)) return *err;

        Currency cur;
        if (auto err = RequireCurrency(currency, cur)) return *err;

        auto data = LoadOrNull(player);
        if (!data)
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "profil joueur indisponible");
        }
        const int64_t before = data->wallets.count(cur.id) ? data->wallets[cur.id] : 0;
        const int64_t after  = before + amount;
        if (auto err = CheckMaxBalance(cur, after)) return *err;

        data->wallets[cur.id] = after;
        if (!rpframework::data::PlayerStore::Save(*data))
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "sauvegarde échouée");
        }

        AuditLog::Log("economy.grant", player, {
            {"currency", cur.id},
            {"amount",   static_cast<int64_t>(amount)},
            {"before",   before},
            {"after",    after},
            {"reason",   std::string(reason)},
            {"source",   std::string(source)},
        });
        return TxResult::MakeSuccess(after, "accordé " + std::to_string(amount) + " " + cur.id);
    }

    // -------------------------------------------------------------------------
    // Reward (synonyme d'Add, séparé pour la traçabilité des récompenses)
    // -------------------------------------------------------------------------

    TxResult Reward(PlayerId player, std::string_view currency, int64_t amount,
                    std::string_view reason, std::string_view source)
    {
        using namespace rpframework::security;

        if (!Permissions::Check(Level::SYSTEM, "economy.reward"))
        {
            AuditLog::LogDenied("economy.reward", player, "permission");
            return TxResult::Make(TxStatus::PermissionDenied, "permission refusée pour economy.reward");
        }
        if (!RateLimiter::Allow(player, "economy.reward"))
        {
            AuditLog::LogDenied("economy.reward", player, "rate_limit");
            return TxResult::Make(TxStatus::RateLimited, "rate limit atteint pour economy.reward");
        }
        if (auto err = RequirePositive(amount)) return *err;

        Currency cur;
        if (auto err = RequireCurrency(currency, cur)) return *err;

        auto data = LoadOrNull(player);
        if (!data)
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "profil joueur indisponible");
        }
        const int64_t before = data->wallets.count(cur.id) ? data->wallets[cur.id] : 0;
        const int64_t after  = before + amount;
        if (auto err = CheckMaxBalance(cur, after)) return *err;

        data->wallets[cur.id] = after;
        if (!rpframework::data::PlayerStore::Save(*data))
        {
            return TxResult::Make(TxStatus::PlayerDataUnavailable, "sauvegarde échouée");
        }

        AuditLog::Log("economy.reward", player, {
            {"currency", cur.id},
            {"amount",   static_cast<int64_t>(amount)},
            {"before",   before},
            {"after",    after},
            {"reason",   std::string(reason)},
            {"source",   std::string(source)},
        });
        return TxResult::MakeSuccess(after, "récompensé " + std::to_string(amount) + " " + cur.id);
    }
}

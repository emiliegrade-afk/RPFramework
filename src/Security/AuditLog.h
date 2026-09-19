// ============================================================================
// RPFramework - Security / AuditLog
//
// GDD §19 : "Les actions importantes doivent pouvoir être enregistrées."
// GDD §17 : on privilégie la détection + journalisation plutôt que le ban
//           automatique agressif.
//
// Format de stockage : JSON-lines (un objet JSON par ligne) dans le fichier
// pointé par Core::Paths::GetAuditLogPath(). Avantages :
//   - grep-friendly (chaque ligne est auto-suffisante) ;
//   - tail -f lisible en prod ;
//   - parseable par n'importe quel outil (jq, Python, etc.).
//
// En mémoire : ring buffer des N dernières entrées (pour /audit recent
// et debug), flushées par paquets vers le fichier.
//
// IMPORTANT : audit n'est PAS async en Phase 2 (pas de thread dédié) pour
// rester simple. Le flush est synchrone mais léger (append + 1 fwrite).
// ============================================================================
#pragma once

#include "json.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Security/Types.h"  // pour PlayerId

namespace rpframework::security
{
    // Sévérités d'audit standardisées.
    namespace audit_severity
    {
        inline constexpr const char* kInfo   = "info";
        inline constexpr const char* kWarn   = "warn";
        inline constexpr const char* kError  = "error";
        inline constexpr const char* kDenied = "denied";
        inline constexpr const char* kReward = "reward";
        inline constexpr const char* kTransaction = "transaction";
    }

    class AuditLog
    {
    public:
        // Configuration de rotation. Quand le fichier courant dépasse
        // maxSizeBytes après un flush, on enchaîne les rotations :
        // audit.log → audit.log.1 → audit.log.2 → ... → audit.log.N
        // où N = maxFiles. Au-delà, le plus ancien est supprimé.
        struct RotateConfig
        {
            std::uint64_t maxSizeBytes = 50ull * 1024ull * 1024ull;  // 50 MB
            int           maxFiles      = 5;
        };

        // Entrée d'audit (snapshot in-memory).
        struct Entry
        {
            std::chrono::system_clock::time_point timestamp;
            std::string                           severity;
            std::string                           action;
            PlayerId                              playerId = 0;
            nlohmann::json                        payload;
        };

        // Initialise le module. Doit être appelé par PluginContext.
        // - charge la config (enabled, max_buffer, file path via Core/Paths) ;
        // - crée le répertoire de logs au besoin.
        static void Initialize();

        // Recharge la config. Appelé après Config::Reload() typiquement.
        static void LoadFromConfig(const nlohmann::json& config);

        // Émet une entrée d'audit.
        static void Log(std::string_view action,
                        PlayerId playerId,
                        nlohmann::json payload = {},
                        std::string_view severity = audit_severity::kInfo);

        // Helper : log un refus (permission, rate limit, validation...).
        static void LogDenied(std::string_view action,
                              PlayerId playerId,
                              std::string_view reason,
                              nlohmann::json context = {});

        // Force le flush du buffer mémoire vers le fichier.
        // Idempotent. Thread-safe.
        static void Flush();

        // Flush final + désactivation. Appelé par PluginContext::Shutdown.
        static void Shutdown();

        // Renvoie les N dernières entrées (lecture seule, pour /audit).
        static std::vector<Entry> Recent(std::size_t max = 100);

        // Le module est-il actif ?
        static bool IsEnabled();

        // Renvoie le chemin du fichier de log.
        static std::filesystem::path LogFilePath();

        // Renvoie la config de rotation courante.
        static RotateConfig GetRotateConfig();

        // Override la config de rotation (utile pour les tests).
        static void SetRotateConfig(const RotateConfig& cfg);

        // Force une rotation maintenant (renvoie true si quelque chose
        // a été tourné). Utile pour /audit rotate ou les tests.
        static bool RotateIfNeeded();

    private:
        AuditLog() = default;

        static AuditLog& Instance();

        void InitializeImpl();
        void LoadFromConfigImpl(const nlohmann::json& config);
        void LogImpl(std::string_view action,
                     PlayerId playerId,
                     nlohmann::json payload,
                     std::string_view severity);
        void FlushImpl();
        void FlushImplLocked();  // suppose mutex_ déjà tenu
        void ShutdownImpl();
        bool RotateIfNeededImpl();  // suppose mutex_ déjà tenu
        std::filesystem::path MakeRotatedPath(int n) const;

        mutable std::mutex                  mutex_;
        std::deque<Entry>                   buffer_;
        std::size_t                         maxBuffer_  = 1000;
        std::size_t                         flushEvery_ = 50;
        bool                                enabled_    = true;
        bool                                initialized_ = false;
        std::filesystem::path               filePath_;
        RotateConfig                        rotateConfig_;
        std::deque<Entry>                   recent_;
    };
}

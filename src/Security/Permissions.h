// ============================================================================
// RPFramework - Security / Permissions
//
// 6 niveaux hiérarchiques (GDD §18) :
//     PLAYER < MODERATOR < GM < ADMIN < OWNER < SYSTEM
//
// Chaque action sensible du framework est identifiée par une clé string
// (ex: "race.select", "quest.create", "config.reload") et associée à un
// niveau minimum requis. Les défauts sont baked-in ; le propriétaire du
// serveur peut surcharger dans config.json → security.permissions.
//
// IMPORTANT (GDD §18) : les permissions sont vérifiées CÔTÉ SERVEUR.
// Le client ne décide jamais de son propre niveau.
// ============================================================================
#pragma once

#include "json.hpp"

#include "Security/Types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rpframework::security
{
    // -------------------------------------------------------------------------
    // Niveaux de permission.
    // -------------------------------------------------------------------------
    enum class Level : std::uint8_t
    {
        PLAYER    = 0,
        MODERATOR = 1,
        GM       = 2,
        ADMIN    = 3,
        OWNER    = 4,
        SYSTEM   = 5,  // réservé au code interne (jamais un joueur)
    };

    // Helpers de conversion (sérialisation, logs, config).
    constexpr std::string_view LevelToString(Level l) noexcept
    {
        switch (l)
        {
            case Level::PLAYER:    return "PLAYER";
            case Level::MODERATOR: return "MODERATOR";
            case Level::GM:        return "GM";
            case Level::ADMIN:     return "ADMIN";
            case Level::OWNER:     return "OWNER";
            case Level::SYSTEM:    return "SYSTEM";
        }
        return "UNKNOWN";
    }

    Level LevelFromString(std::string_view s, Level fallback = Level::PLAYER) noexcept;

    // -------------------------------------------------------------------------
    // Gestionnaire de permissions (singleton, thread-safe).
    // -------------------------------------------------------------------------
    class Permissions
    {
    public:
        // Initialise les défauts baked-in. Appelé une fois par PluginContext.
        static void Initialize();

        // Recharge les overrides depuis config.security.permissions.
        // Format JSON :
        //   "permissions": {
        //       "race.select":   "PLAYER",
        //       "quest.create":  "ADMIN",
        //       "config.reload": "OWNER"
        //   }
        static void LoadFromConfig(const nlohmann::json& config);

        // Enregistre (ou override) le niveau minimum pour une clé.
        // Utile pour les modules Phase 4+ qui veulent déclarer leurs
        // propres clés au démarrage.
        static void Register(std::string_view key, Level minimum);

        // Renvoie le niveau minimum requis pour une clé. Si la clé est
        // inconnue, retourne le `defaultForUnknown` (par défaut PLAYER :
        // on n'invente pas de permissions manquantes).
        static Level GetRequiredLevel(std::string_view key,
                                      Level defaultForUnknown = Level::PLAYER);

        // Check rapide : `playerLevel >= requiredLevel(key)` ?
        static bool Check(Level playerLevel, std::string_view key);

        // Renvoie toutes les clés connues (lecture seule, pour /debug).
        static std::unordered_map<std::string, Level> Snapshot();

    private:
        Permissions() = default;

        static Permissions& Instance();
        void RegisterImpl(std::string_view key, Level minimum);
        Level GetRequiredLevelImpl(std::string_view key, Level fallback) const;

        mutable std::mutex mutex_;
        std::unordered_map<std::string, Level> table_;
        bool initialized_ = false;
    };
}

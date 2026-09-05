// ============================================================================
// RPFramework - Security / RateLimiter
//
// Cooldown / rate limiting par (joueur, action). Implémentation "sliding
// window" : pour chaque clé, on garde un deque de timestamps des N derniers
// événements ; on drop ceux hors fenêtre et on compare au max.
//
// Config (config.json → security.rate_limits) :
//   "rate_limits": {
//       "race.select":    { "max": 1,  "window_sec": 86400 },
//       "quest.complete": { "max": 5,  "window_sec": 60    },
//       "chat.global":    { "max": 30, "window_sec": 60    }
//   }
//
// Si une clé n'est pas configurée, elle n'est PAS rate-limited (Allow
// renvoie toujours true). On n'invente pas de limites qui n'existent pas.
// ============================================================================
#pragma once

#include "json.hpp"

#include "Security/Types.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rpframework::security
{
    class RateLimiter
    {
    public:
        struct Limit
        {
            int max       = 0;  // nombre max d'événements
            int windowSec = 0;  // sur cette fenêtre glissante
        };

        // Initialise la table avec les défauts baked-in (vide en Phase 2 ;
        // chaque module Phase 4+ enregistrera ses propres limites).
        static void Initialize();

        // Recharge les limites depuis config.security.rate_limits.
        // Les clés inconnues dans le JSON qui n'ont pas de défaut sont
        // ajoutées à la volée.
        static void LoadFromConfig(const nlohmann::json& config);

        // Enregistre (ou override) la limite pour une action. `max=0`
        // signifie "pas de limite" (la clé est ignorée).
        static void Register(std::string_view action, Limit limit);

        // Renvoie la limite configurée pour `action`. Si non configurée,
        // renvoie {0,0} (pas de limite).
        static Limit GetLimit(std::string_view action);

        // Check + record atomique. Renvoie true si l'événement est
        // autorisé, false si le joueur a dépassé la limite.
        // Thread-safe.
        static bool Allow(PlayerId player, std::string_view action);

        // Reset complet d'un (player, action).
        static void Reset(PlayerId player, std::string_view action);

        // Reset complet d'un joueur (toutes actions).
        static void ResetPlayer(PlayerId player);

        // Nettoyage des entrées expirées (à appeler périodiquement pour
        // éviter que la map ne grossisse indéfiniment avec des joueurs
        // déconnectés). Renvoie le nombre d'entrées supprimées.
        static std::size_t Cleanup();

        // Renvoie, pour une clé (player, action), le nombre d'événements
        // encore comptés dans la fenêtre. Utile pour /debug.
        static int Count(PlayerId player, std::string_view action);

        // Délai restant avant la prochaine fenêtre disponible, en
        // secondes (0 si pas rate-limited).
        static int SecondsUntilNext(PlayerId player, std::string_view action);

    private:
        RateLimiter() = default;

        using Clock     = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct PlayerKey
        {
            PlayerId     player;
            std::string  action;

            bool operator==(const PlayerKey& other) const noexcept
            {
                return player == other.player && action == other.action;
            }
        };

        struct PlayerKeyHash
        {
            std::size_t operator()(const PlayerKey& k) const noexcept
            {
                return std::hash<std::uint64_t>{}(k.player)
                     ^ (std::hash<std::string>{}(k.action) << 1);
            }
        };

        struct Bucket
        {
            std::deque<TimePoint> timestamps;
        };

        static RateLimiter& Instance();

        Limit GetLimitImpl(std::string_view action) const;  // mutex tenu
        bool  AllowImpl  (PlayerId player, std::string_view action);

        mutable std::mutex mutex_;
        std::unordered_map<std::string, Limit>                limits_;
        std::unordered_map<PlayerKey, Bucket, PlayerKeyHash>  buckets_;
    };
}

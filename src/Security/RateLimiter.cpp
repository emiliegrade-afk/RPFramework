// ============================================================================
// RPFramework - Security / RateLimiter - implémentation
// ============================================================================
#include "Security/RateLimiter.h"
#include "Core/Logger.h"

#include <algorithm>

namespace rpframework::security
{
    RateLimiter& RateLimiter::Instance()
    {
        static RateLimiter inst;
        return inst;
    }

    void RateLimiter::Initialize()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (self.limits_.empty())
        {
            rpframework::core::LogInfo("RateLimiter: pret (aucune limite par defaut ; declarer via config ou Register()).");
        }
    }

    void RateLimiter::LoadFromConfig(const nlohmann::json& config)
    {
        if (!config.is_object())
        {
            return;
        }
        const auto it = config.find("rate_limits");
        if (it == config.end() || !it->is_object())
        {
            return;
        }

        int loaded = 0;
        for (auto kv = it->begin(); kv != it->end(); ++kv)
        {
            if (!kv->is_object())
            {
                rpframework::core::LogWarn("RateLimiter: cle '{}' ignoree (valeur non-objet).", kv.key());
                continue;
            }
            const auto& j = *kv;
            Limit lim;
            lim.max       = j.value("max",        0);
            lim.windowSec = j.value("window_sec", 0);

            if (lim.max < 0)    lim.max = 0;
            if (lim.windowSec < 0) lim.windowSec = 0;

            Instance().Register(kv.key(), lim);
            ++loaded;
        }
        rpframework::core::LogInfo("RateLimiter: {} limites chargees depuis la config.", loaded);
    }

    void RateLimiter::Register(std::string_view action, Limit limit)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (limit.max <= 0 || limit.windowSec <= 0)
        {
            // max=0 = pas de limite, on retire la clé si elle existe.
            self.limits_.erase(std::string(action));
        }
        else
        {
            self.limits_[std::string(action)] = limit;
        }
    }

    RateLimiter::Limit RateLimiter::GetLimit(std::string_view action)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.GetLimitImpl(action);
    }

    RateLimiter::Limit RateLimiter::GetLimitImpl(std::string_view action) const
    {
        const auto it = limits_.find(std::string(action));
        if (it == limits_.end())
        {
            return Limit{};  // pas de limite
        }
        return it->second;
    }

    bool RateLimiter::Allow(PlayerId player, std::string_view action)
    {
        return Instance().AllowImpl(player, action);
    }

    bool RateLimiter::AllowImpl(PlayerId player, std::string_view action)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const Limit lim = GetLimitImpl(action);
        if (lim.max <= 0 || lim.windowSec <= 0)
        {
            return true;  // pas de limite configurée
        }

        const auto now    = Clock::now();
        const auto window = std::chrono::seconds(lim.windowSec);
        const auto cutoff = now - window;

        const PlayerKey key{ player, std::string(action) };
        auto& bucket = buckets_[key];

        // Drop timestamps expirés.
        while (!bucket.timestamps.empty() && bucket.timestamps.front() < cutoff)
        {
            bucket.timestamps.pop_front();
        }

        if (static_cast<int>(bucket.timestamps.size()) >= lim.max)
        {
            return false;  // rate limited
        }

        bucket.timestamps.push_back(now);
        return true;
    }

    void RateLimiter::Reset(PlayerId player, std::string_view action)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.buckets_.erase(PlayerKey{ player, std::string(action) });
    }

    void RateLimiter::ResetPlayer(PlayerId player)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        for (auto it = self.buckets_.begin(); it != self.buckets_.end(); )
        {
            if (it->first.player == player)
            {
                it = self.buckets_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::size_t RateLimiter::Cleanup()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        const auto now = Clock::now();
        std::size_t removed = 0;

        for (auto it = self.buckets_.begin(); it != self.buckets_.end(); )
        {
            const auto& lim = self.GetLimitImpl(it->first.action);
            if (lim.windowSec <= 0)
            {
                ++it;
                continue;
            }
            const auto cutoff = now - std::chrono::seconds(lim.windowSec);
            while (!it->second.timestamps.empty() && it->second.timestamps.front() < cutoff)
            {
                it->second.timestamps.pop_front();
            }
            if (it->second.timestamps.empty())
            {
                it = self.buckets_.erase(it);
                ++removed;
            }
            else
            {
                ++it;
            }
        }
        return removed;
    }

    int RateLimiter::Count(PlayerId player, std::string_view action)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        const Limit lim = self.GetLimitImpl(action);
        if (lim.max <= 0 || lim.windowSec <= 0)
        {
            return 0;
        }

        const auto now    = Clock::now();
        const auto cutoff = now - std::chrono::seconds(lim.windowSec);

        const auto it = self.buckets_.find(PlayerKey{ player, std::string(action) });
        if (it == self.buckets_.end())
        {
            return 0;
        }

        int count = 0;
        for (const auto& t : it->second.timestamps)
        {
            if (t >= cutoff) ++count;
        }
        return count;
    }

    int RateLimiter::SecondsUntilNext(PlayerId player, std::string_view action)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        const Limit lim = self.GetLimitImpl(action);
        if (lim.max <= 0 || lim.windowSec <= 0)
        {
            return 0;
        }

        const auto it = self.buckets_.find(PlayerKey{ player, std::string(action) });
        if (it == self.buckets_.end() || it->second.timestamps.size() < static_cast<std::size_t>(lim.max))
        {
            return 0;
        }

        const auto& oldest = it->second.timestamps.front();
        const auto expiresAt = oldest + std::chrono::seconds(lim.windowSec);
        const auto now = Clock::now();
        if (expiresAt <= now)
        {
            return 0;
        }
        return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(expiresAt - now).count());
    }
}

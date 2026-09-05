// ============================================================================
// RPFramework - Security / Permissions - implémentation
// ============================================================================
#include "Security/Permissions.h"
#include "Core/Logger.h"

#include <iterator>

namespace rpframework::security
{
    Level LevelFromString(std::string_view s, Level fallback) noexcept
    {
        if (s == "PLAYER")    return Level::PLAYER;
        if (s == "MODERATOR") return Level::MODERATOR;
        if (s == "GM")        return Level::GM;
        if (s == "ADMIN")     return Level::ADMIN;
        if (s == "OWNER")     return Level::OWNER;
        if (s == "SYSTEM")    return Level::SYSTEM;
        return fallback;
    }

    Permissions& Permissions::Instance()
    {
        static Permissions inst;
        return inst;
    }

    // -------------------------------------------------------------------------
    // Défauts baked-in : le socle de sécurité est en place dès Phase 2, avant
    // même que les modules Character/Quest existent. Les modules métier
    // enregistreront leurs propres clés en s'initialisant.
    // -------------------------------------------------------------------------
    namespace
    {
        struct DefaultPermission
        {
            std::string_view key;
            Level            level;
        };

        constexpr DefaultPermission kDefaults[] = {
            // Framework / Core
            { "framework.info",          Level::PLAYER    },
            { "framework.config.view",  Level::MODERATOR },
            { "framework.config.edit",  Level::OWNER     },
            { "framework.reload",       Level::OWNER     },

            // Character (les modules Phase 4 enregistreront aussi leurs clés,
            // ces défauts restent utiles en cas d'accès direct anticipé).
            { "character.view.self",    Level::PLAYER    },
            { "character.view.other",   Level::MODERATOR },
            { "character.edit.self",    Level::PLAYER    },
            { "character.edit.other",   Level::GM        },
            { "race.select",            Level::PLAYER    },
            { "profession.select",      Level::PLAYER    },
            { "class.select",           Level::PLAYER    },

            // Quest
            { "quest.list",             Level::PLAYER    },
            { "quest.accept",           Level::PLAYER    },
            { "quest.complete",         Level::PLAYER    },
            { "quest.create",           Level::ADMIN     },
            { "quest.delete",           Level::ADMIN     },

            // Faction / Reputation
            { "faction.view",           Level::PLAYER    },
            { "faction.join",           Level::PLAYER    },
            { "faction.leave",          Level::PLAYER    },
            { "faction.create",         Level::ADMIN     },
            { "faction.modify_reputation", Level::GM      },
            { "reputation.view",        Level::PLAYER    },
            { "reputation.edit",        Level::GM        },

            // Economy
            { "economy.view",           Level::PLAYER    },
            { "economy.transfer",       Level::PLAYER    },
            { "economy.grant",          Level::GM        },
            { "economy.add",            Level::SYSTEM    },
            { "economy.subtract",       Level::SYSTEM    },
            { "economy.reward",         Level::SYSTEM    },

            // Audit / Security
            { "audit.view",             Level::MODERATOR },
            { "audit.flush",            Level::ADMIN     },
        };
    }

    void Permissions::Initialize()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        if (self.initialized_)
        {
            return;
        }

        // On utilise try_emplace pour que les éventuelles entrées déjà
        // chargées (ex: LoadFromConfig appelée avant Initialize par un
        // module externe) ne soient PAS écrasées par les défauts.
        // C'est l'ordre-indépendance : Initialize() est safe à appeler
        // avant ou après LoadFromConfig().
        for (const auto& d : kDefaults)
        {
            self.table_.try_emplace(std::string(d.key), d.level);
        }
        self.initialized_ = true;

        rpframework::core::LogInfo("Permissions: {} entrees par defaut enregistrees (table complete : {}).",
            std::size(kDefaults), self.table_.size());
    }

    void Permissions::LoadFromConfig(const nlohmann::json& config)
    {
        if (!config.is_object())
        {
            return;
        }
        const auto it = config.find("permissions");
        if (it == config.end() || !it->is_object())
        {
            return;
        }

        int overrides = 0;
        for (auto it2 = it->begin(); it2 != it->end(); ++it2)
        {
            if (!it2->is_string())
            {
                rpframework::core::LogWarn("Permissions: cle '{}' ignoree (valeur non-string).", it2.key());
                continue;
            }
            const Level lvl = LevelFromString(it2->get<std::string>(), Level::PLAYER);
            Instance().RegisterImpl(it2.key(), lvl);
            ++overrides;
        }
        rpframework::core::LogInfo("Permissions: {} overrides config charges.", overrides);
    }

    void Permissions::Register(std::string_view key, Level minimum)
    {
        Instance().RegisterImpl(key, minimum);
    }

    void Permissions::RegisterImpl(std::string_view key, Level minimum)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        table_[std::string(key)] = minimum;
    }

    Level Permissions::GetRequiredLevelImpl(std::string_view key, Level fallback) const
    {
        // mutex_ doit être tenu par l'appelant.
        const auto it = table_.find(std::string(key));
        if (it == table_.end())
        {
            return fallback;
        }
        return it->second;
    }

    Level Permissions::GetRequiredLevel(std::string_view key, Level defaultForUnknown)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.GetRequiredLevelImpl(key, defaultForUnknown);
    }

    bool Permissions::Check(Level playerLevel, std::string_view key)
    {
        const Level required = GetRequiredLevel(key);
        return static_cast<std::uint8_t>(playerLevel)
             >= static_cast<std::uint8_t>(required);
    }

    std::unordered_map<std::string, Level> Permissions::Snapshot()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.table_;
    }
}

// ============================================================================
// RPFramework - Security / Permissions - implémentation
// ============================================================================
#include "Security/Permissions.h"
#include "Core/Logger.h"
#include "Core/Paths.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>

namespace rpframework::security
{
    namespace
    {
        std::optional<Level> TryParseLevel(std::string_view s)
        {
            std::string upper(s);
            for (char& c : upper)
            {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            if (upper == "PLAYER")    return Level::PLAYER;
            if (upper == "MODERATOR") return Level::MODERATOR;
            if (upper == "GM")        return Level::GM;
            if (upper == "ADMIN")     return Level::ADMIN;
            if (upper == "OWNER")     return Level::OWNER;
            if (upper == "SYSTEM")    return Level::SYSTEM;
            return std::nullopt;
        }
    }

    Level LevelFromString(std::string_view s, Level fallback) noexcept
    {
        const auto parsed = TryParseLevel(s);
        return parsed.value_or(fallback);
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
            { "framework.config.edit",  Level::MODERATOR },
            { "framework.reload",       Level::OWNER     },

            // Chat (pipeline Security avant envoi vanilla)
            { "chat.send",              Level::PLAYER    },

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
        if (it != config.end() && it->is_object())
        {
            int overrides = 0;
            for (auto it2 = it->begin(); it2 != it->end(); ++it2)
            {
                if (!it2->is_string())
                {
                    rpframework::core::LogWarn("Permissions: cle '{}' ignoree (valeur non-string).", it2.key());
                    continue;
                }
                const auto parsed = TryParseLevel(it2->get<std::string>());
                if (!parsed.has_value())
                {
                    rpframework::core::LogWarn("Permissions: cle '{}' ignoree (niveau invalide).", it2.key());
                    continue;
                }
                std::string key = it2.key();
                if (key == "config.reload")
                {
                    key = "framework.reload";
                }
                Instance().RegisterImpl(key, *parsed);
                ++overrides;
            }
            rpframework::core::LogInfo("Permissions: {} overrides config charges.", overrides);
        }

        // Niveaux individuels des joueurs. Les clés de config sont les
        // identifiants réseau (SteamID/EOS) ; on les hashe en PlayerId comme
        // le font les hooks AsaApi (FStringToUtf8 + MakePlayerId), ce qui
        // garantit que les lookups runtime collent à ce qui est chargé ici.
        const auto itPlayers = config.find("player_levels");
        if (itPlayers != config.end() && itPlayers->is_object())
        {
            auto& self = Instance();
            std::lock_guard<std::mutex> lock(self.mutex_);
            self.playerLevels_.clear();
            int loaded = 0;
            for (auto p = itPlayers->begin(); p != itPlayers->end(); ++p)
            {
                if (!p->is_string())
                {
                    rpframework::core::LogWarn("Permissions: player_levels['{}'] ignore (valeur non-string).", p.key());
                    continue;
                }
                const auto parsed = TryParseLevel(p->get<std::string>());
                if (!parsed.has_value())
                {
                    rpframework::core::LogWarn("Permissions: player_levels['{}'] ignore (niveau invalide).", p.key());
                    continue;
                }
                Level lvl = *parsed;
                if (lvl == Level::SYSTEM)
                {
                    rpframework::core::LogWarn(
                        "Permissions: player_levels['{}'] SYSTEM refuse, OWNER applique.", p.key());
                    lvl = Level::OWNER;
                }
                self.playerLevels_[MakePlayerId(p.key())] = lvl;
                ++loaded;
            }
            rpframework::core::LogInfo("Permissions: {} niveaux joueurs charges.", loaded);
        }

        if (config.contains("owner_on_first_join") && config["owner_on_first_join"].is_boolean())
        {
            Instance().bootstrapOwner_ = config["owner_on_first_join"].get<bool>();
        }

#ifndef RPFRAMEWORK_TESTS
        {
            auto& self = Instance();
            std::lock_guard<std::mutex> lock(self.mutex_);
            if (self.playerLevels_.empty())
            {
                const auto path = rpframework::core::GetPluginDir() / "owner.json";
                std::ifstream in(path);
                if (in)
                {
                    try
                    {
                        nlohmann::json stored;
                        in >> stored;
                        if (stored.contains("player_id") && stored["player_id"].is_number_unsigned())
                        {
                            self.playerLevels_[stored["player_id"].get<PlayerId>()] = Level::OWNER;
                            rpframework::core::LogInfo("Permissions: OWNER restaure depuis owner.json.");
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
#endif
    }

    bool Permissions::TryBootstrapOwner(PlayerId player)
    {
        if (player == 0) return false;
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (!self.bootstrapOwner_) return false;
        for (const auto& [id, level] : self.playerLevels_)
        {
            (void)id;
            if (static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(Level::OWNER))
                return false;
        }
        self.playerLevels_[player] = Level::OWNER;
#ifndef RPFRAMEWORK_TESTS
        try
        {
            const auto dir = rpframework::core::GetPluginDir();
            rpframework::core::EnsureDirectoryExists(dir);
            const auto path = dir / "owner.json";
            std::ofstream out(path, std::ios::trunc);
            if (out)
            {
                nlohmann::json stored;
                stored["player_id"] = player;
                out << stored.dump(2);
            }
        }
        catch (...)
        {
        }
#endif
        rpframework::core::LogInfo("Permissions: joueur {} promu OWNER (premier join).", player);
        return true;
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

    Level Permissions::GetPlayerLevel(PlayerId player)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        const auto it = self.playerLevels_.find(player);
        return (it != self.playerLevels_.end()) ? it->second : Level::PLAYER;
    }

    bool Permissions::CheckFor(PlayerId player, std::string_view key)
    {
        return Check(GetPlayerLevel(player), key);
    }

    std::unordered_map<std::string, Level> Permissions::Snapshot()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.table_;
    }
}

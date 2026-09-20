// ============================================================================
// RPFramework - Core / PluginContext - implémentation
// ============================================================================
#include "Core/PluginContext.h"
#include "Core/Config.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Version.h"

#include "Character/Registry.h"
#include "Faction/Registry.h"
#include "Economy/Registry.h"
#include "Quest/Registry.h"
#include "Crafting/Registry.h"
#include "Effects/Registry.h"
#include "Data/PlayerStore.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include "API/ARK/Ark.h"

namespace rpframework::core
{
    bool PluginContext::initialized_ = false;
    PluginContext::State PluginContext::state_ = PluginContext::State::Uninitialized;

    bool PluginContext::Initialize()
    {
        if (initialized_)
        {
            LogWarn("PluginContext::Initialize() appelé deux fois - ignoré.");
            return true;
        }

        state_ = State::Initializing;

        // 1. Logger (AsaApi gère le spdlog global). En bootstrap/test, le
        // logger AsaApi peut ne pas être prêt : on ne doit pas crasher.
        try
        {
            Log::Get().Init(std::string(kFrameworkName));
            LogStartupBanner();
            LogInfo("Initialize() : démarrage.");
        }
        catch (...) {
            // Le framework continue en mode silencieux si le logger n'est pas
            // disponible dans l'environnement d'exécution courant.
        }

        // 2. Configuration.
        EnsureDirectoryExists(GetPluginDir());
        const auto configPath = GetPluginConfigPath();
        Config::Get().LoadFromFile(configPath);

        // 3. Security : ordre important.
        //    - Permissions : doit être init avant les modules métier.
        //    - RateLimiter  : pas de défaut, lit la config directement.
        //    - AuditLog     : lit sa config, crée le fichier de log.
        security::Permissions::Initialize();
        security::RateLimiter::Initialize();
        security::AuditLog::Initialize();

        const auto& cfg = Config::Get().Root();
        if (cfg.is_object() && cfg.contains("security"))
        {
            const auto& sec = cfg["security"];
            security::Permissions::LoadFromConfig(sec);
            security::RateLimiter::LoadFromConfig(sec);
            security::AuditLog::LoadFromConfig(sec);
        }

        // 4. Data : persistance joueur. Doit venir après l'audit
        //    (peut écrire des entrées d'audit au load).
        data::PlayerStore::Initialize();
        if (!data::PlayerStore::IsReady())
        {
            state_ = State::Failed;
            LogError("Initialize() : PlayerStore indisponible - plugin en échec.");
            return false;
        }

        // 5. Character : registry des races/métiers/classes. Lit la
        //    config; aucune sélection n'est faite à l'init.
        character::Registry::Initialize();

        // 6. Faction : registry des factions. Lit la config ; aucune
        //    adhésion n'est faite à l'init.
        faction::Registry::Initialize();

        // 7. Economy : registry des monnaies. Lit la config ; aucune
        //    transaction n'est faite à l'init.
        economy::Registry::Initialize();

        // Quest definitions depend on Data, Security and Economy being ready.
        quest::Registry::Initialize();
        crafting::Registry::Initialize();
        effects::Registry::Initialize();

        security::AuditLog::Log("framework.init", 0, {
            {"version", std::string(kFrameworkName) + " " + GetVersionString()},
            {"config_schema", GetConfigSchemaVersionString()},
            {"known_players", data::PlayerStore::ListAll().size()},
            {"classes_enabled", character::Registry::ClassesEnabled()},
        });

        // 5. Marque initialized_ AVANT l'enregistrement des hooks.
        initialized_ = true;
        state_ = State::Ready;

        LogInfo("Initialize() : OK. PluginContext prêt.");
        return true;
    }

    bool PluginContext::ReloadConfig()
    {
        if (!initialized_ || state_ != State::Ready)
            return false;
        if (!Config::Get().Reload())
            return false;

        const auto& cfg = Config::Get().Root();
        if (cfg.is_object() && cfg.contains("security"))
        {
            const auto& sec = cfg["security"];
            security::Permissions::LoadFromConfig(sec);
            security::RateLimiter::LoadFromConfig(sec);
            security::AuditLog::LoadFromConfig(sec);
        }
        data::PlayerStore::LoadFromConfig();
        character::Registry::LoadFromConfig();
        faction::Registry::LoadFromConfig();
        economy::Registry::LoadFromConfig();
        quest::Registry::LoadFromConfig();
        crafting::Registry::LoadFromConfig();
        effects::Registry::LoadFromConfig();
        security::AuditLog::Log("framework.config.reload", 0);
        return true;
    }

    bool PluginContext::ApplyLiveConfig()
    {
        const auto cfg = Config::Get().Root();
        if (cfg.is_object() && cfg.contains("security"))
        {
            const auto& sec = cfg["security"];
            security::Permissions::LoadFromConfig(sec);
            security::RateLimiter::LoadFromConfig(sec);
            security::AuditLog::LoadFromConfig(sec);
        }
        character::Registry::LoadFromConfig();
        faction::Registry::LoadFromConfig();
        economy::Registry::LoadFromConfig();
        quest::Registry::LoadFromConfig();
        crafting::Registry::LoadFromConfig();
        effects::Registry::LoadFromConfig();
        security::AuditLog::Log("framework.config.apply", 0);
        return true;
    }

    bool PluginContext::SaveAndApply()
    {
        const auto& path = Config::Get().SourcePath();
        if (!path.empty() && !Config::Get().SaveToFile(path))
            return false;
        return ApplyLiveConfig();
    }

    void PluginContext::Shutdown()
    {
        if (!initialized_ && state_ == State::Uninitialized)
        {
            return;
        }

        state_ = State::ShuttingDown;
        LogInfo("Shutdown() : arrêt du framework.");

        security::AuditLog::Log("framework.shutdown", 0);
        effects::Registry::Shutdown();
        crafting::Registry::Shutdown();
        quest::Registry::Shutdown();
        economy::Registry::Shutdown();
        faction::Registry::Shutdown();
        character::Registry::Shutdown();
        data::PlayerStore::Shutdown();        // flush final (rien à faire, mais cohérent)
        security::AuditLog::Shutdown();       // flush final
        // (RateLimiter et Permissions sont stateless, rien à faire.)

        initialized_ = false;
        state_ = State::Uninitialized;
        LogInfo("Shutdown() : OK.");
    }

    bool PluginContext::IsInitialized()
    {
        return initialized_;
    }

    PluginContext::State PluginContext::GetState()
    {
        return state_;
    }
}

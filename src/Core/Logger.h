// ============================================================================
// RPFramework - Core / Logger
//
// Façade typée au-dessus du logger AsaApi (spdlog).
//
// Le pattern AsaApi inclut déjà `[<nom-du-plugin>]` dans chaque ligne de log
// (via le token `%n` du pattern), donc on n'ajoute PAS de préfixe manuel :
// grep `[RPFramework]` dans ArkApi.log capture déjà tout ce qui vient du
// framework. Les fonctions du framework n'apportent que :
//   - un point d'appel unique (refactor-safe) ;
//   - la convention de nommage (LogInfo / LogWarn / LogError / LogDebug) ;
//   - la garantie noexcept : un échec de log ne doit jamais crasher le
//     serveur.
//
// On prend un `const char*` literal comme premier argument. C'est ce que
// spdlog supporte en mode runtime (équivalent à `fmt::runtime` pour les
// littéraux). Pour un message dynamique, faire `LogInfo("{}", ma_string)`.
// ============================================================================
#pragma once

#include "API/ARK/Ark.h"
#include "Core/Version.h"

#include <string>
#include <utility>

namespace rpframework::core
{
    // -------------------------------------------------------------------------
    // Accès brut au logger AsaApi (pour usages avancés).
    // -------------------------------------------------------------------------
    inline std::shared_ptr<spdlog::logger> GetRawLogger()
    {
        return Log::GetLog();
    }

    // -------------------------------------------------------------------------
    // API publique - tout est noexcept : un échec de log ne doit jamais
    // remonter et crasher le serveur.
    // -------------------------------------------------------------------------

    template <typename... Args>
    void LogInfo(const char* fmt, Args&&... args) noexcept
    {
        if (auto lg = Log::GetLog())
        {
            lg->info(fmt, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void LogWarn(const char* fmt, Args&&... args) noexcept
    {
        if (auto lg = Log::GetLog())
        {
            lg->warn(fmt, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void LogError(const char* fmt, Args&&... args) noexcept
    {
        if (auto lg = Log::GetLog())
        {
            lg->error(fmt, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void LogDebug(const char* fmt, Args&&... args) noexcept
    {
        if (auto lg = Log::GetLog())
        {
            lg->debug(fmt, std::forward<Args>(args)...);
        }
    }

    // -------------------------------------------------------------------------
    // Bannière de démarrage (appelée depuis PluginContext::Initialize()).
    // -------------------------------------------------------------------------
    void LogStartupBanner();
}

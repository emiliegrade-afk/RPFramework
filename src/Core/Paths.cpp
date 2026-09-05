// ============================================================================
// RPFramework - Core / Paths - implémentation
// ============================================================================
#include "Core/Paths.h"
#include "Core/Version.h"

#include "API/ARK/Ark.h"

#include <system_error>

namespace rpframework::core
{
    namespace
    {
        // Cache local du root serveur (lookup peu coûteux mais appelé souvent).
        std::filesystem::path& ServerRootCache()
        {
            static std::filesystem::path cache;
            return cache;
        }

        std::filesystem::path ResolveServerRoot()
        {
            auto& cache = ServerRootCache();
            if (cache.empty())
            {
                try
                {
                    cache = std::filesystem::path(API::Tools::GetCurrentDir());
                }
                catch (...)
                {
                    cache = std::filesystem::current_path();
                }
            }
            return cache;
        }
    }

    std::filesystem::path GetServerRoot()
    {
        return ResolveServerRoot();
    }

    std::filesystem::path GetAsaApiDir()
    {
        return ResolveServerRoot() / "ArkApi";
    }

    std::filesystem::path GetPluginDir()
    {
        return GetAsaApiDir() / "Plugins" / std::string(kFrameworkName);
    }

    std::filesystem::path GetPluginConfigPath()
    {
        return GetPluginDir() / "config.json";
    }

    std::filesystem::path GetFrameworkLogPath()
    {
        return GetPluginDir() / "logs" / "framework.log";
    }

    std::filesystem::path GetAuditLogPath()
    {
        return GetPluginDir() / "logs" / "audit.log";
    }

    bool EnsureDirectoryExists(const std::filesystem::path& dir)
    {
        if (dir.empty())
        {
            return false;
        }
        std::error_code ec;
        if (std::filesystem::exists(dir, ec))
        {
            return std::filesystem::is_directory(dir, ec);
        }
        std::filesystem::create_directories(dir, ec);
        return !ec && std::filesystem::is_directory(dir, ec);
    }
}

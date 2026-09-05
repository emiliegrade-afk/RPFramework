// ============================================================================
// RPFramework - Core / Logger - implémentation
// ============================================================================
#include "Core/Logger.h"

namespace rpframework::core
{
    void LogStartupBanner()
    {
        if (auto lg = Log::GetLog())
        {
            lg->info("================================================================");
            lg->info("  {} v{}", std::string(kFrameworkName), GetVersionString());
            lg->info("  Framework RPG/RP générique pour ARK: Survival Ascended");
            lg->info("  Schéma de config : v{}", GetConfigSchemaVersionString());
            lg->info("================================================================");
        }
    }
}

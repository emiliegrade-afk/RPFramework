// ============================================================================
// RPFramework - Faction / Registry
//
// Charge les définitions de factions depuis `config.factions.*` au boot.
// Data-driven : aucune faction baked-in. Le serveur peut définir autant
// de factions qu'il le souhaite via la config.
// ============================================================================
#pragma once

#include "Faction/Definitions.h"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rpframework::faction
{
    class Registry
    {
    public:
        // Cycle de vie ---------------------------------------------------------
        static void Initialize();
        static void Shutdown();
        // Recharge la section `config.factions.*`. Idempotent.
        static void LoadFromConfig();
        // Variante directe (tests) : charge depuis un sous-objet.
        static void LoadDefinitionsFromSection(const nlohmann::json* section);

        // API -----------------------------------------------------------------
        static bool                       HasFaction(const std::string& id);
        static std::optional<Faction>     GetFaction(const std::string& id);
        static std::vector<Faction>       ListFactions();
        static std::vector<std::string>   ListFactionIds();

        // Tests / admin : reset complet. --------------------------------
        static void ResetForTests();

    private:
        Registry() = default;
        static Registry& Instance();
        void LoadFromConfigImpl();

        mutable std::mutex                                mutex_;
        std::unordered_map<std::string, Faction>          factions_;
        bool                                               initialized_ = false;
    };
}

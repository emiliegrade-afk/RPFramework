// ============================================================================
// RPFramework - Economy / Registry
//
// Charge les définitions de monnaies depuis `config.economy.currencies.*`
// au boot. Data-driven : aucune monnaie baked-in. Le serveur peut définir
// autant de monnaies qu'il le souhaite via la config.
// ============================================================================
#pragma once

#include "Economy/Definitions.h"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rpframework::economy
{
    class Registry
    {
    public:
        // Cycle de vie ---------------------------------------------------------
        static void Initialize();
        static void Shutdown();
        // Recharge la section `config.economy.currencies.*`. Idempotent.
        static void LoadFromConfig();
        // Variante directe (tests) : charge depuis un sous-objet.
        static void LoadDefinitionsFromSection(const nlohmann::json* section);

        // API -----------------------------------------------------------------
        static bool                     HasCurrency(const std::string& id);
        static std::optional<Currency>  GetCurrency(const std::string& id);
        static std::vector<Currency>    ListCurrencies();
        static std::vector<std::string> ListCurrencyIds();

        // Tests / admin : reset complet.
        static void ResetForTests();

    private:
        Registry() = default;
        static Registry& Instance();
        void LoadFromConfigImpl();

        mutable std::mutex                                mutex_;
        std::unordered_map<std::string, Currency>          currencies_;
        bool                                               initialized_ = false;
    };
}

// ============================================================================
// RPFramework - Economy / Definitions
//
// Types de données configurables pour le système économique (GDD §14).
// Le framework ne fournit aucune monnaie baked-in : tout vient de
// `config.economy.currencies.*` (data-driven, comme Character/Faction).
//
// Une monnaie a :
//   - un id unique (clé, ex: "gold", "gem", "token_quest")
//   - un nom affiché
//   - un solde max optionnel (0 ou absent = illimité)
//   - un drapeau `transferable` (false = lié au compte, ex: points de loot)
//
// Le solde d'un joueur est stocké dans `PlayerData.wallets`
// (map<currency_id, int64>), persisté automatiquement par PlayerStore.
// ============================================================================
#pragma once

#include "json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rpframework::economy
{
    // Une monnaie du serveur.
    struct Currency
    {
        std::string id;             // unique, sert de clé
        std::string name;           // affiché
        std::string symbol;         // ex: "g", "💎", ""
        int64_t     maxBalance = 0; // 0 = illimité
        bool        transferable = true;  // false = non-échangeable entre joueurs

        nlohmann::json ToJson() const;
        static Currency FromJson(const std::string& idIn, const nlohmann::json& j);
    };
}

// ============================================================================
// RPFramework - Mod / Bridge
//
// Canal Mod → Plugin via commande console (GDD §48, chantier D1).
// Indépendant des offsets ARK : AsaApi enregistre "rpf" avec
// AddConsoleCommand. Les hooks vanilla restent hors de ce fichier.
//
// Ligne : rpf <module> <action> [args...]
// Testable hors serveur (pas d'include Ark.h).
// ============================================================================
#pragma once

#include "Security/Permissions.h"
#include "Security/Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace rpframework::mod
{
    using PlayerId = rpframework::security::PlayerId;

    inline constexpr std::string_view kConsoleCommand = "rpf";

    struct BridgeResult
    {
        bool        handled = false;
        bool        success = false;
        std::string message;
    };

    // Découpe une ligne en tokens. Guillemets doubles, échappement \",
    // chemins /Game/... (un seul token s'ils n'ont pas d'espace).
    std::vector<std::string> Tokenize(std::string_view input);

    // Enregistre les clés de permission / rate-limit du canal.
    void Initialize();
    void Shutdown();
    void ResetForTests();

    // Routeur pur. `line` peut inclure le préfixe "rpf".
    // Le niveau effectif = max(Permissions::GetPlayerLevel, session auth).
    BridgeResult Execute(PlayerId player, std::string_view line);
    BridgeResult Execute(PlayerId player, const std::vector<std::string>& tokens);

    bool IsSessionAdmin(PlayerId player);
    void ClearSessionAuth(PlayerId player);
}

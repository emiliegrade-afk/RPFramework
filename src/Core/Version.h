// ============================================================================
// RPFramework - Core / Version
//
// Versioning du framework. Header-only, constexpr.
// Source unique de vérité pour la version exposée dans les logs,
// l'audit log, et la future migration de données joueurs.
// ============================================================================
#pragma once

#include <string>
#include <string_view>

namespace rpframework::core
{
    // ---- Identité du framework ------------------------------------------------
    inline constexpr std::string_view kFrameworkName    = "RPFramework";
    inline constexpr std::string_view kFrameworkAuthor  = "RPFramework Contributors";

    // ---- Version sémantique ----------------------------------------------------
    inline constexpr int kVersionMajor = 0;
    inline constexpr int kVersionMinor = 1;
    inline constexpr int kVersionPatch = 0;
    inline constexpr std::string_view kVersionPreRelease = "";  // ex: "alpha", "rc1"

    // ---- Version du protocole de configuration (pour migrations futures) ------
    // Bumped quand le schéma de config.json devient incompatible.
    inline constexpr int kConfigSchemaVersion = 1;

    // ---- Helpers ---------------------------------------------------------------

    // Version courte "0.1.0".
    constexpr std::string_view GetVersionShort()
    {
        // Stocké en chaîne constexpr via return pour rester header-only.
        // Pour rester simple : reconstruit à la compilation via concat literal.
        return "0.1.0";
    }

    // Version complète, ex: "0.1.0-alpha" ou "0.1.0".
    std::string GetVersionString();

    // Version de schéma de config, sous forme de string ("1").
    constexpr std::string_view GetConfigSchemaVersionString()
    {
        return "1";
    }

    // Comparaison utile côté audit / debug.
    constexpr int GetVersionNumber()
    {
        return kVersionMajor * 10000 + kVersionMinor * 100 + kVersionPatch;
    }
}

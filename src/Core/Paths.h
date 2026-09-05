// ============================================================================
// RPFramework - Core / Paths
//
// Centralise tous les chemins filesystem utilisés par le framework.
// Header-only : ne fait que des lookups via l'API AsaApi existante.
// Toutes les fonctions sont noexcept et renvoient un chemin utilisable
// immédiatement (jamais d'exception même si un sous-dossier n'existe pas).
// ============================================================================
#pragma once

#include <filesystem>
#include <string>

namespace rpframework::core
{
    // -------------------------------------------------------------------------
    // Répertoires racines.
    // -------------------------------------------------------------------------

    // Racine du serveur ARK (équivalent de API::Tools::GetCurrentDir()).
    // Exemple : ".../ShooterGame/Binaries/Win64/"
    std::filesystem::path GetServerRoot();

    // Répertoire d'installation d'AsaApi côté serveur.
    // Exemple : ".../ShooterGame/Binaries/Win64/ArkApi/"
    std::filesystem::path GetAsaApiDir();

    // Répertoire d'installation de ce plugin (côté serveur).
    // Exemple : ".../ShooterGame/Binaries/Win64/ArkApi/Plugins/RPFramework/"
    std::filesystem::path GetPluginDir();

    // -------------------------------------------------------------------------
    // Fichiers du framework.
    // -------------------------------------------------------------------------

    // config.json du plugin.
    // Exemple : ".../ArkApi/Plugins/RPFramework/config.json"
    std::filesystem::path GetPluginConfigPath();

    // Fichier de log par défaut du framework (sous-plugin/framework.log).
    // Exemple : ".../ArkApi/Plugins/RPFramework/logs/framework.log"
    std::filesystem::path GetFrameworkLogPath();

    // Fichier d'audit log (Phase 2).
    // Exemple : ".../ArkApi/Plugins/RPFramework/logs/audit.log"
    std::filesystem::path GetAuditLogPath();

    // -------------------------------------------------------------------------
    // Helpers de manipulation de chemins.
    // -------------------------------------------------------------------------

    // Crée le répertoire et tous ses parents. Idempotent. Renvoie true si le
    // répertoire existe (ou a été créé) au retour.
    bool EnsureDirectoryExists(const std::filesystem::path& dir);
}

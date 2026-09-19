// ============================================================================
// RPFramework - Data / Migration
//
// Framework de migration de schéma pour les fichiers joueurs (GDD §20 :
// "versionnement des données" + "migration lors des changements de
// structure").
//
// Comment ajouter une migration :
//   1. Bumper `kPlayerDataSchemaVersion` (PlayerData.h) ET
//      `kCurrentSchemaVersion` (ce fichier).
//   2. Écrire une fonction Migrate_vN_to_vN+1(nlohmann::json& data).
//   3. L'ajouter dans la chaîne `Migrate()`.
// ============================================================================
#pragma once

#include "json.hpp"

namespace rpframework::data
{
    // Version de schéma cible (= kPlayerDataSchemaVersion dans PlayerData.h).
    inline constexpr int kCurrentSchemaVersion = 4;

    // Applique toutes les migrations nécessaires pour faire passer `data`
    // de `fromVersion` à `kCurrentSchemaVersion`. Mutates in place.
    // Renvoie true si quelque chose a été migré, false si déjà à jour.
    //
    // Garanties :
    //   - Si fromVersion > currentVersion : log un warning et fait le
    //     minimum (considère la donnée comme "futur" et la laisse).
    //   - Si fromVersion <= 0 : considéré comme legacy, on tente la
    //     migration depuis v0.
    //   - Si une migration throw, l'exception remonte à l'appelant qui
    //     doit fallback (PlayerStore le fait).
    bool Migrate(nlohmann::json& data, int fromVersion);
}

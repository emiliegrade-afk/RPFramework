// ============================================================================
// RPFramework - Asa / BlueprintPath
//
// Normalisation des chemins blueprint. Module PUR : aucune dépendance ARK,
// compilé dans le plugin ET dans les tests.
//
// Pourquoi ce fichier existe séparément de Asa/Blueprints.h : la clé d'un item
// est utilisée par plusieurs modules (Crafting, Effects, Merchant, hooks). Une
// deuxième implémentation de la normalisation, même « temporaire », finirait
// par diverger sur la casse ou sur le suffixe `_C`, et les lookups
// échoueraient silencieusement. Il n'y a donc qu'UNE normalisation, ici.
//
// Deux fonctions, deux rôles distincts et non interchangeables :
//
//   NormalizeBlueprintPath  → forme CANONIQUE, casse préservée.
//                             C'est ce qu'on stocke et ce qu'on passe à
//                             UVictoryCore::BPLoadClass.
//
//   BlueprintKey            → forme de COMPARAISON, minuscules.
//                             Sert uniquement de clé d'index / d'égalité.
//                             Ne JAMAIS la donner à BPLoadClass.
//
// Format canonique visé, identique à celui de configs/config.json :
//
//   /Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword
//
// soit sans wrapper `Blueprint'…'`, sans préfixe de type, sans `Default__`
// et sans suffixe `_C`.
// ============================================================================
#pragma once

#include <string>
#include <string_view>

namespace rpframework::asa
{
    // Nettoie un chemin brut vers la forme canonique. Idempotente :
    // NormalizeBlueprintPath(NormalizeBlueprintPath(x)) == NormalizeBlueprintPath(x).
    //
    // Formes acceptées en entrée :
    //   "Blueprint'/Game/X/Y.Y_C'"
    //   "BlueprintGeneratedClass /Game/X/Y.Default__Y_C"
    //   "Y_C /Game/X/Y.Default__Y_C"
    //   "/Game/X/Y.Y"                (déjà canonique)
    //   ""                           (renvoie "")
    //
    // Limite connue et acceptée : un asset dont le nom se termine réellement
    // par `_C` serait tronqué. La convention UE rend ce cas indistinguable ;
    // aucun asset vanilla n'est concerné.
    std::string NormalizeBlueprintPath(std::string_view raw);

    // Forme de comparaison : NormalizeBlueprintPath + minuscules ASCII.
    // À utiliser pour indexer et comparer, jamais pour charger une classe.
    std::string BlueprintKey(std::string_view raw);
}

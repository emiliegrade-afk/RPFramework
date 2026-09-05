// ============================================================================
// RPFramework - Loadout / Compose
//
// Composition du kit de départ final à partir de toutes les sources (GDD §9) :
//   - kit commun           (config.loadout.common_kit)
//   - kit de race          (Race.starter_equipment du joueur)
//   - kit de métier        (Profession.starter_equipment du joueur)
//   - kit de classe        (CharClass.starter_equipment du joueur)
//
// Les items identiques (même `id`) sont fusionnés en additionnant les
// quantités. Cela permet au propriétaire du serveur de définir un
// "bread" dans le kit commun ET dans le kit de race sans surprise.
//
// L'ordre d'application est : commun → race → profession → classe
// (l'ordre de dominance est donc : classe > métier > race > commun).
// En cas d'item présent deux fois, c'est la valeur la plus tardive qui
// peut écraser `quality` (sinon on additionne juste).
// ============================================================================
#pragma once

#include "Loadout/Item.h"
#include "Security/Types.h"  // PlayerId

#include <vector>

namespace rpframework::loadout
{
    using PlayerId = rpframework::security::PlayerId;

    class Composer
    {
    public:
        // Compose le kit de départ pour un joueur. Lit le PlayerData
        // (race/profession/class), charge les définitions depuis le
        // Character::Registry, et le kit commun depuis la config.
        // Renvoie un kit toujours valide (vecteur possiblement vide).
        static std::vector<Item> ComposeStarterKit(PlayerId player);

        // Helper pur (testable) : fusionne N listes d'items.
        // Même `id` → addition des quantités. La `quality` du dernier
        // item wins si présente.
        static std::vector<Item> Merge(const std::vector<std::vector<Item>>& sources);

        // Accès au kit commun (depuis la config).
        static std::vector<Item> LoadCommonKit();
    };
}

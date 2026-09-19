// ============================================================================
// RPFramework - Character / Stats
//
// Calcul des stats effectives d'un joueur à partir de ses sélections
// (race + métier + classe). Les modifiers s'appliquent dans cet ordre
// (GDD §8) :
//   1. race.bonuses
//   2. race.maluses
//   3. profession.bonuses
//   4. profession.maluses
//   5. class.bonuses
//   6. class.maluses
//
// Les opérations (Add / Multiply / Set) s'enchaînent sur la même stat.
//
// Le résultat est une map<string, float> libre. Le framework NE sait pas
// ce que veut dire "health" ou "movement" : il agrège juste. C'est aux
// modules aval (Phase 4b Loadout, Phase 9 UI, hooks AsaApi) de consommer
// ces valeurs.
// ============================================================================
#pragma once

#include "Character/Definitions.h"

#include "Data/PlayerData.h"
#include "Security/Types.h"  // PlayerId

#include <string>
#include <unordered_map>

namespace rpframework::character
{
    using PlayerId = rpframework::security::PlayerId;

    struct EffectiveStats
    {
        // target → valeur finale. Clés libres (ex: "health", "movement",
        // "weight", "xp_gain", ...). 0.0 = non spécifié.
        std::unordered_map<std::string, float> values;

        // Applique un modifier en place. Add +=, Multiply *=, Set =.
        // Si la stat n'existe pas et op != Set : considérée comme 0.
        void Apply(const StatModifier& mod);

        // Combine les bonus/maluses d'une Race/Profession/Class.
        // Ordre : race.bonuses, race.maluses, profession.bonuses,
        //         profession.maluses, class.bonuses, class.maluses.
        static EffectiveStats FromSelections(const Race& r,
                                              const Profession& p,
                                              const CharClass& c);
    };

    // Calcule les stats effectives pour un joueur. Lit PlayerData via
    // PlayerStore::LoadDetailed, charge les définitions depuis Registry.
    // Si une sélection pointe vers un ID inconnu, on l'ignore (pas fatal).
    EffectiveStats ComputeEffectiveStats(PlayerId player);
}

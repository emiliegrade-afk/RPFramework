// ============================================================================
// RPFramework - Effects / Definitions
//
// Types du moteur d'effets unique (GDD §44–§45). Un seul registre pour
// nourriture, potions, poisons, métiers et skills — pas un système de buff
// par source.
//
// Contrainte de conception (GDD §44) : le plugin n'a aucune boucle de tick
// (ni timer, ni thread, ni horloge). `durationSeconds` est de la métadonnée
// (durée du buff ARK associé) ; l'expiration n'est PAS gérée en C++. Un
// effet référence un buff ARK (`buffBlueprint`) qui expire seul. Le C++
// décide de l'octroi, des conditions, du cooldown et du stacking.
//
// `durationSeconds == 0` : permanent tant que la source dure.
// `buffBlueprint` vide : effet purement logique (modifiers seuls).
//
// Réutilise `character::StatModifier` et `character::SelectionCondition`
// plutôt que de redéfinir des types équivalents.
//
// Note (dette n°6) : `Asa/PawnEffects.cpp` applique des stats de façon
// PERMANENTE via `SetMaxStatusValue` et ne pourra pas servir de base aux
// effets temporaires. L'application est hors périmètre (phase 18).
// ============================================================================
#pragma once

#include "Character/Definitions.h"

#include "json.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rpframework::effects
{
    enum class EffectType   : std::uint8_t { Buff = 0, Debuff = 1 };
    enum class StackingMode : std::uint8_t { None = 0, Refresh = 1, Stack = 2 };

    // Conversion JSON ↔ enum. Renvoie false si la chaîne n'est pas reconnue
    // (la validation du Registry s'en sert pour rejeter l'entrée).
    bool EffectTypeFromString(std::string_view s, EffectType& out);
    bool StackingModeFromString(std::string_view s, StackingMode& out);
    const char* ToString(EffectType t);
    const char* ToString(StackingMode m);

    struct Effect
    {
        std::string id;
        std::string name;
        EffectType  type = EffectType::Buff;
        std::string buffBlueprint;              // vide = effet purement logique
        int         durationSeconds = 0;        // 0 = permanent tant que la source dure
        StackingMode stacking = StackingMode::Refresh;
        int         maxStacks = 1;
        int         cooldownSeconds = 0;
        std::vector<character::StatModifier> modifiers;   // réutilise l'existant
        character::SelectionCondition        conditions;  // réutilise l'existant

        nlohmann::json ToJson() const;
        static Effect  FromJson(const std::string& id, const nlohmann::json& j);
    };
}

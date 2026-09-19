// ============================================================================
// RPFramework - Clé blueprint canonique (GDD §39, dette n°1)
//
// N'inclut RIEN d'ARK. UObjectBase est forward-déclaré : le header reste
// compilable dans le plugin ET dans les tests.
// ============================================================================
#pragma once

#include <string>

// `NormalizeBlueprintPath` et `BlueprintKey` vivent dans BlueprintPath.h, qui
// est l'unique implémentation de la normalisation. Ce header ne fait qu'ajouter
// la partie qui dépend d'ARK : extraire le chemin depuis un UObjectBase.
#include "Asa/BlueprintPath.h"

// UE déclare UObjectBase en `struct` (pas `class`) : le forward-declare
// doit matcher, sinon C4099 dès qu'un .cpp inclut Blueprints.h puis Ark.h.
struct UObjectBase;

namespace rpframework::asa
{
#ifdef RPFRAMEWORK_TESTS
    inline std::string BlueprintPathOf(struct UObjectBase*) { return {}; }
#else
    std::string BlueprintPathOf(struct UObjectBase* object);
#endif
}

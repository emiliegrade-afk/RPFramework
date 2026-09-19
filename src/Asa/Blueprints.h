// ============================================================================
// RPFramework - Clé blueprint canonique (GDD §39, dette n°1)
//
// N'inclut RIEN d'ARK. UObjectBase est forward-déclaré : le header reste
// compilable dans le plugin ET dans les tests.
// ============================================================================
#pragma once

#include <string>
#include <string_view>

// UE déclare UObjectBase en `struct` (pas `class`) : le forward-declare
// doit matcher, sinon C4099 dès qu'un .cpp inclut Blueprints.h puis Ark.h.
struct UObjectBase;

namespace rpframework::asa
{
    // Normalise un chemin brut vers le format de config.json :
    //   "Blueprint'/Game/X.Default__X_C'" → "/Game/X.X"
    // Pure, sans dépendance ARK : compilée dans le plugin ET dans les tests.
    std::string NormalizeBlueprintPath(std::string_view raw);

#ifdef RPFRAMEWORK_TESTS
    inline std::string BlueprintPathOf(struct UObjectBase*) { return {}; }
#else
    std::string BlueprintPathOf(struct UObjectBase* object);
#endif
}

// ============================================================================
// RPFramework - Résolution ARK d'un chemin blueprint
// Compilé uniquement par le plugin (pas les tests).
// ============================================================================
#include "Asa/Blueprints.h"

#include "Asa/Identity.h"

#include "API/ARK/Ark.h"

namespace rpframework::asa
{
    std::string BlueprintPathOf(UObjectBase* object)
    {
        if (object == nullptr) return {};
        const FString raw = AsaApi::GetApiUtils().GetBlueprint(object);
        return NormalizeBlueprintPath(FStringToUtf8(raw));
    }
}

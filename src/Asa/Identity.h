// ============================================================================
// RPFramework - Glue AsaApi : identité joueur
// Compilé uniquement par le projet plugin (pas les tests).
// ============================================================================
#pragma once

#include "API/ARK/Ark.h"
#include "Security/Types.h"

#include <string>

namespace rpframework::asa
{
    std::string FStringToUtf8(const FString& value);
    FString Utf8ToFString(const std::string& value);
    security::PlayerId ExtractPlayerId(AShooterPlayerController* pc);
    AShooterPlayerController* FindController(security::PlayerId player);
    void Tell(security::PlayerId player, const std::string& message, bool ok = true);
}

// ============================================================================
// RPFramework - Effects / Apply (GDD §44, phase 18)
// Octroi d'un buff ARK. Pas de tick C++ : le buff expire tout seul.
// ============================================================================
#pragma once

#include "Security/Types.h"

#include <string>
#include <string_view>

namespace rpframework::effects
{
    using PlayerId = rpframework::security::PlayerId;

    struct ApplyResult
    {
        bool        ok = false;
        std::string message;
    };

    ApplyResult Apply(PlayerId player, std::string_view effectId);
}

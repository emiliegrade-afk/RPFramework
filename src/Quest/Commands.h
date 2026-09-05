// ============================================================================
// RPFramework - Quest / Commands
// ============================================================================
#pragma once

#include "Security/Types.h"
#include "Security/Permissions.h"

#include <string>
#include <string_view>
#include <vector>

namespace rpframework::quest
{
    using PlayerId = rpframework::security::PlayerId;

    struct CommandResult
    {
        bool handled = false;
        bool success = false;
        std::string message;
    };

    // Routeur pur, reutilisable par chat, console et RCON.
    CommandResult HandleCommand(PlayerId player, const std::vector<std::string>& args,
                                security::Level level = security::Level::PLAYER);
    std::vector<std::string> TokenizeCommand(std::string_view input);
}

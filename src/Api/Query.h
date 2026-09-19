// ============================================================================
// RPFramework - Façade query (GDD §27)
// DTO hors namespace Quest, pour chat, RCON et un futur AI Bridge.
// ============================================================================
#pragma once

#include "Security/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpframework::api
{
    using PlayerId = rpframework::security::PlayerId;

    struct PlayerInfo
    {
        PlayerId id = 0;
        std::string name;
        std::string race;
        std::string profession;
        std::string playerClass;
        std::string faction;
        int level = 1;
        int xp = 0;
        std::unordered_map<std::string, int> reputation;
        std::unordered_map<std::string, std::int64_t> wallets;
        std::vector<std::string> titles;
        std::vector<std::string> unlocks;
        bool starterKitDelivered = false;
    };

    std::optional<PlayerInfo> GetPlayerInfo(PlayerId player);
}

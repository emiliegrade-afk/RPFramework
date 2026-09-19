#include "Api/Query.h"
#include "Data/PlayerStore.h"

namespace rpframework::api
{
    std::optional<PlayerInfo> GetPlayerInfo(PlayerId player)
    {
        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return std::nullopt;

        const auto& src = *load.data;
        PlayerInfo info;
        info.id = src.id;
        info.name = src.name;
        info.race = src.race;
        info.profession = src.profession;
        info.playerClass = src.playerClass;
        info.faction = src.faction;
        info.level = src.level;
        info.xp = src.xp;
        info.reputation = src.reputation;
        info.wallets = src.wallets;
        info.titles = src.titles;
        info.unlocks = src.unlocks;
        info.starterKitDelivered = src.starterKitDelivered;
        return info;
    }
}

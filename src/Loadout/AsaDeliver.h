// ============================================================================
// RPFramework - Traduction Item.id → GiveItem ASA
// ============================================================================
#pragma once

#include "Loadout/Item.h"
#include "Security/Types.h"

#include <vector>

namespace rpframework::loadout
{
#ifdef RPFRAMEWORK_TESTS
    inline void TryGiveItems(security::PlayerId, const std::vector<Item>&) {}
    inline void TryGivePendingQuestItems(security::PlayerId) {}
    inline void TryUnlockEngrams(security::PlayerId, const std::vector<std::string>&) {}
#else
    // Donne les items dont extras.blueprint (ou id chemin) est renseigné.
    // Les ids sans blueprint sont ignorés (kit RP-only).
    void TryGiveItems(security::PlayerId player, const std::vector<Item>& items);
    void TryGivePendingQuestItems(security::PlayerId player);
    void TryUnlockEngrams(security::PlayerId player, const std::vector<std::string>& blueprints);
#endif
}

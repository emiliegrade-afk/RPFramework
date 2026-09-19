// ============================================================================
// RPFramework - Effets monde : stats pawn, spawn, bénéfices de rang
// Compilé par le plugin. Stubs no-op dans les tests.
// ============================================================================
#pragma once

#include "Security/Types.h"

namespace rpframework::asa
{
    enum class WorldApply : unsigned
    {
        None  = 0,
        Stats = 1,
        Spawn = 2,
        All   = 3,
    };

    inline WorldApply operator|(WorldApply a, WorldApply b)
    {
        return static_cast<WorldApply>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }

    inline bool HasFlag(WorldApply set, WorldApply flag)
    {
        return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0;
    }

#ifdef RPFRAMEWORK_TESTS
    inline void ApplyWorldEffects(security::PlayerId, WorldApply = WorldApply::All) {}
#else
    void ApplyWorldEffects(security::PlayerId player, WorldApply flags = WorldApply::All);
#endif
}

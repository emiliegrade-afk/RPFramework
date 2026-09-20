// ============================================================================
// RPFramework - Progression / Métiers - implémentation
// ============================================================================
#include "Progression/Professions.h"

#include "Character/Registry.h"
#include "Core/Config.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Security/AuditLog.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>

namespace rpframework::progression
{
    namespace
    {
        std::optional<data::PlayerData> LoadOrNull(PlayerId player)
        {
            auto load = data::PlayerStore::LoadDetailed(player);
            if (!load.HasData()) return std::nullopt;
            return std::move(load.data);
        }

        XpResult Fail(std::string message)
        {
            XpResult r;
            r.ok = false;
            r.message = std::move(message);
            return r;
        }

        int SkillPointsPerLevel(std::string_view professionId)
        {
            const std::string path = "character.professions."
                + std::string(professionId) + ".skill_points_per_level";
            const int value = core::Config::Get().GetOr(path, 1);
            return value >= 0 ? value : 1;
        }

        int DerivedLevel(int xp, int xpPerLevel, int maxLevel)
        {
            const int step = std::max(1, xpPerLevel);
            const int cap = std::max(1, maxLevel);
            int derived = 1;
            const int fromXp = xp / step;
            if (fromXp > std::numeric_limits<int>::max() - 1)
                derived = std::numeric_limits<int>::max();
            else
                derived = 1 + fromXp;
            return std::min(derived, cap);
        }

        bool WouldOverflowAdd(int before, int amount)
        {
            return amount > 0 && before > (std::numeric_limits<int>::max() - amount);
        }
    }

    XpResult AddProfessionXp(PlayerId player, std::string_view professionId,
                             int amount, std::string_view reason)
    {
        if (professionId.empty()
            || !character::Registry::HasProfession(std::string(professionId)))
        {
            return Fail("metier inconnu");
        }
        if (amount <= 0)
        {
            return Fail("montant invalide");
        }

        const auto def = character::Registry::GetProfession(std::string(professionId));
        if (!def) return Fail("metier inconnu");

        data::PlayerStore::ExclusiveLock storeLock;

        auto data = LoadOrNull(player);
        if (!data) return Fail("profil indisponible");

        const std::string id{professionId};
        auto& prog = data->professions[id];
        if (prog.professionId.empty())
            prog.professionId = id;
        if (prog.level < 1) prog.level = 1;
        if (prog.xp < 0) prog.xp = 0;
        if (prog.skillPoints < 0) prog.skillPoints = 0;

        if (WouldOverflowAdd(prog.xp, amount))
        {
            return Fail("xp overflow");
        }

        const int previousLevel = prog.level;
        prog.xp += amount;

        int nextLevel = DerivedLevel(prog.xp, def->xpPerLevel, def->maxLevel);
        if (nextLevel < previousLevel)
            nextLevel = previousLevel;
        prog.level = nextLevel;

        int skillGained = 0;
        const int levelsGained = prog.level - previousLevel;
        if (levelsGained > 0)
        {
            const int perLevel = SkillPointsPerLevel(id);
            if (perLevel > 0)
            {
                if (levelsGained > std::numeric_limits<int>::max() / perLevel)
                    skillGained = std::numeric_limits<int>::max();
                else
                    skillGained = levelsGained * perLevel;
                if (WouldOverflowAdd(prog.skillPoints, skillGained))
                    prog.skillPoints = std::numeric_limits<int>::max();
                else
                    prog.skillPoints += skillGained;
            }
        }

        if (!data::PlayerStore::Save(*data))
            return Fail("sauvegarde echouee");

        security::AuditLog::Log("progression.xp", player, {
            {"profession", id},
            {"amount", amount},
            {"xp", prog.xp},
            {"level", prog.level},
            {"previous_level", previousLevel},
            {"skill_points_gained", skillGained},
            {"reason", std::string(reason)},
        });

        XpResult result;
        result.ok = true;
        result.level = prog.level;
        result.previousLevel = previousLevel;
        result.xp = prog.xp;
        result.skillPointsGained = skillGained;
        result.message = result.LeveledUp()
            ? ("niveau " + std::to_string(result.level))
            : "ok";
        return result;
    }

    int GetProfessionLevel(PlayerId player, std::string_view professionId)
    {
        auto data = LoadOrNull(player);
        if (!data) return 1;
        const auto it = data->professions.find(std::string(professionId));
        if (it == data->professions.end()) return 1;
        return std::max(1, it->second.level);
    }

    int GetProfessionXp(PlayerId player, std::string_view professionId)
    {
        auto data = LoadOrNull(player);
        if (!data) return 0;
        const auto it = data->professions.find(std::string(professionId));
        if (it == data->professions.end()) return 0;
        return std::max(0, it->second.xp);
    }
}

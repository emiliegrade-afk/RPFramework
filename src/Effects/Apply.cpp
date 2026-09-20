// ============================================================================
// RPFramework - Effects / Apply - implémentation
// ============================================================================
#include "Effects/Apply.h"

#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Effects/Registry.h"
#include "Loadout/AsaDeliver.h"
#include "Core/Logger.h"
#include "Security/AuditLog.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace rpframework::effects
{
    namespace
    {
        std::int64_t NowUnix()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }

    ApplyResult Apply(PlayerId player, std::string_view effectId)
    {
        ApplyResult result;
        const std::string id{effectId};
        const auto effect = GetEffect(id);
        if (!effect)
        {
            result.message = "effet inconnu";
            return result;
        }

        data::PlayerStore::ExclusiveLock storeLock;
        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            result.message = "profil indisponible";
            return result;
        }
        auto data = *load.data;
        if (!effect->conditions.IsSatisfiedBy(data.race, data.profession, data.playerClass,
                                              data.level, data.reputation))
        {
            result.message = effect->conditions.DescribeViolation(
                data.race, data.profession, data.playerClass, data.level, data.reputation);
            security::AuditLog::LogDenied("effect.apply", player, "conditions", {
                {"effect", id}, {"detail", result.message},
            });
            return result;
        }

        const auto now = NowUnix();
        const auto cd = data.effectCooldowns.find(id);
        if (cd != data.effectCooldowns.end() && cd->second > now)
        {
            result.message = "cooldown actif";
            return result;
        }

        if (effect->stacking == StackingMode::None)
        {
            const auto active = std::find(data.activeEffects.begin(), data.activeEffects.end(), id);
            if (active != data.activeEffects.end())
            {
                result.message = "deja actif";
                return result;
            }
        }

        if (!effect->buffBlueprint.empty())
        {
            loadout::TryGiveBuff(player, effect->buffBlueprint);
        }
        else if (!effect->modifiers.empty())
        {
            rpframework::core::LogWarn(
                "Effet '{}' : modifiers ignores sans buff_blueprint (pas de Fold pawn).",
                id);
        }

        if (effect->stacking == StackingMode::None)
            data.activeEffects.push_back(id);
        if (effect->cooldownSeconds > 0)
            data.effectCooldowns[id] = now + effect->cooldownSeconds;

        if (!data::PlayerStore::Save(data))
        {
            result.message = "sauvegarde echouee";
            return result;
        }

        security::AuditLog::Log("effect.apply", player, {
            {"effect", id},
            {"buff", effect->buffBlueprint},
            {"duration_sec", effect->durationSeconds},
        });
        result.ok = true;
        result.message = "effet applique : " + effect->name;
        return result;
    }
}

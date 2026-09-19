#include "Asa/PawnEffects.h"

#include "Asa/Identity.h"

#include "Character/Registry.h"
#include "Character/Stats.h"
#include "Core/Config.h"
#include "Core/Logger.h"
#include "Data/PlayerStore.h"
#include "Faction/Registry.h"
#include "Faction/Reputation.h"

#include "API/ARK/Ark.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace rpframework::asa
{
    namespace
    {
        std::optional<EPrimalCharacterStatusValue::Type> MapStat(std::string_view target)
        {
            if (target == "health") return EPrimalCharacterStatusValue::Health;
            if (target == "stamina") return EPrimalCharacterStatusValue::Stamina;
            if (target == "oxygen") return EPrimalCharacterStatusValue::Oxygen;
            if (target == "food") return EPrimalCharacterStatusValue::Food;
            if (target == "water") return EPrimalCharacterStatusValue::Water;
            if (target == "weight" || target == "carry_weight")
                return EPrimalCharacterStatusValue::Weight;
            if (target == "melee" || target == "damage")
                return EPrimalCharacterStatusValue::MeleeDamageMultiplier;
            if (target == "movement" || target == "speed" || target == "movement_speed")
                return EPrimalCharacterStatusValue::SpeedMultiplier;
            if (target == "fortitude" || target == "cold" || target == "cold_resist"
                || target == "hypothermia")
                return EPrimalCharacterStatusValue::TemperatureFortitude;
            if (target == "crafting" || target == "crafting_speed")
                return EPrimalCharacterStatusValue::CraftingSpeedMultiplier;
            return std::nullopt;
        }

        float Fold(float base, const std::vector<character::StatModifier>& mods,
                   EPrimalCharacterStatusValue::Type type)
        {
            float value = base;
            for (const auto& mod : mods)
            {
                const auto mapped = MapStat(mod.target);
                if (!mapped || *mapped != type) continue;
                switch (mod.op)
                {
                    case character::StatModifier::Op::Add:      value += mod.value; break;
                    case character::StatModifier::Op::Multiply: value *= mod.value; break;
                    case character::StatModifier::Op::Set:      value  = mod.value; break;
                }
            }
            return value;
        }

        std::vector<character::StatModifier> CollectModifiers(security::PlayerId player)
        {
            std::vector<character::StatModifier> mods;
            auto load = data::PlayerStore::LoadDetailed(player);
            if (!load.HasData()) return mods;
            const auto& data = *load.data;

            if (auto race = character::Registry::GetRace(data.race))
            {
                mods.insert(mods.end(), race->bonuses.begin(), race->bonuses.end());
                mods.insert(mods.end(), race->maluses.begin(), race->maluses.end());
            }
            if (auto prof = character::Registry::GetProfession(data.profession))
            {
                mods.insert(mods.end(), prof->bonuses.begin(), prof->bonuses.end());
                mods.insert(mods.end(), prof->maluses.begin(), prof->maluses.end());
            }
            if (character::Registry::ClassesEnabled())
            {
                if (auto cls = character::Registry::GetClass(data.playerClass))
                {
                    mods.insert(mods.end(), cls->bonuses.begin(), cls->bonuses.end());
                    mods.insert(mods.end(), cls->maluses.begin(), cls->maluses.end());
                }
            }

            if (!data.faction.empty())
            {
                if (auto rank = faction::GetCurrentRank(player, data.faction))
                {
                    if (rank->benefits.contains("stats") && rank->benefits["stats"].is_array())
                    {
                        for (const auto& entry : rank->benefits["stats"])
                        {
                            if (!entry.is_object() || !entry.contains("target")) continue;
                            character::StatModifier mod;
                            mod.target = entry.value("target", std::string{});
                            mod.value  = entry.value("value", 0.0f);
                            const auto op = entry.value("op", std::string{"add"});
                            mod.op = character::StatModifier::OpFromString(op);
                            if (!mod.target.empty()) mods.push_back(mod);
                        }
                    }
                }
            }
            return mods;
        }

        void ApplyUnlocks(security::PlayerId player)
        {
            auto load = data::PlayerStore::LoadDetailed(player);
            if (!load.HasData() || load.data->faction.empty()) return;
            auto rank = faction::GetCurrentRank(player, load.data->faction);
            if (!rank || !rank->benefits.contains("unlocks")
                || !rank->benefits["unlocks"].is_array())
            {
                return;
            }

            auto data = *load.data;
            bool changed = false;
            for (const auto& entry : rank->benefits["unlocks"])
            {
                if (!entry.is_string()) continue;
                const auto id = entry.get<std::string>();
                if (id.empty()) continue;
                if (std::find(data.unlocks.begin(), data.unlocks.end(), id) == data.unlocks.end())
                {
                    data.unlocks.push_back(id);
                    changed = true;
                }
            }
            if (changed)
            {
                data::PlayerStore::Save(data);
                core::LogInfo("Rang {}: unlocks appliques pour joueur {}.",
                    rank->rankId, player);
            }
        }

        void ApplyStats(AShooterPlayerController* pc, security::PlayerId player)
        {
            auto* character = pc->GetPlayerCharacter();
            if (character == nullptr) return;
            auto* status = character->GetCharacterStatusComponent();
            if (status == nullptr) return;

            auto* baseline = status->GetDefaultCharacterStatusComponent();
            if (baseline == nullptr) baseline = status;

            const auto mods = CollectModifiers(player);
            int applied = 0;
            for (int i = 0; i < EPrimalCharacterStatusValue::MAX; ++i)
            {
                const auto type = static_cast<EPrimalCharacterStatusValue::Type>(i);
                bool relevant = false;
                for (const auto& mod : mods)
                {
                    const auto mapped = MapStat(mod.target);
                    if (mapped && *mapped == type) { relevant = true; break; }
                }
                if (!relevant) continue;

                const float vanilla = baseline->BPGetMaxStatusValue(type);
                const float next = Fold(vanilla, mods, type);
                if (next <= 0.0f) continue;
                status->SetMaxStatusValue(type, next);
                const float current = status->BPGetCurrentStatusValue(type);
                if (current < next)
                {
                    status->ModifyCurrentStatusValue(type, next, false, false,
                        true, true, {}, false, true);
                }
                ++applied;
            }
            if (applied > 0)
            {
                status->ServerForceUpdateMaxStatValues();
                core::LogInfo("Stats pawn: {} cibles appliquees pour joueur {}.", applied, player);
            }
        }

        void ApplySpawn(AShooterPlayerController* pc, security::PlayerId player)
        {
            auto load = data::PlayerStore::LoadDetailed(player);
            if (!load.HasData() || load.data->race.empty()) return;
            auto race = character::Registry::GetRace(load.data->race);
            if (!race || race->spawnZone.empty()) return;

            const auto zones = core::Config::Get().Get("world.spawn_zones");
            if (!zones || !zones->is_object() || !zones->contains(race->spawnZone)) return;
            const auto& zone = (*zones)[race->spawnZone];
            if (!zone.is_object() || !zone.contains("x") || !zone.contains("y") || !zone.contains("z"))
                return;

            const double x = zone.value("x", 0.0);
            const double y = zone.value("y", 0.0);
            const double z = zone.value("z", 0.0);
            if (x == 0.0 && y == 0.0 && z == 0.0) return;

            auto* character = pc->GetPlayerCharacter();
            if (character == nullptr) return;

            UE::Math::TVector<double> dest;
            dest.X = x;
            dest.Y = y;
            dest.Z = z;
            UE::Math::TRotator<double> rot;
            rot.Pitch = 0.0;
            rot.Yaw = zone.value("yaw", 0.0);
            rot.Roll = 0.0;
            if (character->TeleportTo(&dest, &rot, false, true))
            {
                core::LogInfo("Spawn zone '{}' appliquee pour joueur {}.", race->spawnZone, player);
            }
        }
    }

    void ApplyWorldEffects(security::PlayerId player, WorldApply flags)
    {
        if (player == 0) return;
        auto* pc = FindController(player);
        if (pc == nullptr) return;

        try
        {
            if (HasFlag(flags, WorldApply::Stats))
            {
                ApplyStats(pc, player);
                ApplyUnlocks(player);
            }
            if (HasFlag(flags, WorldApply::Spawn))
            {
                ApplySpawn(pc, player);
            }
        }
        catch (...)
        {
            core::LogWarn("ApplyWorldEffects a echoue pour joueur {}.", player);
        }
    }
}

// ============================================================================
// RPFramework - Crafting / Pipeline - implémentation
// ============================================================================
#include "Crafting/Pipeline.h"

#include "Asa/BlueprintPath.h"
#include "Character/Registry.h"
#include "Crafting/Registry.h"
#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"
#include "Effects/Apply.h"
#include "Loadout/AsaDeliver.h"
#include "Progression/Professions.h"
#include "Security/AuditLog.h"

#include <algorithm>
#include <unordered_set>

namespace rpframework::crafting
{
    namespace
    {
        bool HasSkill(const std::vector<std::string>& unlockedSkills,
                      std::string_view required)
        {
            if (required.empty()) return true;
            return std::find(unlockedSkills.begin(), unlockedSkills.end(),
                             required) != unlockedSkills.end();
        }

        const char* GateReason(RecipeGate gate)
        {
            switch (gate)
            {
            case RecipeGate::WrongProfession: return "mauvais_metier";
            case RecipeGate::LevelTooLow:     return "niveau_insuffisant";
            case RecipeGate::MissingSkill:    return "competence_manquante";
            case RecipeGate::Ok:              return "ok";
            }
            return "conditions";
        }

        std::string ProfessionDisplayName(std::string_view professionId)
        {
            const auto def = character::Registry::GetProfession(std::string(professionId));
            if (def && !def->name.empty()) return def->name;
            return std::string(professionId);
        }

        std::string EngramLabel(const std::string& blueprint)
        {
            if (const auto recipe = FindByOutputBlueprint(blueprint))
            {
                if (!recipe->name.empty()) return recipe->name;
            }
            return blueprint;
        }

        void AddUniqueBlueprint(std::vector<std::string>& out,
                                std::unordered_set<std::string>& seen,
                                const std::string& blueprint)
        {
            if (blueprint.empty()) return;
            const std::string key = asa::BlueprintKey(blueprint);
            if (key.empty() || !seen.insert(key).second) return;
            const std::string canonical = asa::NormalizeBlueprintPath(blueprint);
            out.push_back(canonical.empty() ? blueprint : canonical);
        }

        struct PlayerCraftState
        {
            std::string              selectedProfession;
            int                      level = 1;
            std::vector<std::string> unlockedSkills;
        };

        PlayerCraftState ReadCraftState(const data::PlayerData& data,
                                        std::string_view professionId)
        {
            PlayerCraftState state;
            state.selectedProfession = data.profession;
            const auto it = data.professions.find(std::string(professionId));
            if (it != data.professions.end())
            {
                state.level = std::max(1, it->second.level);
                state.unlockedSkills = it->second.unlockedSkills;
            }
            return state;
        }
    }

    RecipeGate CheckRecipeConditions(
        const Recipe& recipe,
        std::string_view selectedProfession,
        int professionLevel,
        const std::vector<std::string>& unlockedSkills)
    {
        if (selectedProfession.empty()
            || selectedProfession != recipe.profession)
        {
            return RecipeGate::WrongProfession;
        }
        if (professionLevel < recipe.minLevel)
        {
            return RecipeGate::LevelTooLow;
        }
        if (!HasSkill(unlockedSkills, recipe.requiredSkill))
        {
            return RecipeGate::MissingSkill;
        }
        return RecipeGate::Ok;
    }

    std::vector<std::string> CollectAccessibleEngrams(
        const std::string& professionId,
        int professionLevel,
        const std::vector<std::string>& unlockedSkills)
    {
        std::vector<std::string> out;
        std::unordered_set<std::string> seen;

        if (professionId.empty()) return out;

        if (const auto prof = character::Registry::GetProfession(professionId))
        {
            for (const auto& engram : prof->engrams)
                AddUniqueBlueprint(out, seen, engram);
        }

        for (const auto& recipe : ListForProfession(professionId))
        {
            if (CheckRecipeConditions(recipe, professionId, professionLevel,
                                      unlockedSkills) != RecipeGate::Ok)
            {
                continue;
            }
            AddUniqueBlueprint(out, seen, recipe.output.blueprint);
        }
        return out;
    }

    std::vector<std::string> GrantAccessibleEngrams(PlayerId player)
    {
        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return {};

        const auto& data = *load.data;
        if (data.profession.empty()) return {};

        const auto state = ReadCraftState(data, data.profession);
        auto engrams = CollectAccessibleEngrams(
            data.profession, state.level, state.unlockedSkills);
        if (!engrams.empty())
            loadout::TryUnlockEngrams(player, engrams);
        return engrams;
    }

    bool AllowCraft(PlayerId player, const std::string& outputBlueprint)
    {
        if (outputBlueprint.empty()) return true;
        const auto recipe = FindByOutputBlueprint(outputBlueprint);
        if (!recipe) return true;

        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return false;

        const auto state = ReadCraftState(*load.data, recipe->profession);
        return CheckRecipeConditions(
            *recipe, state.selectedProfession, state.level, state.unlockedSkills)
            == RecipeGate::Ok;
    }

    CraftOutcome OnItemCrafted(PlayerId player, const std::string& outputBlueprint)
    {
        CraftOutcome outcome;
        if (outputBlueprint.empty()) return outcome;

        const auto recipe = FindByOutputBlueprint(outputBlueprint);
        if (!recipe) return outcome;

        outcome.recipeId = recipe->id;
        outcome.profession = recipe->profession;

        data::PlayerStore::ExclusiveLock storeLock;

        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            outcome.status = CraftOutcome::Status::PlayerUnavailable;
            return outcome;
        }

        const auto state = ReadCraftState(*load.data, recipe->profession);
        const auto gate = CheckRecipeConditions(
            *recipe, state.selectedProfession, state.level, state.unlockedSkills);
        if (gate != RecipeGate::Ok)
        {
            outcome.status = CraftOutcome::Status::ConditionsUnmet;
            security::AuditLog::LogDenied("crafting.craft", player, GateReason(gate), {
                {"recipe", recipe->id},
                {"profession", recipe->profession},
            });
            return outcome;
        }

        const auto previousEngrams = CollectAccessibleEngrams(
            recipe->profession, state.level, state.unlockedSkills);

        int newLevel = state.level;
        int previousLevel = state.level;
        int xpTotal = 0;
        bool leveledUp = false;

        if (recipe->xp > 0)
        {
            const auto xp = progression::AddProfessionXp(
                player, recipe->profession, recipe->xp, "craft");
            if (!xp.ok)
            {
                outcome.status = CraftOutcome::Status::PlayerUnavailable;
                return outcome;
            }
            outcome.xpGranted = recipe->xp;
            previousLevel = xp.previousLevel;
            newLevel = xp.level;
            xpTotal = xp.xp;
            leveledUp = xp.LeveledUp();
        }
        else
        {
            xpTotal = progression::GetProfessionXp(player, recipe->profession);
        }

        outcome.status = CraftOutcome::Status::Applied;
        outcome.leveledUp = leveledUp;
        outcome.level = newLevel;
        outcome.previousLevel = previousLevel;
        outcome.xpTotal = xpTotal;

        const std::string profName = ProfessionDisplayName(recipe->profession);
        outcome.notices.push_back({
            profName + " : +" + std::to_string(outcome.xpGranted) + " XP",
            true
        });
        if (leveledUp)
        {
            outcome.notices.push_back({
                profName + " : niveau " + std::to_string(newLevel) + " !",
                true
            });
        }

        auto currentEngrams = CollectAccessibleEngrams(
            recipe->profession, newLevel, state.unlockedSkills);

        if (leveledUp && !currentEngrams.empty())
            loadout::TryUnlockEngrams(player, currentEngrams);

        std::unordered_set<std::string> beforeKeys;
        for (const auto& bp : previousEngrams)
            beforeKeys.insert(asa::BlueprintKey(bp));
        for (const auto& bp : currentEngrams)
        {
            if (beforeKeys.count(asa::BlueprintKey(bp)) != 0) continue;
            outcome.newlyUnlockedEngrams.push_back(bp);
            outcome.notices.push_back({
                "Engram débloqué : " + EngramLabel(bp),
                true
            });
        }

        security::AuditLog::Log("crafting.craft", player, {
            {"recipe", recipe->id},
            {"profession", recipe->profession},
            {"xp", outcome.xpGranted},
            {"level", newLevel},
            {"previous_level", previousLevel},
            {"leveled_up", leveledUp},
            {"engrams_unlocked", static_cast<int>(outcome.newlyUnlockedEngrams.size())},
        });
        return outcome;
    }

    ConsumeOutcome OnItemUsed(PlayerId player, const std::string& outputBlueprint)
    {
        ConsumeOutcome outcome;
        if (outputBlueprint.empty()) return outcome;

        const auto effectId = FindEffectForBlueprint(outputBlueprint);
        if (!effectId) return outcome;

        outcome.effectId = *effectId;
        const auto applied = effects::Apply(player, *effectId);
        outcome.applied = applied.ok;
        outcome.message = applied.message;
        if (applied.ok)
        {
            security::AuditLog::Log("crafting.consume", player, {
                {"blueprint", outputBlueprint},
                {"effect", *effectId},
            });
        }
        return outcome;
    }
}

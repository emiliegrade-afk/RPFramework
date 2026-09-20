// ============================================================================
// RPFramework - Character / Stats - implémentation
// ============================================================================
#include "Character/Stats.h"

#include "Character/Registry.h"
#include "Data/PlayerStore.h"
#include "Faction/Registry.h"
#include "Core/Logger.h"

namespace rpframework::character
{
    void EffectiveStats::Apply(const StatModifier& mod)
    {
        if (mod.target.empty()) return;

        // Convention : le tout premier modificateur Multiply sur une
        // stat jusqu'alors absente initialise la stat à 1.0 (élément
        // neutre de la multiplication). Sans cela, 0 * 1.1 = 0 et le
        // bonus n'a aucun effet — piège classique.
        if (mod.op == StatModifier::Op::Multiply && values.find(mod.target) == values.end())
        {
            values[mod.target] = 1.0f;
        }

        auto& v = values[mod.target];
        switch (mod.op)
        {
            case StatModifier::Op::Add:      v += mod.value; break;
            case StatModifier::Op::Multiply: v *= mod.value; break;
            case StatModifier::Op::Set:      v  = mod.value; break;
        }
    }

    EffectiveStats EffectiveStats::FromSelections(const Race& r,
                                                   const Profession& p,
                                                   const CharClass& c)
    {
        EffectiveStats s;
        for (const auto& m : r.bonuses)         s.Apply(m);
        for (const auto& m : r.maluses)         s.Apply(m);
        for (const auto& m : p.bonuses)         s.Apply(m);
        for (const auto& m : p.maluses)         s.Apply(m);
        for (const auto& m : c.bonuses)         s.Apply(m);
        for (const auto& m : c.maluses)         s.Apply(m);
        return s;
    }

    EffectiveStats ComputeEffectiveStats(PlayerId player)
    {
        EffectiveStats s;
        for (const auto& mod : CollectPawnModifiers(player))
            s.Apply(mod);
        return s;
    }

    std::vector<StatModifier> CollectPawnModifiers(PlayerId player)
    {
        std::vector<StatModifier> mods;
        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return mods;
        const auto& data = *load.data;

        if (auto race = Registry::GetRace(data.race))
        {
            mods.insert(mods.end(), race->bonuses.begin(), race->bonuses.end());
            mods.insert(mods.end(), race->maluses.begin(), race->maluses.end());
        }
        if (auto prof = Registry::GetProfession(data.profession))
        {
            mods.insert(mods.end(), prof->bonuses.begin(), prof->bonuses.end());
            mods.insert(mods.end(), prof->maluses.begin(), prof->maluses.end());
        }
        if (Registry::ClassesEnabled())
        {
            if (auto cls = Registry::GetClass(data.playerClass))
            {
                mods.insert(mods.end(), cls->bonuses.begin(), cls->bonuses.end());
                mods.insert(mods.end(), cls->maluses.begin(), cls->maluses.end());
            }
        }

        if (data.faction.empty()) return mods;
        auto faction = faction::Registry::GetFaction(data.faction);
        if (!faction) return mods;
        const auto it = data.reputation.find(data.faction);
        const int rep = (it == data.reputation.end()) ? 0 : it->second;
        const faction::Rank* best = nullptr;
        for (const auto& rank : faction->ranks)
        {
            if (rep >= rank.minReputation)
                best = &rank;
        }
        if (!best || !best->benefits.contains("stats") || !best->benefits["stats"].is_array())
            return mods;
        for (const auto& entry : best->benefits["stats"])
        {
            if (!entry.is_object() || !entry.contains("target")) continue;
            StatModifier mod;
            mod.target = entry.value("target", std::string{});
            mod.value  = entry.value("value", 0.0f);
            const auto op = entry.value("op", std::string{"add"});
            mod.op = StatModifier::OpFromString(op);
            if (!mod.target.empty()) mods.push_back(mod);
        }
        return mods;
    }

    float FoldFromBase(float base, const std::vector<StatModifier>& mods,
                       std::string_view target)
    {
        float value = base;
        for (const auto& mod : mods)
        {
            if (mod.target != target) continue;
            switch (mod.op)
            {
                case StatModifier::Op::Add:      value += mod.value; break;
                case StatModifier::Op::Multiply: value *= mod.value; break;
                case StatModifier::Op::Set:      value  = mod.value; break;
            }
        }
        return value;
    }

    const std::vector<std::string_view>& RpgStatTargets()
    {
        static const std::vector<std::string_view> kTargets = {
            "health", "stamina", "oxygen", "food", "water",
            "weight", "carry_weight",
            "melee", "damage",
            "movement", "speed", "movement_speed",
            "fortitude", "cold", "cold_resist", "hypothermia",
            "crafting", "crafting_speed",
        };
        return kTargets;
    }
}

// ============================================================================
// RPFramework - Character / Stats - implémentation
// ============================================================================
#include "Character/Stats.h"

#include "Character/Registry.h"
#include "Data/PlayerStore.h"
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

        // Charge le profil joueur. Si le fichier est corrompu / manquant,
        // on retourne des stats vides : l'absence de stats n'est pas une
        // erreur, juste un joueur sans sélection.
        auto load = rpframework::data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            return s;
        }
        const auto& data = *load.data;
        if (data.race.empty() && data.profession.empty() && data.playerClass.empty())
        {
            return s;
        }

        // Récupère les définitions. Une sélection vers un ID inconnu
        // (cas possible si la config a été éditée entre deux sessions)
        // est ignorée.
        std::optional<Race>       r;
        std::optional<Profession> p;
        std::optional<CharClass>  c;

        if (!data.race.empty())
        {
            r = Registry::GetRace(data.race);
            if (!r) rpframework::core::LogWarn("Stats: race '{}' inconnue pour joueur {} (ignoree).", data.race, player);
        }
        if (!data.profession.empty())
        {
            p = Registry::GetProfession(data.profession);
            if (!p) rpframework::core::LogWarn("Stats: profession '{}' inconnue pour joueur {} (ignoree).", data.profession, player);
        }
        if (!data.playerClass.empty() && Registry::ClassesEnabled())
        {
            c = Registry::GetClass(data.playerClass);
            if (!c) rpframework::core::LogWarn("Stats: class '{}' inconnue pour joueur {} (ignoree).", data.playerClass, player);
        }

        // Si on n'a rien à combiner, retourne vide.
        if (!r && !p && !c) return s;

        // Cas particulier : Profession et CharClass existent en
        // structures séparées avec des champs communs. On a besoin
        // d'objets par défaut pour FromSelections (qui attend des refs).
        // On accepte la copie (les structs sont petits).
        Race       dummyR;
        Profession dummyP;
        CharClass  dummyC;
        if (r) dummyR = *r; else dummyR.id = data.race;
        if (p) dummyP = *p; else dummyP.id = data.profession;
        if (c) dummyC = *c; else dummyC.id = data.playerClass;

        return EffectiveStats::FromSelections(dummyR, dummyP, dummyC);
    }
}

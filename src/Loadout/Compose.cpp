// ============================================================================
// RPFramework - Loadout / Compose - implémentation
// ============================================================================
#include "Loadout/Compose.h"

#include "Character/Registry.h"
#include "Core/Config.h"
#include "Core/Logger.h"
#include "Data/PlayerStore.h"

#include <algorithm>
#include <unordered_map>

namespace rpframework::loadout
{
    using PlayerId = rpframework::security::PlayerId;
    std::vector<Item> Composer::LoadCommonKit()
    {
        const auto& cfg = rpframework::core::Config::Get().Root();
        if (!cfg.is_object()) return {};
        if (!cfg.contains("loadout")) return {};
        const auto& loadout = cfg["loadout"];
        if (!loadout.is_object()) return {};
        if (!loadout.contains("common_kit")) return {};
        return ItemsFromJsonArray(loadout["common_kit"]);
    }

    std::vector<Item> Composer::Merge(const std::vector<std::vector<Item>>& sources)
    {
        // Map id → Item, en additionnant les quantités.
        // La `quality` la plus tardive wins (dernière source).
        std::unordered_map<std::string, Item> byId;
        for (const auto& source : sources)
        {
            for (const auto& it : source)
            {
                auto found = byId.find(it.id);
                if (found == byId.end())
                {
                    byId.emplace(it.id, it);
                }
                else
                {
                    found->second.quantity += it.quantity;
                    if (!it.quality.empty())
                    {
                        found->second.quality = it.quality;
                    }
                }
            }
        }

        // Renvoie dans l'ordre d'insertion (stable) pour la stabilité
        // des tests. En prod, on pourrait trier par id.
        std::vector<Item> out;
        out.reserve(byId.size());
        for (auto& [_, v] : byId) out.push_back(std::move(v));
        return out;
    }

    std::vector<Item> Composer::ComposeStarterKit(PlayerId player)
    {
        using namespace rpframework;

        // 1. Kit commun (depuis la config).
        std::vector<std::vector<Item>> sources;
        sources.push_back(LoadCommonKit());

        // 2. Charger le profil joueur pour savoir quelles définitions
        //    appliquer. Si le profil est indisponible, on retourne juste
        //    le kit commun (cas d'un joueur fraîchement créé sans
        //    sélection encore).
        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData())
        {
            return Merge(sources);
        }
        const auto& data = *load.data;

        // 3. Race
        if (!data.race.empty())
        {
            auto r = character::Registry::GetRace(data.race);
            if (r)
            {
                sources.push_back(ItemsFromJsonArray(r->starterEquipment));
            }
            else
            {
                rpframework::core::LogWarn("Loadout: race '{}' inconnue pour joueur {} (kit ignoré).",
                    data.race, player);
            }
        }

        // 4. Profession
        if (!data.profession.empty())
        {
            auto p = character::Registry::GetProfession(data.profession);
            if (p)
            {
                sources.push_back(ItemsFromJsonArray(p->starterEquipment));
            }
            else
            {
                rpframework::core::LogWarn("Loadout: profession '{}' inconnue pour joueur {} (kit ignoré).",
                    data.profession, player);
            }
        }

        // 5. Classe (seulement si activée)
        if (!data.playerClass.empty() && character::Registry::ClassesEnabled())
        {
            auto c = character::Registry::GetClass(data.playerClass);
            if (c)
            {
                sources.push_back(ItemsFromJsonArray(c->starterEquipment));
            }
            else
            {
                rpframework::core::LogWarn("Loadout: class '{}' inconnue pour joueur {} (kit ignoré).",
                    data.playerClass, player);
            }
        }

        return Merge(sources);
    }
}

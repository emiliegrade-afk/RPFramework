// ============================================================================
// RPFramework - Crafting / Registry - implémentation
// ============================================================================
#include "Crafting/Registry.h"

#include "Core/Config.h"
#include "Core/Logger.h"

#include <stdexcept>

namespace rpframework::crafting
{
    Registry& Registry::Instance()
    {
        static Registry inst;
        return inst;
    }

    void Registry::Initialize()
    {
        Instance().LoadFromConfigImpl();
    }

    void Registry::Shutdown()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.stations_.clear();
        self.recipes_.clear();
        self.outputIndex_.clear();
        self.initialized_ = false;
    }

    void Registry::LoadFromConfig()
    {
        Instance().LoadFromConfigImpl();
    }

    void Registry::ResetForTests()
    {
        Shutdown();
    }

    void Registry::LoadFromConfigImpl()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        const auto& cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* section = nullptr;
        if (cfg.is_object() && cfg.contains("crafting"))
        {
            section = &cfg["crafting"];
        }
        LoadDefinitionsFromSection(section);
        self.initialized_ = true;
    }

    void Registry::LoadDefinitionsFromSection(const nlohmann::json* section)
    {
        auto& self = Instance();
        // Le mutex est déjà tenu par LoadFromConfigImpl(). Ne pas re-locker ici,
        // sinon on deadlock sur le même thread lors de l'initialisation.

        // Défauts : registres vides. On n'invente aucune station / recette :
        // le propriétaire du serveur DOIT les déclarer dans la config.
        self.stations_.clear();
        self.recipes_.clear();
        self.outputIndex_.clear();

        if (section == nullptr || !section->is_object())
        {
            rpframework::core::LogInfo(
                "Registry(Crafting): aucune section 'crafting' dans la config ; registres vides.");
            return;
        }

        // Stations d'abord : les recettes valident leur référence.
        if (section->contains("stations") && (*section)["stations"].is_object())
        {
            for (auto it = (*section)["stations"].begin();
                 it != (*section)["stations"].end(); ++it)
            {
                try
                {
                    Station s = Station::FromJson(it.key(), it.value());
                    self.stations_.emplace(s.id, std::move(s));
                }
                catch (const std::exception& ex)
                {
                    rpframework::core::LogError(
                        "Registry(Crafting): station '{}' invalide: {}", it.key(), ex.what());
                }
            }
        }

        if (section->contains("recipes") && (*section)["recipes"].is_object())
        {
            for (auto it = (*section)["recipes"].begin();
                 it != (*section)["recipes"].end(); ++it)
            {
                try
                {
                    Recipe r = Recipe::FromJson(it.key(), it.value());
                    if (r.station.empty()
                        || self.stations_.find(r.station) == self.stations_.end())
                    {
                        throw std::runtime_error(
                            "station '" + r.station + "' inconnue");
                    }
                    const std::string outputKey = r.output.blueprint;
                    const std::string recipeId  = r.id;
                    self.recipes_.emplace(recipeId, std::move(r));
                    if (!outputKey.empty()
                        && self.outputIndex_.find(outputKey) == self.outputIndex_.end())
                    {
                        self.outputIndex_.emplace(outputKey, recipeId);
                    }
                }
                catch (const std::exception& ex)
                {
                    rpframework::core::LogError(
                        "Registry(Crafting): recette '{}' invalide: {}", it.key(), ex.what());
                }
            }
        }

        rpframework::core::LogInfo(
            "Registry(Crafting): {} stations, {} recettes.",
            self.stations_.size(), self.recipes_.size());
    }

    // -------------------------------------------------------------------------
    // Accesseurs
    // -------------------------------------------------------------------------

    bool Registry::HasRecipe(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.recipes_.find(id) != self.recipes_.end();
    }

    std::optional<Recipe> Registry::GetRecipe(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.recipes_.find(id);
        if (it == self.recipes_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<Recipe> Registry::FindByOutputBlueprint(const std::string& blueprint)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        const std::string key = NormalizeBlueprintPath(blueprint);
        if (key.empty()) return std::nullopt;
        auto idx = self.outputIndex_.find(key);
        if (idx == self.outputIndex_.end()) return std::nullopt;
        auto it = self.recipes_.find(idx->second);
        if (it == self.recipes_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<Recipe> Registry::ListRecipes()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Recipe> out;
        out.reserve(self.recipes_.size());
        for (const auto& [_, v] : self.recipes_) out.push_back(v);
        return out;
    }

    std::vector<Recipe> Registry::ListForProfession(const std::string& professionId)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Recipe> out;
        for (const auto& [_, v] : self.recipes_)
        {
            if (v.profession == professionId)
                out.push_back(v);
        }
        return out;
    }

    std::optional<Station> Registry::GetStation(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.stations_.find(id);
        if (it == self.stations_.end()) return std::nullopt;
        return it->second;
    }

    // -------------------------------------------------------------------------
    // API figée pour C1
    // -------------------------------------------------------------------------

    void Load()
    {
        Registry::LoadFromConfig();
    }

    std::optional<Recipe> GetRecipe(const std::string& id)
    {
        return Registry::GetRecipe(id);
    }

    std::optional<Recipe> FindByOutputBlueprint(const std::string& blueprint)
    {
        return Registry::FindByOutputBlueprint(blueprint);
    }

    std::vector<Recipe> ListRecipes()
    {
        return Registry::ListRecipes();
    }

    std::vector<Recipe> ListForProfession(const std::string& professionId)
    {
        return Registry::ListForProfession(professionId);
    }

    std::optional<Station> GetStation(const std::string& id)
    {
        return Registry::GetStation(id);
    }

    bool HasRecipe(const std::string& id)
    {
        return Registry::HasRecipe(id);
    }
}

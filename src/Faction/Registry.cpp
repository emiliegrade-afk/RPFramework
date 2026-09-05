// ============================================================================
// RPFramework - Faction / Registry - implémentation
// ============================================================================
#include "Faction/Registry.h"

#include "Core/Config.h"
#include "Core/Logger.h"

namespace rpframework::faction
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
        self.factions_.clear();
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

        self.factions_.clear();

        const auto& cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* section = nullptr;
        if (cfg.is_object() && cfg.contains("factions"))
        {
            section = &cfg["factions"];
        }
        LoadDefinitionsFromSection(section);
        self.initialized_ = true;
    }

    void Registry::LoadDefinitionsFromSection(const nlohmann::json* section)
    {
        auto& self = Instance();
        // mutex_ doit être tenu par LoadFromConfigImpl ; on ne relocke pas ici
        // pour éviter un deadlock sur l'appel d'initialisation.

        if (section == nullptr || !section->is_object())
        {
            rpframework::core::LogInfo("Registry(Faction): aucune section 'factions' dans la config ; registres vides.");
            return;
        }

        for (auto it = section->begin(); it != section->end(); ++it)
        {
            try
            {
                Faction f = Faction::FromJson(it.key(), it.value());
                self.factions_.emplace(f.id, std::move(f));
            }
            catch (const std::exception& ex)
            {
                rpframework::core::LogError("Registry(Faction): faction '{}' invalide: {}", it.key(), ex.what());
            }
        }

        rpframework::core::LogInfo("Registry(Faction): {} factions chargees.", self.factions_.size());
    }

    // -------------------------------------------------------------------------
    // Accesseurs
    // -------------------------------------------------------------------------

    bool Registry::HasFaction(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.factions_.find(id) != self.factions_.end();
    }

    std::optional<Faction> Registry::GetFaction(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.factions_.find(id);
        if (it == self.factions_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<Faction> Registry::ListFactions()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Faction> out;
        out.reserve(self.factions_.size());
        for (const auto& [_, v] : self.factions_) out.push_back(v);
        return out;
    }

    std::vector<std::string> Registry::ListFactionIds()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<std::string> out;
        out.reserve(self.factions_.size());
        for (const auto& [k, _] : self.factions_) out.push_back(k);
        return out;
    }
}

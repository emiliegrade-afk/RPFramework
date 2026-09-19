// ============================================================================
// RPFramework - Effects / Registry - implémentation
// ============================================================================
#include "Effects/Registry.h"

#include "Core/Config.h"
#include "Core/Logger.h"

namespace rpframework::effects
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
        self.effects_.clear();
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

        self.effects_.clear();

        const auto cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* section = nullptr;
        if (cfg.is_object() && cfg.contains("effects") && cfg["effects"].is_object())
        {
            section = &cfg["effects"];
        }
        LoadDefinitionsFromSection(section);
        self.initialized_ = true;
    }

    void Registry::LoadDefinitionsFromSection(const nlohmann::json* section)
    {
        auto& self = Instance();
        // mutex_ est déjà tenu par LoadFromConfigImpl(). Ne pas re-locker ici
        // (deadlock sur le même thread à l'initialisation). Les tests
        // appellent cette fonction après ResetForTests(), sans lock : le
        // processus de test est mono-thread.

        self.effects_.clear();

        if (section == nullptr || !section->is_object())
        {
            self.initialized_ = true;
            rpframework::core::LogInfo(
                "Registry(Effects): aucune section 'effects' dans la config ; registres vides.");
            return;
        }

        for (auto it = section->begin(); it != section->end(); ++it)
        {
            try
            {
                Effect e = Effect::FromJson(it.key(), it.value());
                self.effects_.emplace(e.id, std::move(e));
            }
            catch (const std::exception& ex)
            {
                rpframework::core::LogError(
                    "Registry(Effects): effet '{}' invalide: {}", it.key(), ex.what());
            }
        }

        self.initialized_ = true;
        rpframework::core::LogInfo("Registry(Effects): {} effets charges.", self.effects_.size());
    }

    bool Registry::HasEffect(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.effects_.find(id) != self.effects_.end();
    }

    std::optional<Effect> Registry::GetEffect(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.effects_.find(id);
        if (it == self.effects_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<Effect> Registry::ListEffects()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Effect> out;
        out.reserve(self.effects_.size());
        for (const auto& [_, v] : self.effects_) out.push_back(v);
        return out;
    }

    // -------------------------------------------------------------------------
    // API figée
    // -------------------------------------------------------------------------

    void Load()
    {
        Registry::LoadFromConfig();
    }

    std::optional<Effect> GetEffect(const std::string& id)
    {
        return Registry::GetEffect(id);
    }

    std::vector<Effect> ListEffects()
    {
        return Registry::ListEffects();
    }

    bool HasEffect(const std::string& id)
    {
        return Registry::HasEffect(id);
    }
}

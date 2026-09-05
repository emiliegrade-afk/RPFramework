// ============================================================================
// RPFramework - Quest / Registry - implémentation
// ============================================================================
#include "Quest/Registry.h"

#include "Core/Config.h"
#include "Core/Logger.h"

namespace rpframework::quest
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
        self.quests_.clear();
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

        self.quests_.clear();

        const auto& cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* section = nullptr;
        if (cfg.is_object() && cfg.contains("quests") && cfg["quests"].is_object())
        {
            section = &cfg["quests"];
        }

        LoadDefinitionsFromSection(section);
        self.initialized_ = true;
    }

    void Registry::LoadDefinitionsFromSection(const nlohmann::json* section)
    {
        auto& self = Instance();

        if (section == nullptr || !section->is_object())
        {
            rpframework::core::LogInfo("Registry(Quest): aucune section 'quests' dans la config ; registres vides.");
            return;
        }

        for (auto it = section->begin(); it != section->end(); ++it)
        {
            try
            {
                Quest q = Quest::FromJson(it.key(), it.value());
                self.quests_.emplace(q.id, std::move(q));
            }
            catch (const std::exception& ex)
            {
                rpframework::core::LogError("Registry(Quest): quete '{}' invalide: {}", it.key(), ex.what());
            }
        }

        rpframework::core::LogInfo("Registry(Quest): {} quetes chargees.", self.quests_.size());
    }

    bool Registry::HasQuest(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.quests_.find(id) != self.quests_.end();
    }

    std::optional<Quest> Registry::GetQuest(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.quests_.find(id);
        if (it == self.quests_.end())
            return std::nullopt;
        return it->second;
    }

    std::vector<Quest> Registry::ListQuests()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Quest> out;
        out.reserve(self.quests_.size());
        for (const auto& [_, v] : self.quests_)
            out.push_back(v);
        return out;
    }

    std::vector<std::string> Registry::ListQuestIds()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<std::string> out;
        out.reserve(self.quests_.size());
        for (const auto& [k, _] : self.quests_)
            out.push_back(k);
        return out;
    }
}

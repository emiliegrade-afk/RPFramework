// ============================================================================
// RPFramework - Quest / Registry
//
// Registre des quêtes chargées depuis la configuration. Le module est
// intentionally minimal : il fournit la base de la phase 7 / quest engine.
// ==========================================================================
#pragma once

#include "Quest/Definitions.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpframework::quest
{
    class Registry
    {
    public:
        static void Initialize();
        static void Shutdown();
        static void LoadFromConfig();
        static void LoadDefinitionsFromSection(const nlohmann::json* section);

        static bool HasQuest(const std::string& id);
        static std::optional<Quest> GetQuest(const std::string& id);
        static std::vector<Quest> ListQuests();
        static std::vector<std::string> ListQuestIds();

        static void ResetForTests();

    private:
        Registry() = default;
        static Registry& Instance();
        void LoadFromConfigImpl();

        mutable std::mutex mutex_;
        std::unordered_map<std::string, Quest> quests_;
        bool initialized_ = false;
    };
}

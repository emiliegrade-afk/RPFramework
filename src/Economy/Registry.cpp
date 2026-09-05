// ============================================================================
// RPFramework - Economy / Registry - implémentation
// ============================================================================
#include "Economy/Registry.h"

#include "Core/Config.h"
#include "Core/Logger.h"

namespace rpframework::economy
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
        self.currencies_.clear();
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

        self.currencies_.clear();

        const auto& cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* section = nullptr;
        if (cfg.is_object() && cfg.contains("economy")
            && cfg["economy"].is_object()
            && cfg["economy"].contains("currencies")
            && cfg["economy"]["currencies"].is_object())
        {
            section = &cfg["economy"]["currencies"];
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
            rpframework::core::LogInfo("Registry(Economy): aucune section 'economy.currencies' dans la config ; registres vides.");
            return;
        }

        for (auto it = section->begin(); it != section->end(); ++it)
        {
            try
            {
                Currency c = Currency::FromJson(it.key(), it.value());
                self.currencies_.emplace(c.id, std::move(c));
            }
            catch (const std::exception& ex)
            {
                rpframework::core::LogError("Registry(Economy): monnaie '{}' invalide: {}", it.key(), ex.what());
            }
        }

        rpframework::core::LogInfo("Registry(Economy): {} monnaies chargees.", self.currencies_.size());
    }

    // -------------------------------------------------------------------------
    // Accesseurs
    // -------------------------------------------------------------------------

    bool Registry::HasCurrency(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.currencies_.find(id) != self.currencies_.end();
    }

    std::optional<Currency> Registry::GetCurrency(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.currencies_.find(id);
        if (it == self.currencies_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<Currency> Registry::ListCurrencies()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Currency> out;
        out.reserve(self.currencies_.size());
        for (const auto& [_, v] : self.currencies_) out.push_back(v);
        return out;
    }

    std::vector<std::string> Registry::ListCurrencyIds()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<std::string> out;
        out.reserve(self.currencies_.size());
        for (const auto& [k, _] : self.currencies_) out.push_back(k);
        return out;
    }
}

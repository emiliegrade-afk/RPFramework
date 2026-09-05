// ============================================================================
// RPFramework - Character / Registry - implémentation
// ============================================================================
#include "Character/Registry.h"

#include "Core/Config.h"
#include "Core/Logger.h"

namespace rpframework::character
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
        self.races_.clear();
        self.professions_.clear();
        self.classes_.clear();
        self.classesEnabled_ = false;
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

        // Lecture de la config via le singleton de Core.
        const auto& cfg = rpframework::core::Config::Get().Root();
        const nlohmann::json* charSection = nullptr;
        if (cfg.is_object() && cfg.contains("character"))
        {
            charSection = &cfg["character"];
        }
        LoadDefinitionsFromSection(charSection);
    }

    void Registry::LoadDefinitionsFromSection(const nlohmann::json* charSection)
    {
        auto& self = Instance();
        // Le mutex est déjà tenu par LoadFromConfigImpl(). Ne pas re-locker ici,
        // sinon on deadlock sur le même thread lors de l'initialisation.

        // Défauts : classes désactivées, registres vides. On n'invente
        // aucune race / métier : le propriétaire du serveur DOIT les
        // déclarer dans la config.
        self.classesEnabled_ = false;
        self.races_.clear();
        self.professions_.clear();
        self.classes_.clear();

        if (charSection == nullptr || !charSection->is_object())
        {
            self.initialized_ = true;
            rpframework::core::LogInfo("Registry: aucune section 'character' dans la config ; registres vides.");
            return;
        }

        // classes_enabled (défaut: false pour respecter le GDD §7
        // "Le propriétaire peut décider d'utiliser des classes").
        self.classesEnabled_ = charSection->value("classes_enabled", false);

        // Races
        if (charSection->contains("races") && (*charSection)["races"].is_object())
        {
            for (auto it = (*charSection)["races"].begin();
                 it != (*charSection)["races"].end(); ++it)
            {
                try
                {
                    Race r = Race::FromJson(it.key(), it.value());
                    self.races_.emplace(r.id, std::move(r));
                }
                catch (const std::exception& ex)
                {
                    rpframework::core::LogError("Registry: race '{}' invalide: {}", it.key(), ex.what());
                }
            }
        }

        // Professions
        if (charSection->contains("professions") && (*charSection)["professions"].is_object())
        {
            for (auto it = (*charSection)["professions"].begin();
                 it != (*charSection)["professions"].end(); ++it)
            {
                try
                {
                    Profession p = Profession::FromJson(it.key(), it.value());
                    self.professions_.emplace(p.id, std::move(p));
                }
                catch (const std::exception& ex)
                {
                    rpframework::core::LogError("Registry: profession '{}' invalide: {}", it.key(), ex.what());
                }
            }
        }

        // Classes
        if (self.classesEnabled_
            && charSection->contains("classes") && (*charSection)["classes"].is_object())
        {
            for (auto it = (*charSection)["classes"].begin();
                 it != (*charSection)["classes"].end(); ++it)
            {
                try
                {
                    CharClass c = CharClass::FromJson(it.key(), it.value());
                    self.classes_.emplace(c.id, std::move(c));
                }
                catch (const std::exception& ex)
                {
                    rpframework::core::LogError("Registry: class '{}' invalide: {}", it.key(), ex.what());
                }
            }
        }

        self.initialized_ = true;
        rpframework::core::LogInfo("Registry: {} races, {} métiers, {} classes (classes_enabled={}).",
            self.races_.size(), self.professions_.size(), self.classes_.size(),
            self.classesEnabled_ ? "true" : "false");
    }

    // -------------------------------------------------------------------------
    // Accesseurs
    // -------------------------------------------------------------------------

    bool Registry::ClassesEnabled()
    {
        return Instance().classesEnabled_;
    }

    bool Registry::HasRace(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.races_.find(id) != self.races_.end();
    }

    std::optional<Race> Registry::GetRace(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.races_.find(id);
        if (it == self.races_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<Race> Registry::ListRaces()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Race> out;
        out.reserve(self.races_.size());
        for (const auto& [_, v] : self.races_) out.push_back(v);
        return out;
    }

    bool Registry::HasProfession(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        return self.professions_.find(id) != self.professions_.end();
    }

    std::optional<Profession> Registry::GetProfession(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        auto it = self.professions_.find(id);
        if (it == self.professions_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<Profession> Registry::ListProfessions()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<Profession> out;
        out.reserve(self.professions_.size());
        for (const auto& [_, v] : self.professions_) out.push_back(v);
        return out;
    }

    bool Registry::HasClass(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (!self.classesEnabled_) return false;
        return self.classes_.find(id) != self.classes_.end();
    }

    std::optional<CharClass> Registry::GetClass(const std::string& id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (!self.classesEnabled_) return std::nullopt;
        auto it = self.classes_.find(id);
        if (it == self.classes_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<CharClass> Registry::ListClasses()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        std::vector<CharClass> out;
        if (!self.classesEnabled_) return out;
        out.reserve(self.classes_.size());
        for (const auto& [_, v] : self.classes_) out.push_back(v);
        return out;
    }
}

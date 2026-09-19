// ============================================================================
// RPFramework - Core / Config - implémentation
// ============================================================================
#include "Core/Config.h"
#include "Core/Logger.h"

#include <fmt/format.h>

#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace rpframework::core
{
    namespace
    {
        // Split un chemin-point "a.b.c" en segments. Ignore les segments
        // vides (ex: "a..b" devient ["a", "b"]).
        std::vector<std::string> SplitDottedPath(std::string_view path)
        {
            std::vector<std::string> out;
            out.reserve(4);

            std::string current;
            current.reserve(path.size());

            for (char c : path)
            {
                if (c == '.')
                {
                    if (!current.empty())
                    {
                        out.push_back(std::move(current));
                        current.clear();
                    }
                }
                else
                {
                    current.push_back(c);
                }
            }
            if (!current.empty())
            {
                out.push_back(std::move(current));
            }
            return out;
        }
    }

    // -------------------------------------------------------------------------
    // Singleton.
    // -------------------------------------------------------------------------

    Config& Config::Get()
    {
        static Config instance;
        return instance;
    }

    // -------------------------------------------------------------------------
    // Cycle de vie.
    // -------------------------------------------------------------------------

    bool Config::LoadFromFile(const std::filesystem::path& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::ifstream file(path);
        if (!file.is_open())
        {
            LogWarn("config.json introuvable à {} - configuration precedente conservee.", path.string());
            if (sourcePath_.empty())
            {
                data_       = nlohmann::json::object();
                sourcePath_ = path;
            }
            return false;
        }

        try
        {
            nlohmann::json parsed;
            file >> parsed;

            // Une config valide doit être un objet. On refuse un tableau /
            // scalaire / null pour éviter d'avoir une "config" qui n'est
            // pas un dictionnaire de sections.
            if (!parsed.is_object())
            {
                LogError("config.json à {} n'est pas un objet JSON - ignoré.", path.string());
                return false;
            }

            data_       = std::move(parsed);
            sourcePath_ = path;
            LogInfo("config.json chargé depuis {}.", path.string());
            return true;
        }
        catch (const std::exception& ex)
        {
            LogError("échec du parsing de config.json ({}): {}.", path.string(), ex.what());
            return false;
        }
    }

    bool Config::SaveToFile(const std::filesystem::path& path) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        try
        {
            // Écriture atomique : .tmp + rename.
            const std::filesystem::path tmp = path.string() + ".tmp";
            {
                std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
                if (!out.is_open())
                {
                    LogError("impossible d'ouvrir {} en écriture.", tmp.string());
                    return false;
                }
                out << data_.dump(2);
            }
            std::error_code ec;
            std::filesystem::rename(tmp, path, ec);
            if (ec)
            {
                LogError("rename {} -> {} a échoué: {}", tmp.string(), path.string(), ec.message());
                return false;
            }
            return true;
        }
        catch (const std::exception& ex)
        {
            LogError("erreur sauvegarde config vers {}: {}", path.string(), ex.what());
            return false;
        }
    }

    bool Config::Reload()
    {
        std::filesystem::path path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            path = sourcePath_;
        }
        if (path.empty())
        {
            LogWarn("Reload() appelé avant LoadFromFile() - ignoré.");
            return false;
        }
        return LoadFromFile(path);
    }

    // -------------------------------------------------------------------------
    // Résolution de chemin-point.
    // -------------------------------------------------------------------------

    const nlohmann::json* Config::Resolve(std::string_view dottedPath) const
    {
        const nlohmann::json* current = &data_;
        for (const auto& segment : SplitDottedPath(dottedPath))
        {
            if (current == nullptr || !current->is_object())
            {
                return nullptr;
            }
            auto it = current->find(segment);
            if (it == current->end())
            {
                return nullptr;
            }
            current = &(*it);
        }
        return current;
    }

    nlohmann::json* Config::ResolveMutable(std::string_view dottedPath)
    {
        nlohmann::json* current = &data_;
        for (const auto& segment : SplitDottedPath(dottedPath))
        {
            if (current == nullptr || !current->is_object())
            {
                return nullptr;
            }
            auto it = current->find(segment);
            if (it == current->end())
            {
                // Crée l'intermédiaire.
                (*current)[segment] = nlohmann::json::object();
                current             = &((*current)[segment]);
            }
            else
            {
                current = &(*it);
            }
        }
        return current;
    }

    // -------------------------------------------------------------------------
    // API publique de lecture / écriture.
    // -------------------------------------------------------------------------

    nlohmann::json Config::Root() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_;
    }

    std::optional<nlohmann::json> Config::Get(std::string_view dottedPath) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const nlohmann::json* node = Resolve(dottedPath);
        if (node == nullptr)
        {
            return std::nullopt;
        }
        return *node;
    }

    bool Config::Has(std::string_view dottedPath) const
    {
        return Get(dottedPath).has_value();
    }

    void Config::Set(std::string_view dottedPath, nlohmann::json value)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto segments = SplitDottedPath(dottedPath);
        if (segments.empty())
        {
            // Set racine = remplace tout l'arbre.
            if (value.is_object())
            {
                data_ = std::move(value);
            }
            else
            {
                LogError("Set(\"\") : la racine doit être un objet JSON.");
            }
            return;
        }

        // Navigue/crée les intermédiaires jusqu'à l'avant-dernier segment.
        nlohmann::json* parent = &data_;
        for (std::size_t i = 0; i + 1 < segments.size(); ++i)
        {
            const auto& seg = segments[i];
            if (!parent->is_object())
            {
                LogError("Set: segment '{}' non navigable (parent n'est pas un objet).", seg);
                return;
            }
            auto it = parent->find(seg);
            if (it == parent->end() || !it->is_object())
            {
                (*parent)[seg] = nlohmann::json::object();
            }
            parent = &((*parent)[seg]);
        }

        // Assigne la feuille.
        if (!parent->is_object())
        {
            LogError("Set: parent de '{}' n'est pas un objet.", segments.back());
            return;
        }
        (*parent)[segments.back()] = std::move(value);
    }
}

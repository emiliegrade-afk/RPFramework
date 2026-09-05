// ============================================================================
// RPFramework - Core / Config
//
// Gestionnaire de configuration unique, basé sur nlohmann::json.
// Toute la config du framework (settings, races, métiers, classes, factions,
// quêtes, loadouts) transite par ce module.
//
// Règles (GDD §2, §21) :
//   - Le contenu vient de la configuration, pas du code.
//   - Le format est validé au chargement pour éviter un état incohérent.
//   - L'absence ou une erreur de parsing ne doivent pas faire crasher le
//     serveur : on log un warning et on conserve un objet vide.
//
// Singleton à initialisation explicite via Config::Get().
// ============================================================================
#pragma once

#include "json.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace rpframework::core
{
    class Config
    {
    public:
        // ---------------------------------------------------------------------
        // Cycle de vie.
        // ---------------------------------------------------------------------

        // Charge la configuration depuis `path`. Renvoie true si le fichier
        // a été trouvé et parsé avec succès. Un échec n'est jamais fatal :
        // la config reste utilisable (vide ou avec l'état précédent).
        bool LoadFromFile(const std::filesystem::path& path);

        // Sauvegarde atomique (écrit dans un .tmp puis renomme) de la config
        // courante vers `path`. Utile pour les modules qui modifient leur
        // portion de config (Phase 2+). Renvoie true en cas de succès.
        bool SaveToFile(const std::filesystem::path& path) const;

        // Recharge depuis le dernier chemin utilisé. No-op si rien n'a été
        // chargé.
        bool Reload();

        // Renvoie le dernier chemin de chargement (vide si aucun).
        const std::filesystem::path& SourcePath() const { return sourcePath_; }

        // ---------------------------------------------------------------------
        // Accès typé.
        // ---------------------------------------------------------------------

        // Renvoie l'objet JSON complet (lecture seule). Utile pour itérer
        // (ex: lister toutes les races configurées).
        const nlohmann::json& Root() const { return data_; }

        // Renvoie la portion de config à un chemin-point (notation JSONPath
        // simplifiée : "a.b.c"). Renvoie nullptr si absent.
        const nlohmann::json* Get(std::string_view dottedPath) const;

        // Variante : valeur par défaut si absente ou mauvais type.
        template <typename T>
        T GetOr(std::string_view dottedPath, T defaultValue) const
        {
            const nlohmann::json* node = Get(dottedPath);
            if (node == nullptr)
            {
                return defaultValue;
            }
            try
            {
                return node->get<T>();
            }
            catch (const nlohmann::json::exception&)
            {
                return defaultValue;
            }
        }

        // True si le chemin existe (peut être null).
        bool Has(std::string_view dottedPath) const;

        // Modifie une portion. Thread-safe. Crée les intermédiaires si besoin.
        void Set(std::string_view dottedPath, nlohmann::json value);

        // ---------------------------------------------------------------------
        // Singleton.
        // ---------------------------------------------------------------------
        static Config& Get();

    private:
        // Constructeur privé : l'accès passe par Get().
        Config() = default;

        // Non-copiable / non-movable : il n'y a qu'une seule config active.
        Config(const Config&)            = delete;
        Config& operator=(const Config&) = delete;
        Config(Config&&)                 = delete;
        Config& operator=(Config&&)      = delete;

        // Résout un chemin-point en référence sur l'arbre JSON.
        const nlohmann::json* Resolve(std::string_view dottedPath) const;
        nlohmann::json*       ResolveMutable(std::string_view dottedPath);

        mutable std::mutex mutex_;
        nlohmann::json data_ = nlohmann::json::object();
        std::filesystem::path sourcePath_;
    };
}

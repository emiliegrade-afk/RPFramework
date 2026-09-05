// ============================================================================
// RPFramework - Character / Registry
//
// Registre des définitions de races / métiers / classes chargées depuis
// la configuration. Le framework ne fournit aucune définition baked-in :
// tout est data-driven.
//
// GDD §5/6/7 : le propriétaire du serveur peut créer autant de races /
// métiers / classes qu'il le souhaite. Le système de classes peut être
// désactivé (`config.character.classes_enabled = false`).
// ============================================================================
#pragma once

#include "Character/Definitions.h"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rpframework::character
{
    class Registry
    {
    public:
        // Cycle de vie ---------------------------------------------------------
        static void Initialize();
        static void Shutdown();
        // Recharge la section `config.character.*`. Idempotent : remplace
        // les définitions en place. N'efface PAS la sélection des joueurs.
        static void LoadFromConfig();

        // Charge les définitions depuis un sous-objet `character.*` brut.
        // Utile pour les tests (évite d'écrire un fichier config temporaire).
        // Si `section` est null, traite comme une section absente.
        static void LoadDefinitionsFromSection(const nlohmann::json* section);

        // Système de classes activé ? ----------------------------------------
        static bool ClassesEnabled();

        // API Races ------------------------------------------------------------
        static bool                       HasRace(const std::string& id);
        static std::optional<Race>        GetRace(const std::string& id);
        static std::vector<Race>          ListRaces();

        // API Métiers ----------------------------------------------------------
        static bool                       HasProfession(const std::string& id);
        static std::optional<Profession>  GetProfession(const std::string& id);
        static std::vector<Profession>    ListProfessions();

        // API Classes ----------------------------------------------------------
        static bool                       HasClass(const std::string& id);
        static std::optional<CharClass>   GetClass(const std::string& id);
        static std::vector<CharClass>     ListClasses();

        // Tests / admin : reset complet des définitions. ---------------
        static void ResetForTests();

    private:
        Registry() = default;

        static Registry& Instance();
        void LoadFromConfigImpl();

        mutable std::mutex                                          mutex_;
        bool                                                         classesEnabled_ = false;
        std::unordered_map<std::string, Race>                       races_;
        std::unordered_map<std::string, Profession>                 professions_;
        std::unordered_map<std::string, CharClass>                  classes_;
        bool                                                         initialized_ = false;
    };
}

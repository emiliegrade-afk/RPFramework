// ============================================================================
// RPFramework - Effects / Registry
//
// Registre data-driven des effets (GDD §44). Aucune définition baked-in :
// tout vient de `config.effects.*`. Une entrée invalide est rejetée au
// chargement (log d'erreur) et n'apparaît pas dans le Registry.
//
// Contrat figé (ROADMAP A4) : Load / GetEffect / ListEffects / HasEffect.
//
// Aucune application d'effet ici (phase 18). Aucune boucle de tick, aucun
// timer, aucun thread, aucune horloge.
// ============================================================================
#pragma once

#include "Effects/Definitions.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpframework::effects
{
    class Registry
    {
    public:
        // Cycle de vie ---------------------------------------------------------
        static void Initialize();
        static void Shutdown();

        // Charge (ou recharge) la section `config.effects`. Idempotent :
        // remplace les définitions en place.
        static void LoadFromConfig();

        // Variante directe (tests) : charge depuis un objet `effects` brut.
        // Si `section` est null, traite comme une section absente.
        static void LoadDefinitionsFromSection(const nlohmann::json* section);

        static std::optional<Effect> GetEffect(const std::string& id);
        static std::vector<Effect>   ListEffects();
        static bool                  HasEffect(const std::string& id);

        // Tests / admin : reset complet.
        static void ResetForTests();

    private:
        Registry() = default;

        static Registry& Instance();
        void LoadFromConfigImpl();

        mutable std::mutex                         mutex_;
        std::unordered_map<std::string, Effect>    effects_;
        bool                                       initialized_ = false;
    };

    // -------------------------------------------------------------------------
    // API figée (ROADMAP A4) : Load / GetEffect / ListEffects / HasEffect.
    // -------------------------------------------------------------------------
    void Load();
    std::optional<Effect> GetEffect(const std::string& id);
    std::vector<Effect>   ListEffects();
    bool                  HasEffect(const std::string& id);
}

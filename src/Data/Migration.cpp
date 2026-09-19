// ============================================================================
// RPFramework - Data / Migration - implémentation
// ============================================================================
#include "Data/Migration.h"

#include "Core/Logger.h"

#include <stdexcept>
#include <string>

namespace rpframework::data
{
    namespace
    {
        // --- Étapes de migration ------------------------------------------
        // v0 correspond aux fichiers legacy sans section meta. Cette
        // migration rend le fichier explicitement versionné avant la lecture.
        void Migrate_v0_to_v1(nlohmann::json& data)
        {
            if (!data.is_object())
            {
                throw std::runtime_error("racine de donnees joueur invalide");
            }
            if (!data.contains("meta") || !data["meta"].is_object())
            {
                data["meta"] = nlohmann::json::object();
            }
            data["meta"]["schema_version"] = 1;
        }

        // v1 → v2 : ajout de la section "economy" (wallets). Vide = aucun
        // wallet ; le joueur peut en accumuler par la suite via Economy::Wallet.
        void Migrate_v1_to_v2(nlohmann::json& data)
        {
            if (!data.is_object())
            {
                throw std::runtime_error("racine de donnees joueur invalide");
            }
            if (!data.contains("economy") || !data["economy"].is_object())
            {
                data["economy"] = nlohmann::json::object();
            }
        }

        // v2 → v3 : ajout de la section "unlocks" (récompenses de quête
        // de type "unlock", ex: recettes/zones/fonctionnalités). Vide
        // jusqu'à la première quête qui en décerne. Les anciens unlocks
        // éventuellement stockés avec un préfixe "unlock:" dans `titles`
        // ne sont PAS déplacés automatiquement : un upgrade manuel
        // côté admin peut être nécessaire si vous aviez déjà décerné
        // des unlocks avant la migration.
        void Migrate_v2_to_v3(nlohmann::json& data)
        {
            if (!data.is_object())
            {
                throw std::runtime_error("racine de donnees joueur invalide");
            }
            if (!data.contains("unlocks") || !data["unlocks"].is_array())
            {
                data["unlocks"] = nlohmann::json::array();
            }
        }

        // v3 → v4 : ajout de la section "professions" (progression par
        // métier, GDD §38). Si le métier principal est renseigné, on
        // recopie level / xp globaux dans l'entrée correspondante pour
        // ne perdre aucun joueur. Le champ character.profession n'est
        // PAS supprimé : Character/Stats, Select et Asa/PawnEffects
        // le lisent encore.
        void Migrate_v3_to_v4(nlohmann::json& data)
        {
            if (!data.is_object())
            {
                throw std::runtime_error("racine de donnees joueur invalide");
            }
            if (!data.contains("professions") || !data["professions"].is_object())
            {
                data["professions"] = nlohmann::json::object();
            }

            std::string professionId;
            if (data.contains("character") && data["character"].is_object())
            {
                const auto& character = data["character"];
                if (character.contains("profession") && character["profession"].is_string())
                {
                    professionId = character["profession"].get<std::string>();
                }
            }
            if (professionId.empty())
            {
                return;
            }

            auto& professions = data["professions"];
            if (professions.contains(professionId) && professions[professionId].is_object())
            {
                // Déjà présent : ne pas écraser une progression métier.
                return;
            }

            int level = 1;
            int xp = 0;
            if (data.contains("progression") && data["progression"].is_object())
            {
                const auto& progression = data["progression"];
                level = progression.value("level", 1);
                xp    = progression.value("xp", 0);
            }

            professions[professionId] = {
                {"profession_id", professionId},
                {"level",         level},
                {"xp",            xp},
                {"skill_points",  0},
            };
        }
    }

    bool Migrate(nlohmann::json& data, int fromVersion)
    {
        if (fromVersion == kCurrentSchemaVersion)
        {
            return false;
        }

        if (fromVersion > kCurrentSchemaVersion)
        {
            rpframework::core::LogWarn("Migration: donnees futures detectees (fromVersion={}, current={}). On laisse tel quel.",
                fromVersion, kCurrentSchemaVersion);
            // Ne jamais rétrograder silencieusement une donnée plus récente :
            // un binaire ancien pourrait autrement détruire des champs qu'il
            // ne comprend pas.
            return false;
        }

        rpframework::core::LogInfo("Migration: migration v{} -> v{} en cours.", fromVersion, kCurrentSchemaVersion);

        if (fromVersion < 1)
        {
            Migrate_v0_to_v1(data);
        }
        if (fromVersion < 2)
        {
            Migrate_v1_to_v2(data);
        }
        if (fromVersion < 3)
        {
            Migrate_v2_to_v3(data);
        }
        if (fromVersion < 4)
        {
            Migrate_v3_to_v4(data);
        }

        data["meta"]["schema_version"] = kCurrentSchemaVersion;
        return true;
    }
}

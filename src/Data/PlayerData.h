// ============================================================================
// RPFramework - Data / PlayerData
//
// Structure de données persistante d'un joueur (GDD §4, §20).
// Sérialisée en JSON dans un fichier par joueur sous
// `ArkApi/Plugins/RPFramework/players/{id}.json`.
//
// Le champ `schemaVersion` permet la migration entre versions du schéma
// (voir Data/Migration.h).
// ============================================================================
#pragma once

#include "Security/Types.h"  // PlayerId

#include "json.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rpframework::data
{
    using PlayerId = rpframework::security::PlayerId;

    struct QuestProgress
    {
        enum class Status : std::uint8_t
        {
            Active = 0,
            Completed = 1,
        };

        Status status = Status::Active;
        std::unordered_map<std::string, int> objectives;
        bool rewardsGranted = false;
        std::int64_t startedAt = 0;
        std::vector<nlohmann::json> pendingItemRewards;
    };

    // Progression d'un métier (schéma v4, GDD §38). Plusieurs métiers
    // peuvent progresser simultanément ; `PlayerData.profession` désigne
    // le métier principal, pas le seul.
    struct ProfessionProgression
    {
        std::string              professionId;
        int                      level = 1;
        int                      xp = 0;
        int                      skillPoints = 0;
        std::vector<std::string> unlockedSkills;
        std::vector<std::string> unlockedRecipes;

        nlohmann::json ToJson() const;
        static ProfessionProgression FromJson(const std::string& id,
                                              const nlohmann::json& j);
    };

    // Schéma de données versionné. Quand on ajoute/renomme un champ, on
    // bump ce numéro et on ajoute la migration correspondante dans
    // Migration.cpp.
    inline constexpr int kPlayerDataSchemaVersion = 4;

    struct PlayerData
    {
        // Identité ---------------------------------------------------------
        PlayerId          id          = 0;
        std::string       name;            // peut être vide (joueur non nommé)

        // Character (Phase 4+ remplira ces champs via Character::Select) ---
        std::string       race;
        std::string       profession;
        std::string       playerClass;
        std::string       faction;
        // Teleport de zone de race deja applique (SelectRace, une fois).
        bool              spawnApplied = false;

        // Progression ------------------------------------------------------
        int               level = 1;
        int               xp    = 0;

        // Progression par métier : profession_id → état (GDD §38). Le
        // champ `profession` (string) est conservé : il est lu par
        // Character/Stats, Character/Select et Asa/PawnEffects.
        std::unordered_map<std::string, ProfessionProgression> professions;

        // Reputation : faction_id → valeur --------------------------------
        std::unordered_map<std::string, int> reputation;

        // Wallets : currency_id → solde (Phase 6, GDD §14). Le solde est
        // stocké directement dans PlayerData pour qu'il soit persisté par
        // PlayerStore sans nouveau schéma. La modification DOIT passer par
        // Economy::Wallet (Add/Subtract/Transfer/Grant/Reward).
        std::unordered_map<std::string, std::int64_t> wallets;

        // Progression des quêtes : quest_id -> état et progression par objectif.
        std::unordered_map<std::string, QuestProgress> quests;

        // Titres débloqués (Phase 4+) --------------------------------------
        std::vector<std::string>            titles;

        // Déblocages (Phase 7) : récompenses de type "unlock" (ex: recettes,
        // zones, fonctionnalités). Séparé de `titles` qui reste purement
        // cosmétique/affichage.
        std::vector<std::string>            unlocks;

        // Loadout : a-t-on déjà distribué le kit de départ ? Phase 4b.
        // Idempotent : GiveStarterKit refuse de re-distribuer si true
        // ET que l'outbox `pendingStarterKit` est vide.
        bool                                starterKitDelivered = false;
        // Items ASA pas encore confirmés en jeu. Persistés AVANT GiveItem.
        std::vector<nlohmann::json>         pendingStarterKit;

        // Effets : cooldown unix (secondes) et ids en stacking None.
        std::unordered_map<std::string, std::int64_t> effectCooldowns;
        std::vector<std::string>            activeEffects;

        // Métadonnées de persistance (remplies par PlayerStore) ----------
        int                                          schemaVersion = kPlayerDataSchemaVersion;
        std::chrono::system_clock::time_point        createdAt;
        std::chrono::system_clock::time_point        updatedAt;

        // ---------------------------------------------------------------------
        // (De)sérialisation JSON.
        // Le format est volontairement sectionné (identity / character /
        // progression / professions / reputation / titles / meta) pour que
        // les migrations puissent ajouter une section sans toucher aux autres.
        // ---------------------------------------------------------------------

        // Vers un objet JSON. N'inclut PAS les champs vides (on omet
        // un string vide et un vecteur vide pour garder le fichier propre).
        nlohmann::json ToJson() const;

        // Depuis un objet JSON. Si le JSON est corrompu, throws
        // nlohmann::json::exception (l'appelant doit catch et fallback).
        static PlayerData FromJson(const nlohmann::json& j);

        // Helper : timestamp ISO-8601 UTC. Utilisé dans ToJson.
        static std::string FormatTime(std::chrono::system_clock::time_point tp);
        static std::chrono::system_clock::time_point ParseTime(const std::string& iso);
    };
}

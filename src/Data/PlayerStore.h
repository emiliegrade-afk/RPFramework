// ============================================================================
// RPFramework - Data / PlayerStore
//
// Stockage persistant des données joueurs (GDD §20) :
//   - 1 fichier JSON par joueur sous `players/{id}.json`
//   - Écriture atomique (.tmp + rename)
//   - Backups rotatifs (.bak.1 → .bak.N)
//   - Versionnage + migration à la lecture
//   - Listing de tous les joueurs connus
//
// Thread-safe : mutex global. Les opérations sont rapides (1-2 I/O) donc
// le lock n'est pas un goulot d'étranglement.
// ============================================================================
#pragma once

#include "Data/PlayerData.h"
#include "Security/Types.h"  // PlayerId

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace rpframework::data
{
    // Résultat explicite d'une lecture. Il évite qu'un appelant confonde un
    // fichier absent avec un fichier corrompu et écrase ce dernier par erreur.
    enum class PlayerLoadStatus
    {
        Loaded,
        RecoveredFromBackup,
        Missing,
        Corrupt,
        Unavailable,
    };

    struct PlayerLoadResult
    {
        PlayerLoadStatus status = PlayerLoadStatus::Unavailable;
        std::optional<PlayerData> data;

        bool HasData() const
        {
            return status == PlayerLoadStatus::Loaded
                || status == PlayerLoadStatus::RecoveredFromBackup;
        }
    };

    class PlayerStore
    {
    public:
        // Initialisation. Doit être appelé par PluginContext.
        // - Crée le répertoire `players/` au besoin
        // - Charge la config (save_dir, backup_count)
        static void Initialize();
        static void Shutdown();

        // Recharge la config depuis Config::Get().Root()["data"].
        static void LoadFromConfig();

        // -----------------------------------------------------------------
        // API publique
        // -----------------------------------------------------------------

        // Charge le joueur. Si le fichier n'existe pas, renvoie un
        // PlayerData par défaut (id + name) sans toucher au disque.
        // Si le fichier existe mais est corrompu, log un warning et
        // renvoie un défaut (on n'écrase pas le fichier corrompu tout
        // de suite : c'est à l'appelant de Save() pour le récupérer).
        static PlayerData LoadOrCreate(PlayerId id, const std::string& name = "");

        // Variante stricte : renvoie nullopt si le fichier n'existe pas.
        // Utilisé par les tests.
        static std::optional<PlayerData> Load(PlayerId id);

        // Variante détaillée : distingue une absence normale d'une
        // corruption. Si le fichier principal est invalide, tente de
        // restaurer le backup valide le plus récent sans jamais supprimer le
        // fichier défectueux (il est mis en quarantaine).
        static PlayerLoadResult LoadDetailed(PlayerId id);

        // Sauvegarde atomique avec rotation des backups.
        // - Écrit dans {file}.tmp puis rename atomique
        // - Avant d'écraser, tourne les backups : .bak.(N-1) → .bak.N, ..., .bak → .bak.1
        //   puis copie l'ancien {file} → {file}.bak
        // Renvoie true en cas de succès.
        static bool Save(PlayerData& data);

        // Charge puis réécrit le profil (touch `updatedAt`). Utilisé au
        // logout pour forcer une sauvegarde finale. No-op si absent.
        static bool Flush(PlayerId id);

        // Supprime le fichier joueur et tous ses backups. Pas utilisé
        // en runtime normal (GDD : on ne supprime pas les données
        // joueurs), mais utile pour /admin ou les tests.
        static bool Delete(PlayerId id);

        // Le fichier existe-t-il (sans le charger) ?
        static bool Exists(PlayerId id);

        // Liste tous les IDs joueurs connus (fichiers .json dans le
        // répertoire players/). Les .tmp et .bak sont ignorés.
        static std::vector<PlayerId> ListAll();

        // Renvoie le répertoire de stockage.
        static std::filesystem::path GetSaveDir();

        // Renvoie le chemin du fichier joueur. N'existe pas forcément.
        static std::filesystem::path GetFilePath(PlayerId id);

        // Backup config (lecture seule).
        static int GetBackupCount();

        static bool IsReady();

        // Verrou exclusif du store (mutex récursif) : tient le cycle
        // Load → mutate → Save pour un ou plusieurs joueurs.
        class ExclusiveLock
        {
        public:
            ExclusiveLock();
            ExclusiveLock(const ExclusiveLock&) = delete;
            ExclusiveLock& operator=(const ExclusiveLock&) = delete;
        private:
            std::lock_guard<std::recursive_mutex> guard_;
        };

    private:
        PlayerStore() = default;

        static PlayerStore& Instance();

        // Supprime/renomme les backups pour faire de la place au nouveau.
        // Suppose mutex_ tenu.
        void RotateBackupsForFile(const std::filesystem::path& file);
        bool WriteJsonTemp(const std::filesystem::path& target,
                           const nlohmann::json& data);
        bool QuarantineAndRestore(const std::filesystem::path& corrupted,
                                  const nlohmann::json& recovered,
                                  PlayerId id);
        void ApplyDataConfigLocked();

        mutable std::recursive_mutex mutex_;
        std::filesystem::path saveDir_;
        int                   backupCount_ = 3;
        bool                  initialized_ = false;
    };
}

// ============================================================================
// RPFramework - Data / PlayerStore - implémentation
// ============================================================================
#include "Data/PlayerStore.h"
#include "Data/Migration.h"

#include "Core/Config.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Security/AuditLog.h"

#include "json.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace rpframework::data
{
    // Réexport local du type pour éviter de tout qualifier dans le .cpp.
    using PlayerId = rpframework::security::PlayerId;
    PlayerStore& PlayerStore::Instance()
    {
        static PlayerStore inst;
        return inst;
    }

    // -------------------------------------------------------------------------
    // Cycle de vie
    // -------------------------------------------------------------------------

    void PlayerStore::Initialize()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        if (self.initialized_)
        {
            return;
        }

        // saveDir par défaut : <plugin>/players/
        self.saveDir_ = rpframework::core::GetPluginDir() / "players";

        // Charge la config (peut override saveDir, backupCount).
        // Note : on ne lock pas Config ici (singleton thread-safe).
        if (const auto& cfg = rpframework::core::Config::Get().Root();
            cfg.is_object() && cfg.contains("data"))
        {
            const auto& d = cfg["data"];
            if (d.contains("save_dir") && d["save_dir"].is_string())
            {
                const auto custom = std::filesystem::path(d["save_dir"].get<std::string>());
                if (!custom.empty())
                {
                    self.saveDir_ = custom;
                }
            }
            if (d.contains("backup_count") && d["backup_count"].is_number_integer())
            {
                const int v = d["backup_count"].get<int>();
                // Au moins un backup est requis : sans lui, un arrêt ou une
                // corruption pendant une écriture ne laisse aucun filet de
                // sécurité exploitable.
                if (v >= 1)
                {
                    self.backupCount_ = v;
                }
                else
                {
                    rpframework::core::LogWarn("PlayerStore: backup_count={} invalide; valeur securisee 1 utilisee.", v);
                    self.backupCount_ = 1;
                }
            }
        }

        if (!rpframework::core::EnsureDirectoryExists(self.saveDir_))
        {
            rpframework::core::LogError("PlayerStore: impossible de creer {}. Persistance desactivee.",
                self.saveDir_.string());
            // On reste initialized_ = false : les Load/Save échoueront
            // proprement.
            return;
        }

        self.initialized_ = true;
        rpframework::core::LogInfo("PlayerStore: pret (saveDir={}, backup_count={}).",
            self.saveDir_.string(), self.backupCount_);
    }

    void PlayerStore::LoadFromConfig()
    {
        // Force un reload en détruisant/réinitialisant.
        auto& self = Instance();
        {
            std::lock_guard<std::mutex> lock(self.mutex_);
            self.initialized_ = false;
        }
        Initialize();
    }

    void PlayerStore::Shutdown()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.initialized_ = false;
        rpframework::core::LogInfo("PlayerStore: shutdown.");
    }

    // -------------------------------------------------------------------------
    // Paths
    // -------------------------------------------------------------------------

    std::filesystem::path PlayerStore::GetSaveDir()
    {
        return Instance().saveDir_;
    }

    std::filesystem::path PlayerStore::GetFilePath(PlayerId id)
    {
        return Instance().saveDir_ / (std::to_string(id) + ".json");
    }

    int PlayerStore::GetBackupCount()
    {
        return Instance().backupCount_;
    }

    // -------------------------------------------------------------------------
    // Existence / listing
    // -------------------------------------------------------------------------

    bool PlayerStore::Exists(PlayerId id)
    {
        std::error_code ec;
        return std::filesystem::exists(GetFilePath(id), ec);
    }

    std::vector<PlayerId> PlayerStore::ListAll()
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        std::vector<PlayerId> out;
        if (!self.initialized_ || !std::filesystem::exists(self.saveDir_))
        {
            return out;
        }

        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(self.saveDir_, ec);
             it != std::filesystem::directory_iterator();
             it.increment(ec))
        {
            if (ec) break;
            const auto& entry = *it;
            if (!entry.is_regular_file()) continue;
            const auto& path = entry.path();
            if (path.extension() != ".json") continue;
            // Skip .tmp et .bak.* (leurs extensions ne matchent pas .json
            // sauf .bak.1.json — on gère ça en filtrant le nom).
            const auto fname = path.stem().string();  // ex "1234" pour "1234.json"
            if (fname.find('.') != std::string::npos) continue;  // contient un point = .bak.x ou .tmp

            try
            {
                const auto id = static_cast<PlayerId>(std::stoull(fname));
                out.push_back(id);
            }
            catch (...) { /* nom non numérique, on ignore */ }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // -------------------------------------------------------------------------
    // Load
    // -------------------------------------------------------------------------

    PlayerLoadResult PlayerStore::LoadDetailed(PlayerId id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        if (!self.initialized_) return {PlayerLoadStatus::Unavailable, std::nullopt};

        const auto path = self.saveDir_ / (std::to_string(id) + ".json");
        if (!std::filesystem::exists(path))
        {
            return {PlayerLoadStatus::Missing, std::nullopt};
        }

        auto readFile = [id](const std::filesystem::path& source,
                             nlohmann::json& parsed,
                             PlayerData& player,
                             bool& migrated,
                             std::string& error) -> bool
        {
            try
            {
                std::ifstream f(source, std::ios::binary);
                if (!f.is_open())
                {
                    error = "impossible d'ouvrir le fichier";
                    return false;
                }
                f >> parsed;
                if (!parsed.is_object())
                {
                    error = "racine JSON invalide";
                    return false;
                }

                const int fromVersion = parsed.value("/meta/schema_version"_json_pointer, 0);
                migrated = Migrate(parsed, fromVersion);
                player = PlayerData::FromJson(parsed);
                if (player.id != id)
                {
                    error = "identifiant interne different du nom de fichier";
                    return false;
                }
                return true;
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
                return false;
            }
        };

        nlohmann::json parsed;
        PlayerData player;
        bool migrated = false;
        std::string error;
        if (readFile(path, parsed, player, migrated, error))
        {
            // Une migration doit être durable : sinon elle serait rejouée à
            // chaque démarrage et une panne suivante reviendrait en arrière.
            if (migrated)
            {
                if (!self.WriteJsonTemp(path, parsed))
                {
                    rpframework::core::LogError("PlayerStore: preparation de la migration de {} echouee.", path.string());
                    return {PlayerLoadStatus::Unavailable, std::nullopt};
                }
                self.RotateBackupsForFile(path);
                std::error_code ec;
                std::filesystem::rename(path.string() + ".tmp", path, ec);
                if (ec)
                {
                    const auto backup = path.string() + ".bak.1";
                    std::error_code rollback;
                    std::filesystem::rename(backup, path, rollback);
                    rpframework::core::LogError("PlayerStore: persistance de la migration de {} echouee.", path.string());
                    return {PlayerLoadStatus::Unavailable, std::nullopt};
                }
            }
            return {PlayerLoadStatus::Loaded, std::move(player)};
        }

        rpframework::core::LogError("PlayerStore: donnees corrompues dans {}: {}.", path.string(), error);
        rpframework::security::AuditLog::Log("player.data_corrupt", id, {
            {"file", path.filename().string()}, {"reason", error},
        }, rpframework::security::audit_severity::kError);

        // Le backup .bak.1 est le plus récent. On l'essaie avant les plus
        // anciens et on ne remet le fichier en service qu'après validation.
        for (int i = 1; i <= self.backupCount_; ++i)
        {
            const auto backup = self.saveDir_ / (std::to_string(id) + ".json.bak." + std::to_string(i));
            if (!std::filesystem::exists(backup)) continue;

            nlohmann::json backupJson;
            PlayerData backupPlayer;
            bool backupMigrated = false;
            std::string backupError;
            if (!readFile(backup, backupJson, backupPlayer, backupMigrated, backupError))
            {
                rpframework::core::LogWarn("PlayerStore: backup invalide ignore {}: {}", backup.string(), backupError);
                continue;
            }
            if (self.QuarantineAndRestore(path, backupJson, id))
            {
                rpframework::security::AuditLog::Log("player.data_recovered", id, {
                    {"source", backup.filename().string()},
                }, rpframework::security::audit_severity::kWarn);
                return {PlayerLoadStatus::RecoveredFromBackup, std::move(backupPlayer)};
            }
            return {PlayerLoadStatus::Unavailable, std::nullopt};
        }

        return {PlayerLoadStatus::Corrupt, std::nullopt};
    }

    std::optional<PlayerData> PlayerStore::Load(PlayerId id)
    {
        auto result = LoadDetailed(id);
        return result.HasData() ? std::move(result.data) : std::nullopt;
    }

    PlayerData PlayerStore::LoadOrCreate(PlayerId id, const std::string& name)
    {
        auto result = LoadDetailed(id);
        if (result.HasData())
        {
            return std::move(*result.data);
        }
        if (result.status != PlayerLoadStatus::Missing)
        {
            // Cette API historique ne peut pas transmettre l'erreur. Elle ne
            // sauvegarde jamais le résultat; les nouveaux appelants doivent
            // utiliser LoadDetailed pour ne pas traiter une corruption comme
            // un nouveau joueur.
            rpframework::core::LogWarn("PlayerStore: LoadOrCreate({}) appele apres une lecture non saine.", id);
        }

        PlayerData d;
        d.id        = id;
        d.name      = name;
        d.createdAt = std::chrono::system_clock::now();
        d.updatedAt = d.createdAt;
        return d;
    }

    // -------------------------------------------------------------------------
    // Save (atomique + rotation backups)
    // -------------------------------------------------------------------------

    bool PlayerStore::WriteJsonTemp(const std::filesystem::path& target,
                                    const nlohmann::json& json)
    {
        const auto tmp = target.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
            if (!out.is_open())
            {
                return false;
            }
            out << json.dump(2);
            out.flush();
            if (!out.good()) return false;
        }
        return true;
    }

    bool PlayerStore::QuarantineAndRestore(const std::filesystem::path& corrupted,
                                           const nlohmann::json& recovered,
                                           PlayerId id)
    {
        const auto temp = corrupted.string() + ".recovery.tmp";
        try
        {
            {
                std::ofstream out(temp, std::ios::trunc | std::ios::binary);
                if (!out.is_open()) return false;
                out << recovered.dump(2);
                out.flush();
                if (!out.good()) return false;
            }

            const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const auto quarantine = corrupted.string() + ".corrupt." + std::to_string(stamp);
            std::error_code ec;
            std::filesystem::rename(corrupted, quarantine, ec);
            if (ec)
            {
                std::filesystem::remove(temp, ec);
                rpframework::core::LogError("PlayerStore: mise en quarantaine de {} echouee: {}", corrupted.string(), ec.message());
                return false;
            }
            std::filesystem::rename(temp, corrupted, ec);
            if (ec)
            {
                // Le fichier défectueux reste récupérable; on restaure son
                // emplacement original si possible afin de ne rien perdre.
                std::error_code rollback;
                std::filesystem::rename(quarantine, corrupted, rollback);
                std::filesystem::remove(temp, rollback);
                rpframework::core::LogError("PlayerStore: restauration de {} echouee: {}", corrupted.string(), ec.message());
                return false;
            }
            rpframework::core::LogWarn("PlayerStore: id={} recupere; original conserve dans {}.", id, quarantine);
            return true;
        }
        catch (...)
        {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Save (atomique + rotation backups)
    // -------------------------------------------------------------------------

    bool PlayerStore::Save(PlayerData& data)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        if (!self.initialized_) return false;

        // updatedAt = now
        data.updatedAt = std::chrono::system_clock::now();
        if (data.createdAt.time_since_epoch().count() == 0)
        {
            data.createdAt = data.updatedAt;  // tout nouveau joueur
        }
        data.schemaVersion = kCurrentSchemaVersion;

        const auto path = self.saveDir_ / (std::to_string(data.id) + ".json");

        try
        {
            // Prépare le nouveau contenu avant de modifier l'ancien : une
            // erreur d'écriture laisse le profil actuel intact.
            if (!self.WriteJsonTemp(path, data.ToJson()))
            {
                rpframework::core::LogError("PlayerStore: preparation de {} a echoue.", path.string());
                return false;
            }

            if (std::filesystem::exists(path))
            {
                self.RotateBackupsForFile(path);
            }

            std::error_code ec;
            std::filesystem::rename(path.string() + ".tmp", path, ec);
            if (ec)
            {
                // L'ancien profil se trouve dans le backup le plus récent.
                const auto backup = path.string() + ".bak.1";
                std::error_code rollback;
                std::filesystem::rename(backup, path, rollback);
                std::filesystem::remove(path.string() + ".tmp", rollback);
                rpframework::core::LogError("PlayerStore: validation atomique de {} a echoue.", path.string());
                return false;
            }
            return true;
        }
        catch (const std::exception& ex)
        {
            rpframework::core::LogError("PlayerStore: sauvegarde de id={} echouee: {}",
                data.id, ex.what());
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // Delete
    // -------------------------------------------------------------------------

    bool PlayerStore::Delete(PlayerId id)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        if (!self.initialized_) return false;

        const auto base = self.saveDir_ / std::to_string(id);
        bool any = false;
        std::error_code ec;

        // Supprime le fichier principal et les backups.
        std::vector<std::string> suffixes = {".json", ".json.tmp", ".json.bak"};
        for (int i = 1; i <= self.backupCount_; ++i)
        {
            suffixes.push_back(".json.bak." + std::to_string(i));
        }
        for (const auto& s : suffixes)
        {
            const auto p = self.saveDir_ / (std::to_string(id) + s);
            if (std::filesystem::exists(p, ec))
            {
                if (std::filesystem::remove(p, ec))
                {
                    any = true;
                }
            }
        }
        if (any)
        {
            rpframework::core::LogInfo("PlayerStore: id={} supprime.", id);
        }
        return any;
    }

    // -------------------------------------------------------------------------
    // Backup rotation
    // -------------------------------------------------------------------------

    void PlayerStore::RotateBackupsForFile(const std::filesystem::path& file)
    {
        // On tourne : .bak.(N-1) → .bak.N, ..., .bak → .bak.1
        // Puis le fichier courant → .bak
        //
        // Algorithme : on procède de l'arrière vers l'avant pour ne pas
        // écraser une cible qui contient encore des données.
        //
        // Note : on utilise un suffixe ".json.bak" pour le fichier courant
        // (renommé après rotation) puis ".json.bak.1" etc. pour les anciens.

        const std::string base  = file.filename().string();        // "1234.json"
        const auto dir           = file.parent_path();

        // 1. Supprimer le backup le plus ancien.
        const auto oldest = dir / (base + ".bak." + std::to_string(backupCount_));
        std::error_code ec;
        std::filesystem::remove(oldest, ec);
        ec.clear();

        // 2. Décaler les backups existants.
        for (int i = backupCount_ - 1; i >= 1; --i)
        {
            const auto from = dir / (base + ".bak." + std::to_string(i));
            const auto to   = dir / (base + ".bak." + std::to_string(i + 1));
            std::filesystem::rename(from, to, ec);
            ec.clear();
        }

        // 3. Déplacer l'actuel → .bak (le .bak sera ensuite renommé en .bak.1
        //    au prochain tour, mais ici on fait : actuel → .bak, puis on
        //    renomme .bak → .bak.1 dans la même étape).
        const auto bakSlot = dir / (base + ".bak");
        std::filesystem::rename(file, bakSlot, ec);
        ec.clear();

        // 4. Premier shift : .bak → .bak.1
        const auto bak1 = dir / (base + ".bak.1");
        std::filesystem::rename(bakSlot, bak1, ec);
        if (ec)
        {
            rpframework::core::LogWarn("PlayerStore: rotation backup partielle ({} -> {}): {}",
                bakSlot.string(), bak1.string(), ec.message());
        }
    }
}

// ============================================================================
// RPFramework - Security / AuditLog - implémentation
// ============================================================================
#include "Security/AuditLog.h"

#include "Core/Logger.h"
#include "Core/Paths.h"

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace rpframework::security
{
    namespace
    {
        // Sérialise un time_point en ISO-8601 UTC.
        std::string FormatTimestamp(std::chrono::system_clock::time_point tp)
        {
            const auto t  = std::chrono::system_clock::to_time_t(tp);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                tp.time_since_epoch()).count() % 1000;

            std::tm tm_utc{};
#if defined(_WIN32)
            gmtime_s(&tm_utc, &t);
#else
            gmtime_r(&t, &tm_utc);
#endif
            std::ostringstream oss;
            oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S")
                << '.' << std::setw(3) << std::setfill('0') << ms << 'Z';
            return oss.str();
        }

        // Sérialise une Entry en JSON-line.
        std::string SerializeEntry(const AuditLog::Entry& e)
        {
            nlohmann::json j;
            j["ts"]        = FormatTimestamp(e.timestamp);
            j["severity"]  = e.severity;
            j["action"]    = e.action;
            j["player_id"] = (e.playerId == 0)
                                ? nlohmann::json(nullptr)
                                : nlohmann::json(std::to_string(e.playerId));
            j["payload"]   = e.payload;
            return j.dump();
        }
    }

    AuditLog& AuditLog::Instance()
    {
        static AuditLog inst;
        return inst;
    }

    void AuditLog::Initialize()
    {
        Instance().InitializeImpl();
    }

    void AuditLog::InitializeImpl()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_)
        {
            return;
        }
        filePath_    = rpframework::core::GetAuditLogPath();
        const auto dir = filePath_.parent_path();
        if (!dir.empty() && !rpframework::core::EnsureDirectoryExists(dir))
        {
            rpframework::core::LogWarn("AuditLog: impossible de creer le repertoire {}. Audit desactive.", dir.string());
            enabled_ = false;
            return;
        }
        initialized_ = true;
        rpframework::core::LogInfo("AuditLog: pret (fichier={}, max_buffer={}, flush_every={}).",
                filePath_.string(), maxBuffer_, flushEvery_);
    }

    void AuditLog::LoadFromConfig(const nlohmann::json& config)
    {
        Instance().LoadFromConfigImpl(config);
    }

    void AuditLog::LoadFromConfigImpl(const nlohmann::json& config)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!config.is_object())
        {
            return;
        }

        // enabled
        const auto itEnabled = config.find("audit_log_enabled");
        if (itEnabled != config.end() && itEnabled->is_boolean())
        {
            enabled_ = itEnabled->get<bool>();
        }

        // max_buffer
        const auto itMax = config.find("audit_max_buffer");
        if (itMax != config.end() && itMax->is_number_integer())
        {
            const int v = itMax->get<int>();
            maxBuffer_ = (v > 0) ? static_cast<std::size_t>(v) : 1;
        }

        // flush_every
        const auto itFlush = config.find("audit_flush_every");
        if (itFlush != config.end() && itFlush->is_number_integer())
        {
            const int v = itFlush->get<int>();
            flushEvery_ = (v > 0) ? static_cast<std::size_t>(v) : 1;
        }

        // rotation: audit_max_size_mb
        const auto itSize = config.find("audit_max_size_mb");
        if (itSize != config.end() && itSize->is_number())
        {
            const double mb = itSize->get<double>();
            if (mb > 0.0)
            {
                rotateConfig_.maxSizeBytes =
                    static_cast<std::uint64_t>(mb * 1024.0 * 1024.0);
            }
        }

        // rotation: audit_max_files
        const auto itFiles = config.find("audit_max_files");
        if (itFiles != config.end() && itFiles->is_number_integer())
        {
            const int v = itFiles->get<int>();
            rotateConfig_.maxFiles = (v >= 1) ? v : 1;
        }

        rpframework::core::LogInfo("AuditLog: config chargee (enabled={}, max_buffer={}, flush_every={}, rotate_at={}MB, keep={}fichiers).",
                enabled_ ? "true" : "false", maxBuffer_, flushEvery_,
                rotateConfig_.maxSizeBytes / (1024ull * 1024ull),
                rotateConfig_.maxFiles);
    }

    void AuditLog::Log(std::string_view action,
                       PlayerId playerId,
                       nlohmann::json payload,
                       std::string_view severity)
    {
        Instance().LogImpl(action, playerId, std::move(payload), severity);
    }

    void AuditLog::LogImpl(std::string_view action,
                           PlayerId playerId,
                           nlohmann::json payload,
                           std::string_view severity)
    {
        if (!initialized_)
        {
            return;
        }

        Entry e;
        e.timestamp = std::chrono::system_clock::now();
        e.severity  = std::string(severity);
        e.action    = std::string(action);
        e.playerId  = playerId;
        e.payload   = std::move(payload);

        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Ring buffer en mémoire.
            buffer_.push_back(std::move(e));
            while (buffer_.size() > maxBuffer_)
            {
                buffer_.pop_front();
            }

            // Flush si seuil atteint. On flush SOUS le lock mutex_ pour
            // rester simple : FlushImplLocked() est non-récursif et rapide
            // (un append + fwrite par N entrées), donc le coût de bloquer
            // les autres Log() pendant le flush est négligeable. Évite
            // toute la machinerie d'un worker thread dédié pour Phase 2.
            if (enabled_ && buffer_.size() >= flushEvery_)
            {
                FlushImplLocked();
            }
        }
    }

    void AuditLog::LogDenied(std::string_view action,
                              PlayerId playerId,
                              std::string_view reason,
                              nlohmann::json context)
    {
        nlohmann::json payload = std::move(context);
        payload["reason"] = std::string(reason);
        Log(action, playerId, std::move(payload), audit_severity::kDenied);
    }

    void AuditLog::Flush()
    {
        Instance().FlushImpl();
    }

    void AuditLog::FlushImpl()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        FlushImplLocked();
    }

    // Helper privé : suppose mutex_ tenu.
    void AuditLog::FlushImplLocked()
    {
        if (!enabled_ || !initialized_ || buffer_.empty())
        {
            return;
        }

        try
        {
            std::ofstream out(filePath_, std::ios::app | std::ios::binary);
            if (!out.is_open())
            {
                rpframework::core::LogError("AuditLog: impossible d'ouvrir {} en append.", filePath_.string());
                return;
            }
            for (const auto& e : buffer_)
            {
                out << SerializeEntry(e) << '\n';
            }
            out.flush();
            out.close();
            buffer_.clear();
        }
        catch (const std::exception& ex)
        {
            rpframework::core::LogError("AuditLog: erreur de flush: {}", ex.what());
            return;
        }

        // Rotation : on la déclenche après le flush (fichier fermé) et
        // toujours sous le lock pour éviter qu'un autre Log() vienne
        // écrire dedans pendant qu'on renomme.
        RotateIfNeededImpl();
    }

    void AuditLog::Shutdown()
    {
        Instance().ShutdownImpl();
    }

    void AuditLog::ShutdownImpl()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_)
        {
            rpframework::core::LogInfo("AuditLog: shutdown (flush final).");
            FlushImplLocked();
            initialized_ = false;
        }
    }

    std::vector<AuditLog::Entry> AuditLog::Recent(std::size_t max)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);

        const std::size_t n = std::min(max, self.buffer_.size());
        std::vector<Entry> out;
        out.reserve(n);
        // Copie les N plus récents (en partant de la fin).
        auto it = self.buffer_.rbegin();
        for (std::size_t i = 0; i < n && it != self.buffer_.rend(); ++i, ++it)
        {
            out.push_back(*it);
        }
        return out;
    }

    bool AuditLog::IsEnabled()
    {
        return Instance().enabled_;
    }

    std::filesystem::path AuditLog::LogFilePath()
    {
        return Instance().filePath_;
    }

    AuditLog::RotateConfig AuditLog::GetRotateConfig()
    {
        return Instance().rotateConfig_;
    }

    void AuditLog::SetRotateConfig(const RotateConfig& cfg)
    {
        auto& self = Instance();
        std::lock_guard<std::mutex> lock(self.mutex_);
        self.rotateConfig_ = cfg;
    }

    bool AuditLog::RotateIfNeeded()
    {
        return Instance().RotateIfNeededImpl();
    }

    std::filesystem::path AuditLog::MakeRotatedPath(int n) const
    {
        // n == 0  → fichier courant (audit.log)
        // n == 1  → audit.log.1
        // n == 2  → audit.log.2
        if (n <= 0)
        {
            return filePath_;
        }
        return std::filesystem::path(filePath_.string() + "." + std::to_string(n));
    }

    bool AuditLog::RotateIfNeededImpl()
    {
        // mutex_ doit être tenu par l'appelant.
        if (!enabled_ || !initialized_ || filePath_.empty())
        {
            return false;
        }
        if (rotateConfig_.maxSizeBytes == 0 || rotateConfig_.maxFiles <= 0)
        {
            return false;
        }

        std::error_code ec;
        const auto size = std::filesystem::file_size(filePath_, ec);
        if (ec || size < rotateConfig_.maxSizeBytes)
        {
            return false;
        }

        // 1. Supprimer le plus ancien (audit.log.N où N = maxFiles).
        if (rotateConfig_.maxFiles >= 1)
        {
            const auto oldest = MakeRotatedPath(rotateConfig_.maxFiles);
            std::filesystem::remove(oldest, ec);
            ec.clear();
        }

        // 2. Décaler les archives : .(N-1) → .N, ..., .1 → .2.
        for (int i = rotateConfig_.maxFiles - 1; i >= 1; --i)
        {
            const auto from = MakeRotatedPath(i);
            const auto to   = MakeRotatedPath(i + 1);
            std::filesystem::rename(from, to, ec);
            ec.clear();
        }

        // 3. Le fichier courant devient .1.
        const auto first = MakeRotatedPath(1);
        std::filesystem::rename(filePath_, first, ec);
        if (ec)
        {
            rpframework::core::LogError("AuditLog: rotation echouee ({} -> {}): {}",
                filePath_.string(), first.string(), ec.message());
            return false;
        }

        rpframework::core::LogInfo("AuditLog: rotation effectuée ({} → {}, taille={} octets).",
            filePath_.string(), first.string(), size);
        return true;
    }
}

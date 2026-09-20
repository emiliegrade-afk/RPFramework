// ============================================================================
// RPFramework - Mod / Bridge - implémentation
// ============================================================================
#include "Mod/Bridge.h"

#include "Core/Config.h"
#include "Core/PluginContext.h"
#include "Data/PlayerStore.h"
#include "Faction/World.h"
#include "Progression/Professions.h"
#include "Quest/Commands.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"

#include "json.hpp"

#include <cctype>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace rpframework::mod
{
    namespace
    {
        std::mutex g_mutex;
        bool g_ready = false;
        std::unordered_set<PlayerId> g_sessionAdmins;

        std::string Lower(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        BridgeResult Ok(std::string message)
        {
            return {true, true, std::move(message)};
        }

        BridgeResult Fail(std::string message)
        {
            return {true, false, std::move(message)};
        }

        bool ConstantTimeEquals(std::string_view a, std::string_view b)
        {
            const std::size_t n = a.size() > b.size() ? a.size() : b.size();
            unsigned char diff = static_cast<unsigned char>(a.size() != b.size() ? 1 : 0);
            for (std::size_t i = 0; i < n; ++i)
            {
                const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
                const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
                diff = static_cast<unsigned char>(diff | (ca ^ cb));
            }
            return diff == 0;
        }

        security::Level EffectiveLevel(PlayerId player)
        {
            const auto stored = security::Permissions::GetPlayerLevel(player);
            if (IsSessionAdmin(player)
                && static_cast<std::uint8_t>(stored)
                   < static_cast<std::uint8_t>(security::Level::ADMIN))
            {
                return security::Level::ADMIN;
            }
            return stored;
        }

        bool CheckKeyFor(PlayerId player, security::Level level, std::string_view key)
        {
            if (security::Permissions::Check(level, key))
            {
                return true;
            }
            security::AuditLog::LogDenied(std::string(key), player, "permission");
            return false;
        }

        std::string Usage()
        {
            return "Usage: rpf ping | auth <code> | race list|select <id> | "
                   "job list|select <id> | player status | mod get <json.path> | "
                   "crime report <id> <0|1> | location canenter <id> | npc hostile <faction>";
        }

        BridgeResult FromQuest(const quest::CommandResult& result)
        {
            return {true, result.success, result.message};
        }

        BridgeResult HandlePing(PlayerId player, security::Level level)
        {
            if (!CheckKeyFor(player, level, "rpf.ping"))
            {
                return Fail("permission refusee");
            }
            return Ok("RPFramework OK");
        }

        BridgeResult HandleAuth(PlayerId player, security::Level level,
                                const std::vector<std::string>& args)
        {
            if (player == 0)
            {
                return Fail("joueur inconnu");
            }
            if (!CheckKeyFor(player, level, "rpf.auth"))
            {
                return Fail("permission refusee");
            }
            if (args.empty())
            {
                return Fail("Usage: rpf auth <code>");
            }
            if (!security::RateLimiter::Allow(player, "rpf.auth"))
            {
                security::AuditLog::LogDenied("rpf.auth", player, "rate_limit");
                return Fail("trop de tentatives");
            }

            const auto expected = core::Config::Get().GetOr<std::string>(
                "security.admin_code", "");
            if (expected.empty())
            {
                security::AuditLog::LogDenied("rpf.auth", player, "disabled");
                return Fail("auth desactivee");
            }

            // Un seul token : le tokenizer a déjà regroupé les guillemets.
            const std::string& submitted = args[0];
            if (!ConstantTimeEquals(submitted, expected))
            {
                security::AuditLog::LogDenied("rpf.auth", player, "bad_code");
                return Fail("code invalide");
            }

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_sessionAdmins.insert(player);
            }
            security::AuditLog::Log("rpf.auth", player, {{"ok", true}});
            return Ok("auth ok");
        }

        BridgeResult HandleRace(PlayerId player, security::Level level,
                                const std::vector<std::string>& args)
        {
            const std::string action = args.empty() ? "list" : Lower(args[0]);
            if (action == "list")
            {
                if (!CheckKeyFor(player, level, "rpf.race.list"))
                {
                    return Fail("permission refusee");
                }
                return FromQuest(quest::HandleCommand(player, {"race", "list"}, level));
            }
            if (action == "select")
            {
                if (args.size() < 2)
                {
                    return Fail("Usage: rpf race select <id>");
                }
                if (!CheckKeyFor(player, level, "rpf.race.select"))
                {
                    return Fail("permission refusee");
                }
                return FromQuest(quest::HandleCommand(
                    player, {"race", "select", args[1]}, level));
            }
            return Fail("Usage: rpf race list|select <id>");
        }

        BridgeResult HandleJob(PlayerId player, security::Level level,
                               const std::vector<std::string>& args)
        {
            const std::string action = args.empty() ? "list" : Lower(args[0]);
            if (action == "list")
            {
                if (!CheckKeyFor(player, level, "rpf.job.list"))
                {
                    return Fail("permission refusee");
                }
                return FromQuest(quest::HandleCommand(
                    player, {"profession", "list"}, level));
            }
            if (action == "select")
            {
                if (args.size() < 2)
                {
                    return Fail("Usage: rpf job select <id>");
                }
                if (!CheckKeyFor(player, level, "rpf.job.select"))
                {
                    return Fail("permission refusee");
                }
                return FromQuest(quest::HandleCommand(
                    player, {"profession", "select", args[1]}, level));
            }
            return Fail("Usage: rpf job list|select <id>");
        }

        BridgeResult HandlePlayer(PlayerId player, security::Level level,
                                  const std::vector<std::string>& args)
        {
            const std::string action = args.empty() ? "status" : Lower(args[0]);
            if (action != "status")
            {
                return Fail("Usage: rpf player status");
            }
            if (!CheckKeyFor(player, level, "rpf.player.status"))
            {
                return Fail("permission refusee");
            }

            nlohmann::json out;
            out["race"] = "";
            out["profession"] = "";
            out["level"] = 1;
            out["xp"] = 0;
            out["job_level"] = 1;
            out["job_xp"] = 0;

            auto load = data::PlayerStore::LoadDetailed(player);
            if (load.status == data::PlayerLoadStatus::Corrupt
                || load.status == data::PlayerLoadStatus::Unavailable)
            {
                return Fail("profil indisponible");
            }
            if (load.HasData())
            {
                const auto& d = *load.data;
                out["race"] = d.race;
                out["profession"] = d.profession;
                out["level"] = d.level;
                out["xp"] = d.xp;
                if (!d.profession.empty())
                {
                    out["job_level"] = progression::GetProfessionLevel(player, d.profession);
                    out["job_xp"] = progression::GetProfessionXp(player, d.profession);
                }
            }
            return Ok(out.dump());
        }

        BridgeResult HandleMod(PlayerId player, security::Level level,
                               const std::vector<std::string>& args)
        {
            if (args.empty())
            {
                return Fail("Usage: rpf mod get <json.path>");
            }
            const std::string action = Lower(args[0]);
            if (action != "get")
            {
                return Fail("Usage: rpf mod get <json.path>");
            }
            if (!CheckKeyFor(player, level, "rpf.mod.get"))
            {
                return Fail("permission refusee");
            }
            if (args.size() < 2)
            {
                return Fail("Usage: rpf mod get <json.path>");
            }
            std::vector<std::string> forwarded;
            forwarded.emplace_back("mod");
            forwarded.insert(forwarded.end(), args.begin(), args.end());
            return FromQuest(quest::HandleCommand(player, forwarded, level));
        }

        bool ValidWorldId(const std::string& id)
        {
            if (id.empty() || id.size() > 64) return false;
            for (unsigned char c : id)
            {
                if (!std::isalnum(c) && c != '_' && c != '-') return false;
            }
            return true;
        }

        bool ParseWitnessed(const std::string& raw, bool& out)
        {
            const auto value = Lower(raw);
            if (value == "1" || value == "true" || value == "yes"
                || value == "seen" || value == "witnessed")
            {
                out = true;
                return true;
            }
            if (value == "0" || value == "false" || value == "no"
                || value == "unseen")
            {
                out = false;
                return true;
            }
            return false;
        }

        BridgeResult HandleCrime(PlayerId player, security::Level level,
                                 const std::vector<std::string>& args)
        {
            if (!CheckKeyFor(player, level, "rpf.crime.report"))
                return Fail("permission refusee");
            if (args.size() < 3 || Lower(args[0]) != "report")
                return Fail("Usage: rpf crime report <id> <0|1>");
            if (!ValidWorldId(args[1]))
                return Fail("id invalide");
            bool witnessed = false;
            if (!ParseWitnessed(args[2], witnessed))
                return Fail("Usage: rpf crime report <id> <0|1>");
            const auto result = faction::ReportCrime(player, args[1], witnessed);
            if (!result.ok) return Fail(result.message);
            return Ok(result.message);
        }

        BridgeResult HandleLocation(PlayerId player, security::Level level,
                                    const std::vector<std::string>& args)
        {
            if (!CheckKeyFor(player, level, "rpf.location.canenter"))
                return Fail("permission refusee");
            if (args.size() < 2 || Lower(args[0]) != "canenter")
                return Fail("Usage: rpf location canenter <id>");
            if (!ValidWorldId(args[1]))
                return Fail("id invalide");
            const auto access = faction::CanEnter(player, args[1]);
            std::string msg = access.allowed ? "allowed" : "denied";
            if (!access.standingId.empty())
                msg += " standing=" + access.standingId;
            if (!access.allowed)
                return Fail(msg);
            return Ok(msg);
        }

        BridgeResult HandleNpc(PlayerId player, security::Level level,
                               const std::vector<std::string>& args)
        {
            if (!CheckKeyFor(player, level, "rpf.npc.hostile"))
                return Fail("permission refusee");
            if (args.size() < 2 || Lower(args[0]) != "hostile")
                return Fail("Usage: rpf npc hostile <faction>");
            if (!ValidWorldId(args[1]))
                return Fail("id invalide");
            if (faction::IsHostileTo(player, args[1]))
                return Ok("hostile");
            return Ok("non");
        }
    }

    std::vector<std::string> Tokenize(std::string_view input)
    {
        std::vector<std::string> tokens;
        std::string current;
        bool inQuotes = false;
        bool escape = false;

        for (char ch : input)
        {
            if (escape)
            {
                current.push_back(ch);
                escape = false;
                continue;
            }
            if (inQuotes && ch == '\\')
            {
                escape = true;
                continue;
            }
            if (ch == '"')
            {
                inQuotes = !inQuotes;
                continue;
            }
            const bool isSpace = ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
            if (!inQuotes && isSpace)
            {
                if (!current.empty())
                {
                    tokens.push_back(std::move(current));
                    current.clear();
                }
                continue;
            }
            current.push_back(ch);
        }
        if (!current.empty() || inQuotes)
        {
            if (!current.empty())
            {
                tokens.push_back(std::move(current));
            }
        }
        return tokens;
    }

    void Initialize()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_ready)
        {
            return;
        }
        security::Permissions::Initialize();
        security::Permissions::Register("rpf.ping", security::Level::PLAYER);
        security::Permissions::Register("rpf.auth", security::Level::PLAYER);
        security::Permissions::Register("rpf.race.list", security::Level::PLAYER);
        security::Permissions::Register("rpf.race.select", security::Level::PLAYER);
        security::Permissions::Register("rpf.job.list", security::Level::PLAYER);
        security::Permissions::Register("rpf.job.select", security::Level::PLAYER);
        security::Permissions::Register("rpf.player.status", security::Level::PLAYER);
        security::Permissions::Register("rpf.mod.get", security::Level::MODERATOR);
        security::Permissions::Register("rpf.crime.report", security::Level::PLAYER);
        security::Permissions::Register("rpf.location.canenter", security::Level::PLAYER);
        security::Permissions::Register("rpf.npc.hostile", security::Level::PLAYER);
        security::RateLimiter::Register("rpf.auth", {5, 60});
        g_ready = true;
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_sessionAdmins.clear();
        g_ready = false;
    }

    void ResetForTests()
    {
        Shutdown();
        Initialize();
    }

    bool IsSessionAdmin(PlayerId player)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_sessionAdmins.find(player) != g_sessionAdmins.end();
    }

    void ClearSessionAuth(PlayerId player)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_sessionAdmins.erase(player);
    }

    BridgeResult Execute(PlayerId player, std::string_view line)
    {
        return Execute(player, Tokenize(line));
    }

    BridgeResult Execute(PlayerId player, const std::vector<std::string>& tokens)
    {
        if (core::PluginContext::GetState() == core::PluginContext::State::Failed)
        {
            return Fail("plugin indisponible");
        }

        Initialize();

        std::vector<std::string> args = tokens;
        if (!args.empty() && Lower(args.front()) == kConsoleCommand)
        {
            args.erase(args.begin());
        }
        if (args.empty())
        {
            return Fail(Usage());
        }

        const std::string module = Lower(args[0]);
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        const auto level = EffectiveLevel(player);

        if (module == "ping")
        {
            return HandlePing(player, level);
        }
        if (module == "auth")
        {
            return HandleAuth(player, level, rest);
        }
        if (module == "race")
        {
            return HandleRace(player, level, rest);
        }
        if (module == "job" || module == "metier" || module == "profession")
        {
            return HandleJob(player, level, rest);
        }
        if (module == "player" || module == "joueur")
        {
            return HandlePlayer(player, level, rest);
        }
        if (module == "mod")
        {
            return HandleMod(player, level, rest);
        }
        if (module == "crime")
        {
            return HandleCrime(player, level, rest);
        }
        if (module == "location" || module == "lieu")
        {
            return HandleLocation(player, level, rest);
        }
        if (module == "npc" || module == "garde")
        {
            return HandleNpc(player, level, rest);
        }

        return Fail("module inconnu: " + module + " | " + Usage());
    }
}

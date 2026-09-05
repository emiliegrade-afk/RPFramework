// ============================================================================
// RPFramework - Quest / Commands - implementation
// ============================================================================
#include "Quest/Commands.h"

#include "Data/PlayerStore.h"
#include "Character/Registry.h"
#include "Character/Select.h"
#include "Economy/Registry.h"
#include "Economy/Wallet.h"
#include "Faction/Join.h"
#include "Faction/Registry.h"
#include "Faction/Reputation.h"
#include "Quest/Engine.h"
#include "Quest/Registry.h"
#include "Core/PluginContext.h"
#include "Core/Version.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"

#include <sstream>

namespace rpframework::quest
{
    namespace
    {
        CommandResult FromMessage(bool success, std::string message)
        {
            return {true, success, std::move(message)};
        }

        bool CanView(PlayerId player, std::string_view permission, std::string_view action)
        {
            using namespace security;
            if (Permissions::Check(Level::PLAYER, permission)) return true;
            AuditLog::LogDenied(action, player, "permission");
            return false;
        }

        CommandResult CharacterCommand(PlayerId player, const std::string& module,
                                       const std::vector<std::string>& args)
        {
            if (args.empty() || args[0] == "list")
            {
                if (!CanView(player, "character.view.self", "character.view"))
                    return FromMessage(false, "permission refusee");
                std::string message = module + ": ";
                bool first = true;
                if (module == "race")
                    for (const auto& item : character::Registry::ListRaces())
                    {
                        if (!first) message += ", ";
                        message += item.id; first = false;
                    }
                else if (module == "profession")
                    for (const auto& item : character::Registry::ListProfessions())
                    {
                        if (!first) message += ", ";
                        message += item.id; first = false;
                    }
                else
                    for (const auto& item : character::Registry::ListClasses())
                    {
                        if (!first) message += ", ";
                        message += item.id; first = false;
                    }
                return FromMessage(true, message);
            }
            if (args[0] != "select" || args.size() < 2)
                return FromMessage(false, "Usage: /" + module + " list|select <id>");

            character::SelectResult result;
            if (module == "race") result = character::SelectRace(player, args[1]);
            else if (module == "profession") result = character::SelectProfession(player, args[1]);
            else result = character::SelectClass(player, args[1]);
            return FromMessage(result.status == character::SelectResult::Status::Success,
                               result.message);
        }

        CommandResult FactionCommand(PlayerId player, const std::vector<std::string>& args)
        {
            if (args.empty() || args[0] == "list")
            {
                if (!CanView(player, "faction.view", "faction.view"))
                    return FromMessage(false, "permission refusee");
                std::string message = "factions: ";
                bool first = true;
                for (const auto& item : faction::Registry::ListFactionIds())
                {
                    if (!first) message += ", ";
                    message += item; first = false;
                }
                return FromMessage(true, message);
            }
            if (args[0] == "join" && args.size() >= 2)
            {
                const auto result = faction::Join(player, args[1]);
                return FromMessage(result.status == faction::JoinStatus::Success, result.message);
            }
            if (args[0] == "leave")
            {
                const auto result = faction::Leave(player);
                return FromMessage(result.status == faction::JoinStatus::Success, result.message);
            }
            if (args[0] == "rep" && args.size() >= 2)
            {
                if (!CanView(player, "reputation.view", "reputation.view"))
                    return FromMessage(false, "permission refusee");
                return FromMessage(true, std::to_string(faction::GetReputation(player, args[1])));
            }
            if (args[0] == "rank" && args.size() >= 2)
            {
                if (!CanView(player, "reputation.view", "reputation.view"))
                    return FromMessage(false, "permission refusee");
                const auto rank = faction::GetCurrentRank(player, args[1]);
                return FromMessage(rank.has_value(), rank ? rank->rankName : "rang indisponible");
            }
            return FromMessage(false, "Usage: /faction list|join <id>|leave|rep <id>|rank <id>");
        }

        CommandResult EconomyCommand(PlayerId player, const std::vector<std::string>& args)
        {
            if (args.empty() || args[0] == "list")
            {
                if (!CanView(player, "economy.view", "economy.view"))
                    return FromMessage(false, "permission refusee");
                std::string message = "monnaies: ";
                bool first = true;
                for (const auto& item : economy::Registry::ListCurrencyIds())
                {
                    if (!first) message += ", ";
                    message += item; first = false;
                }
                return FromMessage(true, message);
            }
            if (args[0] == "balance" && args.size() >= 2)
            {
                if (!CanView(player, "economy.view", "economy.view"))
                    return FromMessage(false, "permission refusee");
                return FromMessage(true, std::to_string(economy::GetBalance(player, args[1])));
            }
            if (args[0] == "transfer" && args.size() >= 4)
            {
                try
                {
                    const auto target = static_cast<PlayerId>(std::stoull(args[1]));
                    const auto amount = std::stoll(args[3]);
                    const auto result = economy::Transfer(player, target, args[2], amount, "chat");
                    return FromMessage(result.status == economy::TxStatus::Success, result.message);
                }
                catch (...) { return FromMessage(false, "cible ou montant invalide"); }
            }
            return FromMessage(false, "Usage: /economy list|balance <currency>|transfer <player> <currency> <amount>");
        }

    }

    std::vector<std::string> TokenizeCommand(std::string_view input)
    {
        std::istringstream stream{std::string(input)};
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token) tokens.push_back(std::move(token));
        return tokens;
    }

    CommandResult HandleCommand(PlayerId player, const std::vector<std::string>& args,
                                security::Level level)
    {
        if (args.empty()) return {true, false, "Usage: /quetes liste|demarrer|terminer|abandonner"};

        const auto& rawCommand = args[0];
        const std::string command = rawCommand == "metier" ? "profession"
            : rawCommand == "classe" ? "class"
            : rawCommand == "quetes" ? "quest" : rawCommand;
        if (command == "race" || command == "profession" || command == "class")
            return CharacterCommand(player, command, {args.begin() + 1, args.end()});
        if (command == "reputation")
        {
            // /reputation [<faction>]                → réputation numérique
            // /reputation rank <faction>             → rang actuel
            // /reputation rep <faction>              → alias explicite
            if (args.size() >= 3 && args[1] == "rank")
                return FactionCommand(player, {"rank", args[2]});
            if (args.size() >= 3 && args[1] == "rep")
                return FactionCommand(player, {"rep", args[2]});
            if (args.size() >= 2)
                return FactionCommand(player, {"rep", args[1]});
            return FromMessage(false,
                "Usage: /reputation <faction_id> | /reputation rep <faction_id> | /reputation rank <faction_id>");
        }
        if (command == "faction")
            return FactionCommand(player, {args.begin() + 1, args.end()});
        if (command == "economy")
            return EconomyCommand(player, {args.begin() + 1, args.end()});
        if (command == "framework")
        {
            if (args.size() == 2 && args[1] == "version")
                return FromMessage(true, std::string(core::kFrameworkName) + " " + core::GetVersionString());
            if (args.size() == 2 && args[1] == "reload")
            {
                if (level < security::Level::OWNER)
                    return FromMessage(false, "permission refusee");
                const bool reloaded = core::PluginContext::ReloadConfig();
                return FromMessage(reloaded,
                    reloaded ? "configuration rechargee" : "rechargement impossible");
            }
            return FromMessage(false, "Usage: /framework version");
        }
        if (command == "available" || command == "disponibles")
        {
            std::string message = "quetes disponibles: ";
            bool first = true;
            for (const auto& id : ListAvailableQuests(player))
            {
                if (!first) message += ", ";
                message += id;
                first = false;
            }
            return {true, true, message};
        }
        if (command == "list" || command == "liste")
        {
            std::string message = "Quetes: ";
            bool first = true;
            for (const auto& quest : Registry::ListQuests())
            {
                if (!first) message += ", ";
                message += quest.id;
                first = false;
            }
            return {true, true, message};
        }

        if (command == "status" || command == "etat")
        {
            if (args.size() < 2)
                return {true, false, "Usage: /quetes etat <quest_id>"};
            return {true, true, DescribeProgress(player, args[1])};
        }

        if (args.size() < 2)
            return {true, false, "Usage: /quetes liste|demarrer|terminer|abandonner <quest_id>"};

        const auto& questId = args[1];
        if (command == "start" || command == "demarrer")
        {
            const auto result = Start(player, questId);
            return {true, result.success(), result.message};
        }
        if (command == "complete" || command == "terminer")
        {
            const auto result = Complete(player, questId);
            return {true, result.success(), result.message};
        }
        if (command == "abandon" || command == "abandonner")
        {
            const auto result = Abandon(player, questId);
            return {true, result.success(), result.message};
        }
        return {true, false, "Commande quete inconnue"};
    }
}

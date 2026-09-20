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
#include "Core/Config.h"
#include "Core/PluginContext.h"
#include "Core/Version.h"
#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"
#include "Security/Validator.h"

#include "json.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string_view>

namespace rpframework::quest
{
    namespace
    {
        CommandResult FromMessage(bool success, std::string message)
        {
            return {true, success, std::move(message)};
        }

        std::string ClipChat(std::string text);

        bool CanView(PlayerId player, std::string_view permission, std::string_view action)
        {
            using namespace security;
            if (Permissions::CheckFor(player, permission)) return true;
            AuditLog::LogDenied(action, player, "permission");
            return false;
        }

        bool ValidToken(const std::string& id)
        {
            using namespace security;
            const auto result = Validator::All({
                Validator::NotEmpty(id, "id"),
                Validator::MaxLength(id, 64, "id"),
            });
            if (!result.valid) return false;
            for (unsigned char c : id)
            {
                if (!std::isalnum(c) && c != '_' && c != '-') return false;
            }
            return true;
        }

        std::string FormatMod(const character::StatModifier& mod)
        {
            std::string line = mod.target;
            if (mod.op == character::StatModifier::Op::Multiply) line += " x";
            else if (mod.op == character::StatModifier::Op::Set) line += "=";
            else line += " ";
            if (mod.op != character::StatModifier::Op::Set && mod.value > 0
                && mod.op == character::StatModifier::Op::Add)
                line += "+";
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2g", static_cast<double>(mod.value));
            line += buf;
            return line;
        }

        std::string FormatTraitList(const std::vector<character::StatModifier>& bonuses,
                                    const std::vector<character::StatModifier>& maluses)
        {
            std::string out;
            for (const auto& mod : bonuses)
            {
                if (!out.empty()) out += ", ";
                out += "+" + FormatMod(mod);
            }
            for (const auto& mod : maluses)
            {
                if (!out.empty()) out += ", ";
                out += "-" + FormatMod(mod);
            }
            return out.empty() ? "aucun trait" : out;
        }

        std::string DescribeContent(const std::string& module, const std::string& id)
        {
            if (module == "race")
            {
                auto item = character::Registry::GetRace(id);
                if (!item) return "race inconnue";
                return item->name + " | " + FormatTraitList(item->bonuses, item->maluses);
            }
            if (module == "profession")
            {
                auto item = character::Registry::GetProfession(id);
                if (!item) return "metier inconnu";
                std::string line = item->name + " | "
                    + FormatTraitList(item->bonuses, item->maluses);
                if (!item->engrams.empty())
                    line += " | engrams=" + std::to_string(item->engrams.size());
                return line;
            }
            auto item = character::Registry::GetClass(id);
            if (!item) return "classe inconnue";
            return item->name + " | " + FormatTraitList(item->bonuses, item->maluses);
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
                auto append = [&](const std::string& id, const std::string& name) {
                    if (!first) message += ", ";
                    message += id;
                    if (!name.empty() && name != id) message += "(" + name + ")";
                    first = false;
                };
                if (module == "race")
                    for (const auto& item : character::Registry::ListRaces())
                        append(item.id, item.name);
                else if (module == "profession")
                    for (const auto& item : character::Registry::ListProfessions())
                        append(item.id, item.name);
                else
                    for (const auto& item : character::Registry::ListClasses())
                        append(item.id, item.name);
                return FromMessage(true, ClipChat(message));
            }
            if (args[0] == "info" && args.size() >= 2)
            {
                if (!CanView(player, "character.view.self", "character.view"))
                    return FromMessage(false, "permission refusee");
                if (!ValidToken(args[1])) return FromMessage(false, "id invalide");
                return FromMessage(true, ClipChat(DescribeContent(module, args[1])));
            }
            if (args[0] != "select" || args.size() < 2)
                return FromMessage(false, "Usage: /" + module + " list|info <id>|select <id>");
            if (!ValidToken(args[1]))
                return FromMessage(false, "id invalide");

            character::SelectResult result;
            if (module == "race") result = character::SelectRace(player, args[1]);
            else if (module == "profession") result = character::SelectProfession(player, args[1]);
            else result = character::SelectClass(player, args[1]);
            std::string message = result.message;
            if (result.status == character::SelectResult::Status::Success)
                message += " | " + DescribeContent(module, args[1]);
            return FromMessage(result.status == character::SelectResult::Status::Success,
                               ClipChat(message));
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
                if (!ValidToken(args[1]))
                    return FromMessage(false, "id invalide");
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
                if (!ValidToken(args[1]))
                    return FromMessage(false, "id invalide");
                return FromMessage(true, std::to_string(faction::GetReputation(player, args[1])));
            }
            if (args[0] == "rank" && args.size() >= 2)
            {
                if (!CanView(player, "reputation.view", "reputation.view"))
                    return FromMessage(false, "permission refusee");
                if (!ValidToken(args[1]))
                    return FromMessage(false, "id invalide");
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
                if (!ValidToken(args[1]))
                    return FromMessage(false, "id invalide");
                return FromMessage(true, std::to_string(economy::GetBalance(player, args[1])));
            }
            if (args[0] == "transfer" && args.size() >= 4)
            {
                if (!ValidToken(args[1]) || !ValidToken(args[2]))
                    return FromMessage(false, "id invalide");
                try
                {
                    const auto& token = args[1];
                    const bool numeric = !token.empty()
                        && token.find_first_not_of("0123456789") == std::string::npos;
                    const auto target = (!numeric || token.size() >= 16)
                        ? security::MakePlayerId(token)
                        : static_cast<PlayerId>(std::stoull(token));
                    const auto amount = std::stoll(args[3]);
                    const auto result = economy::Transfer(player, target, args[2], amount, "chat");
                    return FromMessage(result.status == economy::TxStatus::Success, result.message);
                }
                catch (...) { return FromMessage(false, "cible ou montant invalide"); }
            }
            return FromMessage(false, "Usage: /economy list|balance <currency>|transfer <player> <currency> <amount>");
        }

        bool CanModerate(PlayerId player, security::Level level)
        {
            using namespace security;
            if (level >= Level::MODERATOR) return true;
            if (Permissions::CheckFor(player, "framework.config.edit")) return true;
            AuditLog::LogDenied("framework.config.edit", player, "permission");
            return false;
        }

        std::string JoinArgs(const std::vector<std::string>& args, std::size_t from)
        {
            std::string out;
            for (std::size_t i = from; i < args.size(); ++i)
            {
                if (!out.empty()) out += ' ';
                out += args[i];
            }
            return out;
        }

        nlohmann::json ParseConfigValue(const std::string& raw)
        {
            if (raw == "true") return true;
            if (raw == "false") return false;
            if (raw == "null") return nullptr;
            try { return nlohmann::json::parse(raw); }
            catch (...) {}
            try
            {
                std::size_t idx = 0;
                const auto n = std::stoll(raw, &idx);
                if (idx == raw.size()) return n;
            }
            catch (...) {}
            try
            {
                std::size_t idx = 0;
                const auto n = std::stod(raw, &idx);
                if (idx == raw.size()) return n;
            }
            catch (...) {}
            return raw;
        }

        bool ValidPath(const std::string& path)
        {
            if (path.empty() || path.size() > 128) return false;
            for (unsigned char c : path)
            {
                if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') return false;
            }
            return true;
        }

        // /mod get|set : gameplay live-edit seulement. security.* et data.*
        // restent hors de portée même pour un OWNER (fichier + reload).
        bool PathAllowedForLiveEdit(std::string_view path)
        {
            if (path.empty()) return false;
            if (path == "security" || path.starts_with("security.")) return false;
            if (path == "data" || path.starts_with("data.")) return false;

            auto allowedRoot = [](std::string_view value, std::string_view root)
            {
                if (value == root) return true;
                return value.size() > root.size()
                    && value.starts_with(root)
                    && value[root.size()] == '.';
            };

            return allowedRoot(path, "character")
                || allowedRoot(path, "quests")
                || allowedRoot(path, "quest")
                || allowedRoot(path, "factions")
                || allowedRoot(path, "faction")
                || allowedRoot(path, "loadout")
                || allowedRoot(path, "merchants")
                || allowedRoot(path, "merchant")
                || allowedRoot(path, "crafting")
                || allowedRoot(path, "effects")
                || path == "debug"
                || path.starts_with("debug.")
                || path == "world.spawn_zones"
                || path.starts_with("world.spawn_zones.");
        }

        PlayerId ParsePlayerToken(const std::string& token)
        {
            const bool numeric = !token.empty()
                && token.find_first_not_of("0123456789") == std::string::npos;
            if (!numeric || token.size() >= 16)
                return security::MakePlayerId(token);
            try { return static_cast<PlayerId>(std::stoull(token)); }
            catch (...) { return security::MakePlayerId(token); }
        }

        std::string ClipChat(std::string text)
        {
            if (text.size() > 220) text = text.substr(0, 217) + "...";
            return text;
        }

        CommandResult PersistLive(PlayerId player, const std::string& action,
                                  const nlohmann::json& payload)
        {
            if (!core::PluginContext::SaveAndApply())
                return FromMessage(false, "sauvegarde ou application impossible");
            security::AuditLog::Log(action, player, payload);
            return FromMessage(true, "ok, config appliquee");
        }

        nlohmann::json GetOrObject(const std::string& path)
        {
            const auto node = core::Config::Get().Get(path);
            if (node && node->is_object()) return *node;
            return nlohmann::json::object();
        }

        const char* ContentRoot(const std::string& kind)
        {
            if (kind == "race") return "character.races";
            if (kind == "job" || kind == "profession" || kind == "metier")
                return "character.professions";
            if (kind == "faction") return "factions";
            if (kind == "quest" || kind == "quete") return "quests";
            return nullptr;
        }

        nlohmann::json DefaultContent(const std::string& kind, const std::string& id,
                                      const std::string& name)
        {
            const std::string display = name.empty() ? id : name;
            if (kind == "race")
            {
                return {
                    {"name", display},
                    {"description", ""},
                    {"bonuses", nlohmann::json::array()},
                    {"maluses", nlohmann::json::array()},
                    {"skills", nlohmann::json::array()},
                    {"starter_equipment", nlohmann::json::array()},
                };
            }
            if (kind == "job" || kind == "profession" || kind == "metier")
            {
                return {
                    {"name", display},
                    {"description", ""},
                    {"bonuses", nlohmann::json::array()},
                    {"maluses", nlohmann::json::array()},
                    {"engrams", nlohmann::json::array()},
                    {"starter_equipment", nlohmann::json::array()},
                };
            }
            if (kind == "faction")
            {
                return {
                    {"name", display},
                    {"description", ""},
                    {"ranks", nlohmann::json::array({
                        {{"id", "member"}, {"name", "Membre"}, {"min_reputation", 0}}
                    })},
                    {"starter_quests", nlohmann::json::array()},
                    {"journal", nlohmann::json::object()},
                };
            }
            return {
                {"name", display},
                {"description", ""},
                {"objectives", nlohmann::json::array()},
                {"rewards", nlohmann::json::array()},
                {"auto_complete", true},
            };
        }

        CommandResult ContentCommand(PlayerId player, const std::string& kind,
                                     const std::vector<std::string>& args)
        {
            const char* root = ContentRoot(kind);
            if (!root) return FromMessage(false, "section inconnue");

            const char* raceHelp =
                "Usage: /mod race add <id> [nom]|trait <id> bonus|malus <stat> add|multiply|set <val>|desc <id> <texte>|del <id>";
            const char* jobHelp =
                "Usage: /mod job add <id> [nom]|trait <id> bonus|malus <stat> add|multiply|set <val>|engram <id> add <bp>|engram <id> clear|kit <id> add <item> <qty> [bp]|del <id>";
            const char* factionHelp =
                "Usage: /mod faction add <id> [nom]|journal <id> <item> [bp]|quest <id> add <quete>|desc <id> <texte>|del <id>";
            const char* questHelp =
                "Usage: /mod quest add <id> [nom]|desc <id> <texte>|objective <id> <obj> <type> <entity> <n>|reward <id> <type> <rid> <n>|del <id>";

            const char* help = raceHelp;
            if (kind == "job" || kind == "profession" || kind == "metier") help = jobHelp;
            else if (kind == "faction") help = factionHelp;
            else if (kind == "quest" || kind == "quete") help = questHelp;

            if (args.empty() || args[0] == "help")
                return FromMessage(true, help);

            const auto& op = args[0];
            if (args.size() < 2 || !ValidToken(args[1]))
                return FromMessage(false, help);
            const auto& id = args[1];
            const std::string path = std::string(root) + "." + id;

            if (op == "add")
            {
                auto existing = core::Config::Get().Get(path);
                nlohmann::json node = (existing && existing->is_object())
                    ? *existing : DefaultContent(kind, id, JoinArgs(args, 2));
                if (!JoinArgs(args, 2).empty())
                    node["name"] = JoinArgs(args, 2);
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.content", {
                    {"kind", kind}, {"op", "add"}, {"id", id},
                });
            }
            if (op == "del" || op == "delete")
            {
                auto parent = GetOrObject(root);
                if (!parent.contains(id)) return FromMessage(false, "id inconnu");
                parent.erase(id);
                core::Config::Get().Set(root, parent);
                return PersistLive(player, "framework.config.content", {
                    {"kind", kind}, {"op", "del"}, {"id", id},
                });
            }
            if (op == "desc" || op == "description")
            {
                if (args.size() < 3) return FromMessage(false, help);
                auto node = GetOrObject(path);
                if (node.empty() && !core::Config::Get().Has(path))
                    return FromMessage(false, "id inconnu");
                node["description"] = JoinArgs(args, 2);
                if (!node.contains("name")) node["name"] = id;
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.content", {
                    {"kind", kind}, {"op", "desc"}, {"id", id},
                });
            }
            if (op == "trait" && (kind == "race" || kind == "job"
                || kind == "profession" || kind == "metier"))
            {
                if (args.size() < 6) return FromMessage(false, help);
                std::string bucket = args[2];
                if (bucket == "bonus") bucket = "bonuses";
                if (bucket == "malus") bucket = "maluses";
                if (bucket != "bonuses" && bucket != "maluses")
                    return FromMessage(false, "trait: bonus ou malus");
                if (!ValidToken(args[3])) return FromMessage(false, "stat invalide");
                std::string opName = args[4];
                for (char& c : opName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (opName != "add" && opName != "multiply" && opName != "set")
                    return FromMessage(false, "op: add|multiply|set");
                const auto value = ParseConfigValue(args[5]);
                if (!value.is_number()) return FromMessage(false, "valeur invalide");
                auto node = GetOrObject(path);
                if (node.empty() && !core::Config::Get().Has(path))
                    return FromMessage(false, "id inconnu");
                if (!node.contains("name")) node["name"] = id;
                if (!node[bucket].is_array()) node[bucket] = nlohmann::json::array();
                node[bucket].push_back({
                    {"target", args[3]}, {"op", opName}, {"value", value},
                });
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.trait", {
                    {"kind", kind}, {"id", id}, {"target", args[3]},
                });
            }
            if (op == "engram" && (kind == "job" || kind == "profession" || kind == "metier"))
            {
                if (args.size() < 3) return FromMessage(false, help);
                auto node = GetOrObject(path);
                if (node.empty() && !core::Config::Get().Has(path))
                    return FromMessage(false, "id inconnu");
                if (!node.contains("name")) node["name"] = id;
                if (args[2] == "clear")
                {
                    node["engrams"] = nlohmann::json::array();
                }
                else if (args[2] == "add" && args.size() >= 4)
                {
                    if (!node["engrams"].is_array()) node["engrams"] = nlohmann::json::array();
                    node["engrams"].push_back(JoinArgs(args, 3));
                }
                else return FromMessage(false, help);
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.engram", {
                    {"id", id}, {"op", args[2]},
                });
            }
            if (op == "kit" && (kind == "job" || kind == "profession" || kind == "metier"
                || kind == "race"))
            {
                if (args.size() < 5 || args[2] != "add") return FromMessage(false, help);
                if (!ValidToken(args[3])) return FromMessage(false, "id invalide");
                int qty = 1;
                try { qty = std::stoi(args[4]); } catch (...) { return FromMessage(false, "quantite invalide"); }
                if (qty < 1) qty = 1;
                auto node = GetOrObject(path);
                if (node.empty() && !core::Config::Get().Has(path))
                    return FromMessage(false, "id inconnu");
                if (!node.contains("name")) node["name"] = id;
                if (!node["starter_equipment"].is_array())
                    node["starter_equipment"] = nlohmann::json::array();
                nlohmann::json item = {{"id", args[3]}, {"quantity", qty}};
                if (args.size() >= 6) item["blueprint"] = JoinArgs(args, 5);
                node["starter_equipment"].push_back(std::move(item));
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.kit", {
                    {"kind", kind}, {"id", id}, {"item", args[3]},
                });
            }
            if (kind == "faction" && op == "journal")
            {
                if (args.size() < 3) return FromMessage(false, help);
                auto node = GetOrObject(path);
                if (node.empty() && !core::Config::Get().Has(path))
                    return FromMessage(false, "id inconnu");
                nlohmann::json journal = {{"id", args[2]}, {"quantity", 1}};
                if (args.size() >= 4) journal["blueprint"] = JoinArgs(args, 3);
                node["journal"] = journal;
                if (!node.contains("name")) node["name"] = id;
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.journal", {{"faction", id}});
            }
            if (kind == "faction" && op == "quest")
            {
                if (args.size() < 4 || args[2] != "add" || !ValidToken(args[3]))
                    return FromMessage(false, help);
                auto node = GetOrObject(path);
                if (node.empty() && !core::Config::Get().Has(path))
                    return FromMessage(false, "id inconnu");
                if (!node["starter_quests"].is_array())
                    node["starter_quests"] = nlohmann::json::array();
                node["starter_quests"].push_back(args[3]);
                if (!node.contains("name")) node["name"] = id;
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.faction_quest", {
                    {"faction", id}, {"quest", args[3]},
                });
            }
            if ((kind == "quest" || kind == "quete") && op == "objective")
            {
                if (args.size() < 6 || !ValidToken(args[2]) || !ValidToken(args[3])
                    || !ValidToken(args[4]))
                    return FromMessage(false, help);
                int target = 1;
                try { target = std::stoi(args[5]); } catch (...) { return FromMessage(false, "cible invalide"); }
                if (target < 1) target = 1;
                auto node = GetOrObject(path);
                if (node.empty() && !core::Config::Get().Has(path))
                    return FromMessage(false, "id inconnu");
                if (!node["objectives"].is_array()) node["objectives"] = nlohmann::json::array();
                node["objectives"].push_back({
                    {"id", args[2]}, {"type", args[3]},
                    {"entity", args[4]}, {"target", target}, {"required", true},
                });
                if (!node.contains("name")) node["name"] = id;
                if (!node.contains("auto_complete")) node["auto_complete"] = true;
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.quest_objective", {
                    {"quest", id}, {"objective", args[2]},
                });
            }
            if ((kind == "quest" || kind == "quete") && op == "reward")
            {
                if (args.size() < 5 || !ValidToken(args[2]) || !ValidToken(args[3]))
                    return FromMessage(false, help);
                int amount = 0;
                try { amount = std::stoi(args[4]); } catch (...) { return FromMessage(false, "montant invalide"); }
                auto node = GetOrObject(path);
                if (node.empty() && !core::Config::Get().Has(path))
                    return FromMessage(false, "id inconnu");
                if (!node["rewards"].is_array()) node["rewards"] = nlohmann::json::array();
                node["rewards"].push_back({
                    {"type", args[2]}, {"id", args[3]}, {"amount", amount},
                });
                if (!node.contains("name")) node["name"] = id;
                core::Config::Get().Set(path, node);
                return PersistLive(player, "framework.config.quest_reward", {
                    {"quest", id}, {"type", args[2]},
                });
            }
            return FromMessage(false, help);
        }

        CommandResult ModCommand(PlayerId player, const std::vector<std::string>& args,
                                 security::Level level)
        {
            const char* usage =
                "Usage: /mod help [race|job|faction|quest]|get|set|list|"
                "kit|race|job|faction|quest|player|grant|rep";

            if (args.empty() || args[0] == "help")
            {
                if (!CanModerate(player, level))
                    return FromMessage(false, "permission refusee");
                if (args.size() >= 2)
                    return ContentCommand(player, args[1], {"help"});
                return FromMessage(true, usage);
            }

            const auto& verb = args[0];
            if (!CanModerate(player, level))
                return FromMessage(false, "permission refusee");

            const bool mutating = verb == "set" || verb == "player" || verb == "grant"
                || verb == "rep" || verb == "spawn" || verb == "kit"
                || verb == "race" || verb == "job" || verb == "profession"
                || verb == "metier" || verb == "faction" || verb == "quest"
                || verb == "quete";
            if (mutating && !security::RateLimiter::Allow(player, "framework.config.edit"))
                return FromMessage(false, "rate limit atteint");

            if (verb == "race" || verb == "job" || verb == "profession" || verb == "metier"
                || verb == "faction" || verb == "quest" || verb == "quete")
            {
                return ContentCommand(player, verb, {args.begin() + 1, args.end()});
            }

            if (verb == "get" && args.size() >= 2)
            {
                if (!ValidPath(args[1])) return FromMessage(false, "chemin invalide");
                if (!PathAllowedForLiveEdit(args[1]))
                    return FromMessage(false, "chemin config refuse");
                const auto node = core::Config::Get().Get(args[1]);
                if (!node) return FromMessage(false, "chemin inconnu");
                return FromMessage(true, ClipChat(node->dump()));
            }

            if (verb == "set" && args.size() >= 3)
            {
                if (!ValidPath(args[1])) return FromMessage(false, "chemin invalide");
                if (!PathAllowedForLiveEdit(args[1]))
                    return FromMessage(false, "chemin config refuse");
                const auto value = ParseConfigValue(JoinArgs(args, 2));
                core::Config::Get().Set(args[1], value);
                return PersistLive(player, "framework.config.set", {
                    {"path", args[1]}, {"value", value},
                });
            }

            if (verb == "list" && args.size() >= 2)
            {
                const auto& section = args[1];
                std::string message = section + ": ";
                bool first = true;
                auto append = [&](const std::string& id) {
                    if (!first) message += ", ";
                    message += id; first = false;
                };
                if (section == "races")
                    for (const auto& item : character::Registry::ListRaces()) append(item.id);
                else if (section == "professions" || section == "metiers")
                    for (const auto& item : character::Registry::ListProfessions()) append(item.id);
                else if (section == "classes")
                    for (const auto& item : character::Registry::ListClasses()) append(item.id);
                else if (section == "factions")
                    for (const auto& item : faction::Registry::ListFactionIds()) append(item);
                else if (section == "quests" || section == "quetes")
                    for (const auto& item : quest::Registry::ListQuests()) append(item.id);
                else if (section == "currencies" || section == "monnaies")
                    for (const auto& item : economy::Registry::ListCurrencyIds()) append(item);
                else if (section == "kit")
                {
                    const auto kit = core::Config::Get().Get("loadout.common_kit");
                    return FromMessage(true, ClipChat(kit ? kit->dump() : "[]"));
                }
                else
                    return FromMessage(false, "Usage: /mod list races|professions|classes|factions|quests|currencies|kit");
                return FromMessage(true, ClipChat(message));
            }

            if (verb == "kit" && args.size() >= 2 && args[1] == "clear")
            {
                core::Config::Get().Set("loadout.common_kit", nlohmann::json::array());
                return PersistLive(player, "framework.config.kit", {{"op", "clear"}});
            }
            if (verb == "kit" && args.size() >= 4 && args[1] == "add")
            {
                if (!ValidToken(args[2])) return FromMessage(false, "id invalide");
                int qty = 1;
                try { qty = std::stoi(args[3]); } catch (...) { return FromMessage(false, "quantite invalide"); }
                if (qty < 1) qty = 1;
                auto kit = core::Config::Get().Get("loadout.common_kit").value_or(nlohmann::json::array());
                if (!kit.is_array()) kit = nlohmann::json::array();
                nlohmann::json item = {{"id", args[2]}, {"quantity", qty}};
                if (args.size() >= 5) item["blueprint"] = args[4];
                kit.push_back(std::move(item));
                core::Config::Get().Set("loadout.common_kit", kit);
                return PersistLive(player, "framework.config.kit", {{"op", "add"}, {"id", args[2]}});
            }

            if (verb == "spawn" && args.size() >= 5)
            {
                if (!ValidToken(args[1])) return FromMessage(false, "id invalide");
                try
                {
                    nlohmann::json zone;
                    zone["x"] = std::stod(args[2]);
                    zone["y"] = std::stod(args[3]);
                    zone["z"] = std::stod(args[4]);
                    if (args.size() >= 6) zone["yaw"] = std::stod(args[5]);
                    core::Config::Get().Set("world.spawn_zones." + args[1], zone);
                    return PersistLive(player, "framework.config.spawn", {
                        {"zone", args[1]}, {"coords", zone},
                    });
                }
                catch (...) { return FromMessage(false, "coordonnees invalides"); }
            }

            if (verb == "player" && args.size() >= 3)
            {
                std::string upper = args[2];
                for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (upper != "PLAYER" && upper != "MODERATOR" && upper != "GM"
                    && upper != "ADMIN" && upper != "OWNER")
                    return FromMessage(false, "niveau: PLAYER|MODERATOR|GM|ADMIN|OWNER");
                const auto newLevel = security::LevelFromString(upper);
                const auto target = ParsePlayerToken(args[1]);
                const auto current = security::Permissions::GetPlayerLevel(target);
                if (current >= level)
                    return FromMessage(false, "cible de niveau superieur ou egal");
                if (newLevel >= level)
                    return FromMessage(false, "niveau assigne trop eleve");
                core::Config::Get().Set("security.player_levels." + args[1], upper);
                return PersistLive(player, "framework.config.player", {
                    {"target", args[1]}, {"level", upper},
                });
            }

            if (verb == "grant" && args.size() >= 4)
            {
                if (!security::Permissions::Check(level, "economy.grant"))
                    return FromMessage(false, "permission refusee");
                if (!ValidToken(args[2])) return FromMessage(false, "id invalide");
                try
                {
                    const auto target = ParsePlayerToken(args[1]);
                    const auto amount = std::stoll(args[3]);
                    const auto result = economy::Grant(player, target, args[2], amount, "mod");
                    return FromMessage(result.status == economy::TxStatus::Success, result.message);
                }
                catch (...) { return FromMessage(false, "joueur ou montant invalide"); }
            }

            if (verb == "rep" && args.size() >= 4)
            {
                if (!security::Permissions::Check(level, "faction.modify_reputation"))
                    return FromMessage(false, "permission refusee");
                if (!ValidToken(args[2])) return FromMessage(false, "id invalide");
                try
                {
                    const auto target = ParsePlayerToken(args[1]);
                    const auto value = std::stoi(args[3]);
                    const auto after = faction::SetReputation(target, args[2], value, "mod");
                    return FromMessage(true, "reputation=" + std::to_string(after));
                }
                catch (...) { return FromMessage(false, "joueur ou valeur invalide"); }
            }

            return FromMessage(false, usage);
        }

        // Verbes du module quête. `args[0]` est le verbe (list/start/…),
        // jamais le nom de commande chat : le glue AsaApi préfixe
        // "quest"/"quetes" (Main.cpp registerCommand), et on le strippe
        // dans HandleCommand avant d'arriver ici.
        CommandResult QuestJournal(PlayerId player)
        {
            if (!CanView(player, "quest.list", "quest.journal"))
                return FromMessage(false, "permission refusee");
            const auto active = ListActiveQuests(player);
            if (active.empty())
                return FromMessage(true, "journal vide. /quetes disponibles");
            std::string message = "journal: ";
            for (std::size_t i = 0; i < active.size(); ++i)
            {
                if (i > 0) message += " | ";
                message += active[i];
            }
            return FromMessage(true, ClipChat(message));
        }

        CommandResult QuestCommand(PlayerId player, const std::vector<std::string>& args)
        {
            if (args.empty() || args[0] == "journal" || args[0] == "actif"
                || args[0] == "actifs")
                return QuestJournal(player);

            const auto& verb = args[0];
            if (verb == "available" || verb == "disponibles")
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
            if (verb == "list" || verb == "liste")
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
            if (verb == "status" || verb == "etat")
            {
                if (args.size() < 2)
                    return {true, false, "Usage: /quetes etat <quest_id>"};
                if (!ValidToken(args[1]))
                    return {true, false, "id invalide"};
                return {true, true, DescribeProgress(player, args[1])};
            }
            if (args.size() < 2)
                return {true, false, "Usage: /quetes liste|demarrer|terminer|abandonner <quest_id>"};

            const auto& questId = args[1];
            if (!ValidToken(questId))
                return {true, false, "id invalide"};
            if (verb == "start" || verb == "demarrer")
            {
                const auto result = Start(player, questId);
                return {true, result.success(), result.message};
            }
            if (verb == "complete" || verb == "terminer")
            {
                const auto result = Complete(player, questId);
                return {true, result.success(), result.message};
            }
            if (verb == "abandon" || verb == "abandonner")
            {
                const auto result = Abandon(player, questId);
                return {true, result.success(), result.message};
            }
            return {true, false, "Commande quete inconnue"};
        }

        bool IsQuestVerb(std::string_view command)
        {
            return command == "list" || command == "liste"
                || command == "available" || command == "disponibles"
                || command == "status" || command == "etat"
                || command == "start" || command == "demarrer"
                || command == "complete" || command == "terminer"
                || command == "abandon" || command == "abandonner"
                || command == "journal" || command == "actif" || command == "actifs";
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
        if (core::PluginContext::GetState() == core::PluginContext::State::Failed)
            return FromMessage(false, "plugin indisponible");
        if (args.empty()) return QuestJournal(player);

        const auto& rawCommand = args[0];
        const std::string command = rawCommand == "metier" ? "profession"
            : rawCommand == "classe" ? "class"
            : rawCommand == "quetes" ? "quest"
            : rawCommand == "journal" ? "journal" : rawCommand;
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
                if (level < security::Level::OWNER
                    && !security::Permissions::CheckFor(player, "framework.reload"))
                    return FromMessage(false, "permission refusee");
                const bool reloaded = core::PluginContext::ReloadConfig();
                return FromMessage(reloaded,
                    reloaded ? "configuration rechargee" : "rechargement impossible");
            }
            if (args.size() == 1)
                return FromMessage(true, "Usage: /framework version|reload  |  /mod help");
            return ModCommand(player, {args.begin() + 1, args.end()}, level);
        }
        if (command == "mod" || command == "config")
            return ModCommand(player, {args.begin() + 1, args.end()}, level);
        // Chat : /quest list → {"quest","list"} (préfixe collé par AsaApi).
        // Tests / console : {"list"} ou {"start", id} sans préfixe.
        if (command == "journal")
            return QuestCommand(player, {"journal"});
        if (command == "quest")
            return QuestCommand(player, {args.begin() + 1, args.end()});
        if (IsQuestVerb(command))
            return QuestCommand(player, args);
        return {true, false, "Commande quete inconnue"};
    }
}

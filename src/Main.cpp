// ============================================================================
// RPFramework - Point d'entrée du plugin
//
// Glue AsaApi : Plugin_Init / Plugin_Unload + hooks minimaux.
// Toute la logique métier vit dans src/Core/, src/Security/, src/Data/,
// src/Character/, src/Loadout/, src/Faction/, src/Economy/, src/Quest/,
// src/Api/, src/Mod/. Le glue monde ASA est dans src/Asa/ et Loadout/AsaDeliver.cpp.
// Canal mod : commande console "rpf" (AddConsoleCommand, GDD §48).
//
// Hooks en place :
//   - AShooterGameMode_BeginPlay                        → log + audit
//   - AShooterPlayerController_ServerSendChatMessage_Impl
//                                                     → pipeline Security (chat)
//   - AShooterGameMode_HandleNewPlayer_Implementation  → charge/crée PlayerData
//   - AShooterGameMode_Logout                           → sauvegarde PlayerData
//   - APrimalDinoCharacter_Die                          → quest ReportKill
//   - APrimalDinoCharacter_TameDino                     → quest ReportTame
//   - AShooterPlayerController_ServerCraftItem_Implementation → ReportCraft + pipeline XP
//   - AShooterPlayerController_HarvestedElement         → ReportCollection
// ============================================================================
#include "API/ARK/Ark.h"

#include "Asa/Identity.h"
#include "Asa/PawnEffects.h"
#include "Asa/WorldHooks.h"

#include "Core/PluginContext.h"
#include "Core/Logger.h"
#include "Core/Version.h"

#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"

#include "Loadout/AsaDeliver.h"
#include "Loadout/Distribute.h"

#include "Crafting/Pipeline.h"

#include "Quest/Commands.h"
#include "Economy/Merchant.h"
#include "Mod/Bridge.h"

#include "Security/AuditLog.h"
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"
#include "Security/Validator.h"

#include <fmt/format.h>

#include <Windows.h>

#include <functional>
#include <string>

namespace
{
    // Pipeline Security partagé : permission → rate limit → audit.
    bool RunSecurityCheck(rpframework::security::PlayerId playerId,
                          rpframework::security::Level   playerLevel,
                          std::string_view              actionKey,
                          const nlohmann::json&         payload = {})
    {
        using namespace rpframework::security;

        if (!Permissions::Check(playerLevel, actionKey))
        {
            AuditLog::LogDenied(actionKey, playerId, "permission");
            return false;
        }
        if (!RateLimiter::Allow(playerId, actionKey))
        {
            AuditLog::LogDenied(actionKey, playerId, "rate_limit", {
                {"retry_in_sec", RateLimiter::SecondsUntilNext(playerId, actionKey)},
            });
            return false;
        }
        AuditLog::Log(actionKey, playerId, payload);
        return true;
    }

    void OnServerReady()
    {
        rpframework::core::LogInfo("serveur prêt - {} v{} opérationnel.",
            std::string(rpframework::core::kFrameworkName),
            rpframework::core::GetVersionString());

        rpframework::security::AuditLog::Log("server.ready", 0, {
            {"version", rpframework::core::GetVersionString()},
        });
    }
}

// ---------------------------------------------------------------------------
// Hook 1 : AShooterGameMode::BeginPlay
// ---------------------------------------------------------------------------
DECLARE_HOOK(AShooterGameMode_BeginPlay, void, AShooterGameMode*);

void Hook_AShooterGameMode_BeginPlay(AShooterGameMode* _this)
{
    AShooterGameMode_BeginPlay_original(_this);

    if (rpframework::core::PluginContext::IsInitialized())
    {
        OnServerReady();
    }
}

// ---------------------------------------------------------------------------
// Hook 2 : AShooterPlayerController::ServerSendChatMessage_Impl
// ---------------------------------------------------------------------------
DECLARE_HOOK(AShooterPlayerController_ServerSendChatMessage_Impl,
             void, AShooterPlayerController*, FString*, int, int);

void Hook_AShooterPlayerController_ServerSendChatMessage_Impl(
    AShooterPlayerController* pc, FString* msg, int mode, int toTeam)
{
    if (pc == nullptr || msg == nullptr)
    {
        AShooterPlayerController_ServerSendChatMessage_Impl_original(pc, msg, mode, toTeam);
        return;
    }

    const auto pid = rpframework::asa::ExtractPlayerId(pc);
    if (pid == 0)
    {
        AShooterPlayerController_ServerSendChatMessage_Impl_original(pc, msg, mode, toTeam);
        return;
    }

    const int msgLen = msg->Len();
    if (msgLen > 256)
    {
        rpframework::security::AuditLog::LogDenied("chat.send", pid, "too_long", {
            {"len", msgLen}, {"max", 256},
        });
        return;
    }
    if (msg->IsEmpty())
    {
        AShooterPlayerController_ServerSendChatMessage_Impl_original(pc, msg, mode, toTeam);
        return;
    }

    if (!RunSecurityCheck(pid, rpframework::security::Permissions::GetPlayerLevel(pid),
        "chat.send", {
        {"len", msgLen}, {"mode", mode}, {"team", toTeam},
    }))
    {
        return;
    }

    AShooterPlayerController_ServerSendChatMessage_Impl_original(pc, msg, mode, toTeam);
}

// ---------------------------------------------------------------------------
// Hook 3 : AShooterGameMode::HandleNewPlayer_Implementation (joueur rejoint)
//
// On charge/crée la fiche persistante ici. L'audit trace l'arrivée.
// ---------------------------------------------------------------------------
DECLARE_HOOK(AShooterGameMode_HandleNewPlayer_Implementation,
             bool, AShooterGameMode*, AShooterPlayerController*,
             UPrimalPlayerData*, AShooterCharacter*, bool);

bool Hook_AShooterGameMode_HandleNewPlayer_Implementation(
    AShooterGameMode* gm, AShooterPlayerController* pc,
    UPrimalPlayerData* playerData, AShooterCharacter* character, bool isFromLogin)
{
    // On transmet les paramètres d'origine tels quels : les remplacer par
    // nullptr casserait la création/spawn vanilla du joueur.
    const bool result = AShooterGameMode_HandleNewPlayer_Implementation_original(
        gm, pc, playerData, character, isFromLogin);

    if (pc == nullptr) return result;

    const auto pid = rpframework::asa::ExtractPlayerId(pc);
    if (pid == 0) return result;

    std::string name;
    try
    {
        name = rpframework::asa::FStringToUtf8(
            AsaApi::IApiUtils::GetCharacterName(pc));
    }
    catch (...)
    {
    }

    rpframework::data::PlayerData data;
    bool isNew = false;
    {
        rpframework::data::PlayerStore::ExclusiveLock storeLock;

        auto load = rpframework::data::PlayerStore::LoadDetailed(pid);
        if (load.status == rpframework::data::PlayerLoadStatus::Corrupt
            || load.status == rpframework::data::PlayerLoadStatus::Unavailable)
        {
            rpframework::security::AuditLog::Log("player.join_data_unavailable", pid, {
                {"status", static_cast<int>(load.status)},
            }, rpframework::security::audit_severity::kError);
            return result;
        }

        isNew = (load.status == rpframework::data::PlayerLoadStatus::Missing);
        data = load.HasData() ? std::move(*load.data)
                              : rpframework::data::PlayerData{};
        bool nameChanged = false;
        if (isNew)
        {
            data.id = pid;
            data.name = name;
        }
        else if (!name.empty() && data.name != name)
        {
            data.name = name;
            nameChanged = true;
        }

        if (isNew || nameChanged)
        {
            rpframework::data::PlayerStore::Save(data);
        }

        rpframework::security::AuditLog::Log("player.join", pid, {
            {"name",       name},
            {"is_new",     isNew},
            {"level",      data.level},
            {"xp",         data.xp},
            {"race",       data.race},
            {"profession", data.profession},
        });
    }

    rpframework::security::Permissions::TryBootstrapOwner(pid);
    rpframework::loadout::Distributor::GiveStarterKit(pid);
    rpframework::loadout::TryGivePendingQuestItems(pid);
    rpframework::asa::ApplyWorldEffects(pid, rpframework::asa::WorldApply::Stats);
    if (!data.profession.empty())
    {
        rpframework::crafting::GrantAccessibleEngrams(pid);
    }
    if (isNew || data.race.empty())
    {
        rpframework::asa::Tell(pid,
            "Bienvenue. Tape /race list puis /race select <id>, ensuite /metier list.",
            false);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Hook 4 : AShooterGameMode::Logout (joueur part)
//
// On force la sauvegarde finale ici, même si on a un auto-save ailleurs.
// ---------------------------------------------------------------------------
DECLARE_HOOK(AShooterGameMode_Logout, void, AShooterGameMode*, AController*);

void Hook_AShooterGameMode_Logout(AShooterGameMode* gm, AController* controller)
{
    AShooterGameMode_Logout_original(gm, controller);

    if (controller == nullptr) return;

    auto* pc = static_cast<AShooterPlayerController*>(controller);
    const auto pid = rpframework::asa::ExtractPlayerId(pc);
    if (pid == 0) return;

    rpframework::security::RateLimiter::ResetPlayer(pid);
    rpframework::security::RateLimiter::Cleanup();

    auto load = rpframework::data::PlayerStore::LoadDetailed(pid);
    if (load.HasData())
    {
        rpframework::data::PlayerStore::Flush(pid);
        rpframework::security::AuditLog::Log("player.leave", pid, {
            {"level",      load.data->level},
            {"xp",         load.data->xp},
            {"race",       load.data->race},
            {"profession", load.data->profession},
        });
    }
}

// ---------------------------------------------------------------------------
// Plugin entry points
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) void Plugin_Init()
{
    if (!rpframework::core::PluginContext::Initialize())
    {
        rpframework::core::LogError("PluginContext::Initialize() a échoué - le plugin tourne en mode dégradé.");
    }
    rpframework::economy::Merchant::Load();

    AsaApi::GetHooks().SetHook("AShooterGameMode.BeginPlay()",
        Hook_AShooterGameMode_BeginPlay, &AShooterGameMode_BeginPlay_original);

    AsaApi::GetHooks().SetHook("AShooterPlayerController.ServerSendChatMessage_Impl()",
        Hook_AShooterPlayerController_ServerSendChatMessage_Impl,
        &AShooterPlayerController_ServerSendChatMessage_Impl_original);

    AsaApi::GetHooks().SetHook("AShooterGameMode.HandleNewPlayer_Implementation()",
        Hook_AShooterGameMode_HandleNewPlayer_Implementation,
        &AShooterGameMode_HandleNewPlayer_Implementation_original);

    AsaApi::GetHooks().SetHook("AShooterGameMode.Logout()",
        Hook_AShooterGameMode_Logout,
        &AShooterGameMode_Logout_original);

    rpframework::asa::RegisterWorldHooks();

    if (AsaApi::GetApiUtils().GetStatus() == AsaApi::ServerStatus::Ready)
    {
        OnServerReady();
    }

    // Commandes chat communes : le routeur appelle les APIs métier et reste
    // réutilisable depuis console/RCON dans une phase ultérieure.
    const auto registerCommand = [](const char* commandName, const FString& name)
    {
        AsaApi::GetCommands().AddChatCommand(name,
            [commandName](AShooterPlayerController* pc, FString* message, int, int)
            {
                if (pc == nullptr || message == nullptr) return;
                auto tokens = rpframework::quest::TokenizeCommand(
                    rpframework::asa::FStringToUtf8(*message));
                const auto player = rpframework::asa::ExtractPlayerId(pc);
                if (player == 0) return;
                if (!tokens.empty() && tokens.front().front() == '/')
                    tokens.front().erase(tokens.front().begin());
                if (tokens.empty() || tokens.front() != commandName)
                    tokens.insert(tokens.begin(), commandName);
                const auto& args = tokens;
                const auto level = rpframework::security::Permissions::GetPlayerLevel(player);
                bool success = false;
                std::string reply;
                if (std::string(commandName) == "marchand")
                {
                    const auto merchant = rpframework::economy::HandleMerchantCommand(player, args);
                    success = merchant.success;
                    reply = merchant.message;
                }
                else
                {
                    const auto result = rpframework::quest::HandleCommand(player, args, level);
                    success = result.success;
                    reply = result.message;
                }
                AsaApi::GetApiUtils().SendServerMessage(pc,
                    success ? FColorList::Green : FColorList::Red,
                    success ? "[RPFramework] %s" : "[RPFramework] Erreur: %s",
                    reply.c_str());
                rpframework::core::LogInfo("Commande chat {}: {}",
                    args.front(), reply);
            });
    };
    registerCommand("quest", FString(L"quest"));
    registerCommand("quetes", FString(L"quetes"));
    registerCommand("race", FString(L"race"));
    registerCommand("profession", FString(L"profession"));
    registerCommand("metier", FString(L"metier"));
    registerCommand("class", FString(L"class"));
    registerCommand("classe", FString(L"classe"));
    registerCommand("faction", FString(L"faction"));
    registerCommand("reputation", FString(L"reputation"));
    registerCommand("economy", FString(L"economy"));
    registerCommand("framework", FString(L"framework"));
    registerCommand("mod", FString(L"mod"));
    registerCommand("config", FString(L"config"));
    registerCommand("journal", FString(L"journal"));
    registerCommand("marchand", FString(L"marchand"));

    // Canal Mod → Plugin (GDD §48) : commande console, aucun offset ARK.
    rpframework::mod::Initialize();
    AsaApi::GetCommands().AddConsoleCommand(FString(L"rpf"),
        [](APlayerController* controller, FString* cmd, bool /*writeToLog*/)
        {
            if (cmd == nullptr) return;
            const std::string line = rpframework::asa::FStringToUtf8(*cmd);
            auto* pc = static_cast<AShooterPlayerController*>(controller);
            const auto player = (pc != nullptr)
                ? rpframework::asa::ExtractPlayerId(pc) : 0;
            const auto result = rpframework::mod::Execute(player, line);
            if (pc != nullptr && !result.message.empty())
            {
                AsaApi::GetApiUtils().SendServerMessage(pc,
                    result.success ? FColorList::Green : FColorList::Red,
                    result.success ? "[RPFramework] %s" : "[RPFramework] Erreur: %s",
                    result.message.c_str());
            }
            rpframework::core::LogInfo("Commande console rpf: {}", result.message);
        });
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
    rpframework::asa::UnregisterWorldHooks();

    AsaApi::GetHooks().DisableHook("AShooterGameMode.BeginPlay()",
        Hook_AShooterGameMode_BeginPlay);

    AsaApi::GetHooks().DisableHook("AShooterPlayerController.ServerSendChatMessage_Impl()",
        Hook_AShooterPlayerController_ServerSendChatMessage_Impl);

    AsaApi::GetHooks().DisableHook("AShooterGameMode.HandleNewPlayer_Implementation()",
        Hook_AShooterGameMode_HandleNewPlayer_Implementation);

    AsaApi::GetHooks().DisableHook("AShooterGameMode.Logout()",
        Hook_AShooterGameMode_Logout);

    AsaApi::GetCommands().RemoveChatCommand(FString(L"quest"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"quetes"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"race"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"profession"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"metier"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"class"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"classe"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"faction"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"reputation"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"economy"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"framework"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"mod"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"config"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"journal"));
    AsaApi::GetCommands().RemoveChatCommand(FString(L"marchand"));
    AsaApi::GetCommands().RemoveConsoleCommand(FString(L"rpf"));
    rpframework::mod::Shutdown();

    rpframework::core::PluginContext::Shutdown();

    rpframework::core::LogInfo("{} v{} déchargé.",
        std::string(rpframework::core::kFrameworkName),
        rpframework::core::GetVersionString());
}

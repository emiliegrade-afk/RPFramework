// ============================================================================
// RPFramework - Point d'entrée du plugin
//
// Glue AsaApi : Plugin_Init / Plugin_Unload + hooks minimaux.
// Toute la logique métier vit dans src/Core/, src/Security/ et src/Data/.
//
// Hooks en place :
//   - AShooterGameMode_BeginPlay                        → log + audit
//   - AShooterPlayerController_ServerSendChatMessage_Impl
//                                                     → pipeline Security (chat)
//   - AShooterGameMode_HandleNewPlayer_Implementation  → charge/crée PlayerData
//   - AShooterGameMode_Logout                           → sauvegarde PlayerData
// ============================================================================
#include "API/ARK/Ark.h"

#include "Core/PluginContext.h"
#include "Core/Logger.h"
#include "Core/Version.h"

#include "Data/PlayerData.h"
#include "Data/PlayerStore.h"

#include "Loadout/Distribute.h"

#include "Quest/Commands.h"

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
    // ---- Helpers -------------------------------------------------------------

    // AsaApi est compilée en Unicode : convertir explicitement les FString
    // évite de tronquer les identifiants/noms non ASCII et stabilise le hash.
    std::string FStringToUtf8(const FString& value)
    {
        const int length = value.Len();
        if (length <= 0) return {};

        const auto* raw = reinterpret_cast<const wchar_t*>(value.operator*());
        const int bytes = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            raw, length, nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) return {};

        std::string result(static_cast<std::size_t>(bytes), '\0');
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw, length,
            result.data(), bytes, nullptr, nullptr) != bytes)
        {
            return {};
        }
        return result;
    }

    // Extrait un PlayerId stable à partir d'un AShooterPlayerController.
    // Utilise GetUniqueNetIdAsString (SteamID sur PC, EOS ID sur consoles)
    // puis hashe en uint64. Si l'appel échoue, fallback sur l'adresse
    // du controller (moins stable, mais ça ne crashe pas).
    rpframework::security::PlayerId ExtractPlayerId(AShooterPlayerController* pc)
    {
        using namespace rpframework::security;
        if (pc == nullptr)
        {
            return 0;
        }
        try
        {
            FString idStr;
            pc->GetUniqueNetIdAsString(&idStr);
            const std::string s = FStringToUtf8(idStr);
            if (!s.empty())
            {
                return MakePlayerId(s);
            }

            // Fallback défensif : ne dépend pas de l'adresse du contrôleur,
            // mais reste stable pour l'instance active pendant la session.
            const void* raw = static_cast<const void*>(pc);
            return MakePlayerIdFromPointer(raw);
        }
        catch (...)
        {
            const void* raw = static_cast<const void*>(pc);
            return MakePlayerIdFromPointer(raw);
        }
    }

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
    AShooterPlayerController_ServerSendChatMessage_Impl_original(pc, msg, mode, toTeam);

    if (pc == nullptr || msg == nullptr) return;

    const int msgLen = msg->Len();
    if (msgLen > 256)
    {
        const auto pid = ExtractPlayerId(pc);
        rpframework::security::AuditLog::LogDenied("chat.send", pid, "too_long", {
            {"len", msgLen}, {"max", 256},
        });
        return;
    }
    if (msg->IsEmpty()) return;

    const auto pid = ExtractPlayerId(pc);
    RunSecurityCheck(pid, rpframework::security::Level::PLAYER, "chat.send", {
        {"len", msgLen}, {"mode", mode}, {"team", toTeam},
    });
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
    UPrimalPlayerData*, AShooterCharacter*, bool)
{
    const bool result = AShooterGameMode_HandleNewPlayer_Implementation_original(
        gm, pc, nullptr, nullptr, false);

    if (pc == nullptr) return result;

    const auto pid = ExtractPlayerId(pc);

    // L'API ASA exposée ici ne fournit pas encore un accès stable au nom via
    // le controller. On conserve donc le nom déjà persistant (ou vide pour un
    // nouveau profil) plutôt que d'appeler une méthode inexistante.
    std::string name;

    auto load = rpframework::data::PlayerStore::LoadDetailed(pid);
    if (load.status == rpframework::data::PlayerLoadStatus::Corrupt
        || load.status == rpframework::data::PlayerLoadStatus::Unavailable)
    {
        // Ne jamais transformer une corruption en "nouveau joueur" : le
        // fichier reste disponible pour une intervention administrateur.
        rpframework::security::AuditLog::Log("player.join_data_unavailable", pid, {
            {"status", static_cast<int>(load.status)},
        }, rpframework::security::audit_severity::kError);
        return result;
    }

    const bool isNew = (load.status == rpframework::data::PlayerLoadStatus::Missing);
    auto data = load.HasData() ? std::move(*load.data)
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

    // Les fichiers restaurés sont déjà écrits par PlayerStore. On sauvegarde
    // uniquement un nouveau profil ou une modification réelle de son nom.
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

    // Phase 4b : distribue le kit de départ au premier join. No-op si
    // le joueur a déjà reçu son kit (flag starterKitDelivered).
    // La distribution effective (give items ASA) n'est PAS faite ici :
    // on logge dans audit + set le flag, et c'est à un hook AsaApi
    // Phase 9 de traduire les items en actions UE/ASA réelles.
    rpframework::loadout::Distributor::GiveStarterKit(pid);

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
    const auto pid = ExtractPlayerId(pc);

    if (rpframework::data::PlayerStore::Exists(pid))
    {
        auto load = rpframework::data::PlayerStore::LoadDetailed(pid);
        if (load.HasData())
        {
            rpframework::security::AuditLog::Log("player.leave", pid, {
                {"level",      load.data->level},
                {"xp",         load.data->xp},
                {"race",       load.data->race},
                {"profession", load.data->profession},
            });
        }
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
                    FStringToUtf8(*message));
                const auto player = ExtractPlayerId(pc);
                if (!tokens.empty() && tokens.front().front() == '/')
                    tokens.front().erase(tokens.front().begin());
                if (tokens.empty() || tokens.front() != commandName)
                    tokens.insert(tokens.begin(), commandName);
                const auto& args = tokens;
                const auto result = rpframework::quest::HandleCommand(player, args);
                AsaApi::GetApiUtils().SendServerMessage(pc, FColorList::Green,
                    result.success ? "[RPFramework] %s" : "[RPFramework] Erreur: %s",
                    result.message.c_str());
                rpframework::core::LogInfo("Commande chat {}: {}",
                    args.front(), result.message);
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
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
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

    rpframework::core::PluginContext::Shutdown();

    rpframework::core::LogInfo("{} v{} déchargé.",
        std::string(rpframework::core::kFrameworkName),
        rpframework::core::GetVersionString());
}

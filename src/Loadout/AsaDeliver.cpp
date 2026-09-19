#include "Loadout/AsaDeliver.h"

#include "Asa/Identity.h"
#include "Core/Logger.h"
#include "Data/PlayerStore.h"
#include "Quest/Engine.h"
#include "Security/AuditLog.h"

#include "API/ARK/Ark.h"

#include <Windows.h>
#include <string>
#include <vector>

namespace rpframework::loadout
{
    namespace
    {
        FString Utf8ToFString(const std::string& value)
        {
            if (value.empty()) return FString();
            const int wide = ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                static_cast<int>(value.size()), nullptr, 0);
            if (wide <= 0) return FString();
            std::wstring buffer(static_cast<std::size_t>(wide), L'\0');
            if (::MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                static_cast<int>(value.size()), buffer.data(), wide) != wide)
            {
                return FString();
            }
            return FString(buffer.c_str());
        }

        std::string BlueprintOf(const Item& item)
        {
            if (item.extras.contains("blueprint") && item.extras["blueprint"].is_string())
            {
                return item.extras["blueprint"].get<std::string>();
            }
            if (item.id.find('/') != std::string::npos) return item.id;
            return {};
        }

        float ParseQuality(const nlohmann::json& value)
        {
            if (value.is_number()) return value.get<float>();
            if (value.is_string())
            {
                try { return std::stof(value.get<std::string>()); }
                catch (...) {}
            }
            return 0.0f;
        }

        float QualityOf(const Item& item)
        {
            if (item.extras.contains("quality"))
                return ParseQuality(item.extras["quality"]);
            if (!item.quality.empty())
            {
                try { return std::stof(item.quality); }
                catch (...) {}
            }
            return 0.0f;
        }

        bool GiveOne(AShooterPlayerController* pc, const Item& item)
        {
            const auto blueprint = BlueprintOf(item);
            if (blueprint.empty() || pc == nullptr) return false;

            FString path = Utf8ToFString(blueprint);
            if (path.IsEmpty()) return false;
            return pc->GiveItem(&path, item.quantity, QualityOf(item), false, false, 0.0f);
        }
    }

    void TryGiveItems(security::PlayerId player, const std::vector<Item>& items)
    {
        auto* pc = asa::FindController(player);
        if (pc == nullptr) return;

        int given = 0;
        for (const auto& item : items)
        {
            if (GiveOne(pc, item)) ++given;
        }
        if (given > 0)
        {
            security::AuditLog::Log("loadout.asa.given", player, {{"count", given}});
        }
    }

    void TryGivePendingQuestItems(security::PlayerId player)
    {
        auto* pc = asa::FindController(player);
        if (pc == nullptr) return;

        auto load = data::PlayerStore::LoadDetailed(player);
        if (!load.HasData()) return;

        for (const auto& [questId, progress] : load.data->quests)
        {
            for (const auto& pending : progress.pendingItemRewards)
            {
                Item item;
                item.id = pending.value("id", std::string{});
                item.quantity = pending.value("amount", 1);
                if (pending.contains("payload") && pending["payload"].is_object())
                {
                    item.extras = pending["payload"];
                }
                if (GiveOne(pc, item))
                {
                    quest::ConfirmItemReward(player, questId, item.id);
                }
            }
        }
    }

    void TryUnlockEngrams(security::PlayerId player, const std::vector<std::string>& blueprints)
    {
        auto* pc = asa::FindController(player);
        if (pc == nullptr || blueprints.empty()) return;

        auto* state = static_cast<AShooterPlayerState*>(pc->PlayerStateField().Get());
        if (state == nullptr) return;

        int unlocked = 0;
        for (const auto& blueprint : blueprints)
        {
            if (blueprint.empty()) continue;
            FString path = Utf8ToFString(blueprint);
            if (path.IsEmpty()) continue;
            UClass* cls = UVictoryCore::BPLoadClass(path);
            if (cls == nullptr) continue;
            try
            {
                state->ServerUnlockEngram(cls, true, true);
                ++unlocked;
            }
            catch (...)
            {
                core::LogWarn("Unlock engram echoue: {}", blueprint);
            }
        }
        if (unlocked > 0)
        {
            security::AuditLog::Log("loadout.asa.engrams", player, {{"count", unlocked}});
        }
    }
}

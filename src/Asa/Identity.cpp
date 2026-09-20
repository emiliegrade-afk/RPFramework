#include "Asa/Identity.h"

#include <Windows.h>
#include <string>

namespace rpframework::asa
{
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

    AShooterPlayerController* AsShooterPlayerController(AController* controller)
    {
        if (controller == nullptr) return nullptr;
        if (!controller->IsA(AShooterPlayerController::GetPrivateStaticClass()))
            return nullptr;
        return static_cast<AShooterPlayerController*>(controller);
    }

    security::PlayerId ExtractPlayerId(AShooterPlayerController* pc)
    {
        using namespace rpframework::security;
        if (pc == nullptr) return 0;
        try
        {
            FString idStr;
            pc->GetUniqueNetIdAsString(&idStr);
            const std::string s = FStringToUtf8(idStr);
            if (!s.empty()) return MakePlayerId(s);
        }
        catch (...)
        {
        }
        return 0;
    }

    AShooterPlayerController* FindController(security::PlayerId player)
    {
        if (player == 0) return nullptr;
        auto* world = AsaApi::GetApiUtils().GetWorld();
        if (world == nullptr) return nullptr;

        const auto& list = world->PlayerControllerListField();
        for (TWeakObjectPtr<APlayerController> entry : list)
        {
            auto* pc = AsShooterPlayerController(entry.Get());
            if (pc != nullptr && ExtractPlayerId(pc) == player)
            {
                return pc;
            }
        }
        return nullptr;
    }

    void Tell(security::PlayerId player, const std::string& message, bool ok)
    {
        if (message.empty()) return;
        auto* pc = FindController(player);
        if (pc == nullptr) return;
        AsaApi::GetApiUtils().SendServerMessage(pc,
            ok ? FColorList::Green : FColorList::Yellow,
            "[RPFramework] %s", message.c_str());
    }
}

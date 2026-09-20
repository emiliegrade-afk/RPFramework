#include "Asa/WorldHooks.h"
#include "Asa/Blueprints.h"
#include "Asa/Identity.h"

#include "Core/Logger.h"
#include "Crafting/Pipeline.h"
#include "Quest/Events.h"

#include "API/ARK/Ark.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
    std::string ToSlug(std::string value)
    {
        for (char& c : value)
        {
            if (c == ' ' || c == '\t') c = '_';
            else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return value;
    }

    std::string ResourceSlug(std::string name)
    {
        std::string snake;
        snake.reserve(name.size() + 4);
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            if (std::isupper(c) && i > 0
                && std::islower(static_cast<unsigned char>(name[i - 1])))
            {
                snake.push_back('_');
            }
            snake.push_back(static_cast<char>(c));
        }
        return ToSlug(std::move(snake));
    }

    std::string CharacterSlug(APrimalCharacter* character)
    {
        if (character == nullptr) return {};
        FString name;
        character->GetDescriptiveName(&name);
        auto slug = ToSlug(rpframework::asa::FStringToUtf8(name));
        if (!slug.empty()) return slug;

        auto* dino = static_cast<APrimalDinoCharacter*>(character);
        if (dino != nullptr)
        {
            const auto tag = dino->DinoNameTagField();
            FString tagStr;
            tag.ToString(tagStr);
            slug = ToSlug(rpframework::asa::FStringToUtf8(tagStr));
        }
        return slug;
    }

    DECLARE_HOOK(APrimalDinoCharacter_Die, bool, APrimalDinoCharacter*,
                 float, FDamageEvent*, AController*, AActor*);

    bool Hook_APrimalDinoCharacter_Die(APrimalDinoCharacter* dino, float damage,
                                       FDamageEvent* event, AController* killer,
                                       AActor* causer)
    {
        const bool dead = APrimalDinoCharacter_Die_original(dino, damage, event, killer, causer);
        if (!dead || killer == nullptr) return dead;

        auto* pc = static_cast<AShooterPlayerController*>(killer);
        const auto pid = rpframework::asa::ExtractPlayerId(pc);
        if (pid == 0) return dead;

        const auto slug = CharacterSlug(dino);
        const auto blueprint = rpframework::asa::BlueprintPathOf(dino);
        if (!slug.empty() || !blueprint.empty())
        {
            std::vector<rpframework::quest::EventNotice> notices;
            rpframework::quest::ReportGameplay(pid, "kill", {blueprint, slug}, 1, &notices);
            for (const auto& notice : notices)
                rpframework::asa::Tell(pid, notice.message, notice.completed);
        }
        return dead;
    }

    DECLARE_HOOK(APrimalDinoCharacter_TameDino, void, APrimalDinoCharacter*,
                 AShooterPlayerController*, bool, int, bool, bool, bool);

    void Hook_APrimalDinoCharacter_TameDino(APrimalDinoCharacter* dino,
                                            AShooterPlayerController* pc,
                                            bool ignoreLimit, int team,
                                            bool preventName, bool skipLevels,
                                            bool suppress)
    {
        APrimalDinoCharacter_TameDino_original(dino, pc, ignoreLimit, team,
            preventName, skipLevels, suppress);
        const auto pid = rpframework::asa::ExtractPlayerId(pc);
        if (pid == 0) return;
        const auto slug = CharacterSlug(dino);
        const auto blueprint = rpframework::asa::BlueprintPathOf(dino);
        if (!slug.empty() || !blueprint.empty())
        {
            std::vector<rpframework::quest::EventNotice> notices;
            rpframework::quest::ReportGameplay(pid, "tame", {blueprint, slug}, 1, &notices);
            for (const auto& notice : notices)
                rpframework::asa::Tell(pid, notice.message, notice.completed);
        }
    }

    DECLARE_HOOK(AShooterPlayerController_ServerCraftItem_Implementation, void,
                 AShooterPlayerController*, UPrimalInventoryComponent*, FItemNetID);

    std::string ItemSlug(UPrimalItem* item, AShooterPlayerController* pc)
    {
        if (item == nullptr) return {};
        FString name;
        item->GetItemName(&name, false, true, pc);
        auto slug = ToSlug(rpframework::asa::FStringToUtf8(name));
        if (!slug.empty()) return slug;
        return ToSlug(rpframework::asa::FStringToUtf8(item->DescriptiveNameBaseField()));
    }

    void Hook_AShooterPlayerController_ServerCraftItem_Implementation(
        AShooterPlayerController* pc, UPrimalInventoryComponent* inventory, FItemNetID itemId)
    {
        const auto pid = rpframework::asa::ExtractPlayerId(pc);

        std::string slug;
        std::string blueprint;
        if (inventory != nullptr)
        {
            int index = 0;
            if (auto* item = inventory->FindItem(&itemId, true, true, &index))
            {
                slug = ItemSlug(item, pc);
                blueprint = rpframework::asa::BlueprintPathOf(item);
            }
        }

        if (pid != 0 && !rpframework::crafting::AllowCraft(pid, blueprint))
        {
            rpframework::asa::Tell(pid, "craft refuse : conditions non remplies", false);
            rpframework::crafting::OnItemCrafted(pid, blueprint);
            return;
        }

        AShooterPlayerController_ServerCraftItem_Implementation_original(pc, inventory, itemId);
        if (pid == 0) return;

        std::vector<rpframework::quest::EventNotice> notices;
        rpframework::quest::ReportGameplay(pid, "craft",
            {blueprint, slug.empty() ? "item" : slug}, 1, &notices);
        for (const auto& notice : notices)
            rpframework::asa::Tell(pid, notice.message, notice.completed);

        const auto craft = rpframework::crafting::OnItemCrafted(pid, blueprint);
        for (const auto& notice : craft.notices)
            rpframework::asa::Tell(pid, notice.message, notice.ok);
    }

    DECLARE_HOOK(AShooterPlayerController_HarvestedElement, void,
                 AShooterPlayerController*, FAttachedInstancedHarvestingElement*, bool, bool);

    void Hook_AShooterPlayerController_HarvestedElement(
        AShooterPlayerController* pc, FAttachedInstancedHarvestingElement* element,
        bool gaveResources, bool damaged)
    {
        (void)element;
        AShooterPlayerController_HarvestedElement_original(pc, element, gaveResources, damaged);
        if (!gaveResources) return;
        const auto pid = rpframework::asa::ExtractPlayerId(pc);
        if (pid == 0) return;

        std::string blueprint;
        std::string slug;
        TArray<AActor*, TSizedDefaultAllocator<32>> actors;
        TArray<UActorComponent*, TSizedDefaultAllocator<32>> components;
        TArray<int, TSizedDefaultAllocator<32>> indices;
        if (pc->GetAllAimedHarvestActors(1200.0f, &actors, &components, &indices)
            && actors.Num() > 0 && actors[0] != nullptr)
        {
            blueprint = rpframework::asa::BlueprintPathOf(actors[0]);
            auto name = blueprint;
            const auto slash = name.find_last_of('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            const auto dot = name.find('.');
            if (dot != std::string::npos) name = name.substr(0, dot);
            slug = ResourceSlug(name);
        }
        if (slug.empty()) slug = "harvest";

        std::vector<rpframework::quest::EventNotice> notices;
        rpframework::quest::ReportGameplay(pid, "collect",
            {blueprint, slug, "harvest"}, 1, &notices);
        for (const auto& notice : notices)
            rpframework::asa::Tell(pid, notice.message, notice.completed);
    }
}

namespace rpframework::asa
{
    void RegisterWorldHooks()
    {
        AsaApi::GetHooks().SetHook(
            "APrimalDinoCharacter.Die(float,FDamageEvent&,AController*,AActor*)",
            Hook_APrimalDinoCharacter_Die, &APrimalDinoCharacter_Die_original);
        AsaApi::GetHooks().SetHook(
            "APrimalDinoCharacter.TameDino(AShooterPlayerController*,bool,int,bool,bool,bool)",
            Hook_APrimalDinoCharacter_TameDino, &APrimalDinoCharacter_TameDino_original);
        AsaApi::GetHooks().SetHook(
            "AShooterPlayerController.ServerCraftItem_Implementation(UPrimalInventoryComponent*,FItemNetID)",
            Hook_AShooterPlayerController_ServerCraftItem_Implementation,
            &AShooterPlayerController_ServerCraftItem_Implementation_original);
        AsaApi::GetHooks().SetHook(
            "AShooterPlayerController.HarvestedElement(FAttachedInstancedHarvestingElement*,bool,bool)",
            Hook_AShooterPlayerController_HarvestedElement,
            &AShooterPlayerController_HarvestedElement_original);
        rpframework::core::LogInfo("Asa world hooks: kill / tame / craft / harvest.");
    }

    void UnregisterWorldHooks()
    {
        AsaApi::GetHooks().DisableHook(
            "APrimalDinoCharacter.Die(float,FDamageEvent&,AController*,AActor*)",
            Hook_APrimalDinoCharacter_Die);
        AsaApi::GetHooks().DisableHook(
            "APrimalDinoCharacter.TameDino(AShooterPlayerController*,bool,int,bool,bool,bool)",
            Hook_APrimalDinoCharacter_TameDino);
        AsaApi::GetHooks().DisableHook(
            "AShooterPlayerController.ServerCraftItem_Implementation(UPrimalInventoryComponent*,FItemNetID)",
            Hook_AShooterPlayerController_ServerCraftItem_Implementation);
        AsaApi::GetHooks().DisableHook(
            "AShooterPlayerController.HarvestedElement(FAttachedInstancedHarvestingElement*,bool,bool)",
            Hook_AShooterPlayerController_HarvestedElement);
    }
}

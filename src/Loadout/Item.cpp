// ============================================================================
// RPFramework - Loadout / Item - implémentation
// ============================================================================
#include "Loadout/Item.h"

#include "Core/Logger.h"

namespace rpframework::loadout
{
    nlohmann::json Item::ToJson() const
    {
        nlohmann::json j;
        j["id"]       = id;
        j["quantity"] = quantity;
        if (!quality.empty()) j["quality"] = quality;
        if (!extras.is_null() && !extras.empty()) j["extras"] = extras;
        return j;
    }

    Item Item::FromJson(const nlohmann::json& j)
    {
        Item item;
        if (!j.is_object()) return item;
        if (!j.contains("id") || !j["id"].is_string()) return item;

        item.id       = j["id"].get<std::string>();
        item.quantity = j.value("quantity", 1);
        if (item.quantity < 1) item.quantity = 1;
        item.quality  = j.value("quality",  std::string{});
        if (j.contains("extras") && j["extras"].is_object())
        {
            item.extras = j["extras"];
        }
        if (j.contains("blueprint") && j["blueprint"].is_string())
        {
            if (!item.extras.is_object()) item.extras = nlohmann::json::object();
            if (!item.extras.contains("blueprint"))
                item.extras["blueprint"] = j["blueprint"].get<std::string>();
        }
        return item;
    }

    std::vector<Item> ItemsFromJsonArray(const nlohmann::json& j)
    {
        std::vector<Item> out;
        if (!j.is_array()) return out;
        out.reserve(j.size());
        for (const auto& entry : j)
        {
            if (!entry.is_object()) continue;
            Item it = Item::FromJson(entry);
            if (it.id.empty()) continue;  // entrée invalide → skip silencieux
            out.push_back(std::move(it));
        }
        return out;
    }
}

// ============================================================================
// RPFramework - Crafting / Definitions - implémentation
// ============================================================================
#include "Crafting/Definitions.h"

#include "Asa/BlueprintPath.h"

#include <stdexcept>

namespace rpframework::crafting
{
    using rpframework::asa::NormalizeBlueprintPath;

    // -------------------------------------------------------------------------
    // ItemStack
    // -------------------------------------------------------------------------

    nlohmann::json ItemStack::ToJson() const
    {
        nlohmann::json j;
        j["blueprint"] = blueprint;
        j["quantity"]  = quantity;
        return j;
    }

    ItemStack ItemStack::FromJson(const nlohmann::json& j, const std::string& context)
    {
        if (!j.is_object())
        {
            throw std::runtime_error(context + " : payload n'est pas un objet");
        }

        ItemStack s;
        s.blueprint = NormalizeBlueprintPath(j.value("blueprint", std::string{}));
        s.quantity  = j.value("quantity", 0);
        if (s.blueprint.empty())
        {
            throw std::runtime_error(context + " : blueprint vide");
        }
        if (s.quantity <= 0)
        {
            throw std::runtime_error(context + " : quantity doit etre > 0");
        }
        return s;
    }

    // -------------------------------------------------------------------------
    // Station
    // -------------------------------------------------------------------------

    nlohmann::json Station::ToJson() const
    {
        nlohmann::json j;
        j["id"]          = id;
        j["name"]        = name;
        j["blueprint"]   = blueprint;
        j["professions"] = professions;
        return j;
    }

    Station Station::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("station '" + idIn + "' : payload racine n'est pas un objet");
        }
        if (idIn.empty())
        {
            throw std::runtime_error("station : id vide");
        }

        Station s;
        s.id        = idIn;
        s.name      = j.value("name", idIn);
        s.blueprint = NormalizeBlueprintPath(j.value("blueprint", std::string{}));
        if (j.contains("professions") && j["professions"].is_array())
        {
            for (const auto& p : j["professions"])
            {
                if (p.is_string())
                    s.professions.push_back(p.get<std::string>());
            }
        }
        return s;
    }

    // -------------------------------------------------------------------------
    // Recipe
    // -------------------------------------------------------------------------

    nlohmann::json Recipe::ToJson() const
    {
        nlohmann::json j;
        j["id"]             = id;
        j["name"]           = name;
        j["station"]        = station;
        j["profession"]     = profession;
        j["min_level"]      = minLevel;
        j["required_skill"] = requiredSkill;
        j["craft_time_sec"] = craftTimeSec;
        j["xp"]             = xp;
        j["output"]         = output.ToJson();
        nlohmann::json ings = nlohmann::json::array();
        for (const auto& ing : ingredients)
            ings.push_back(ing.ToJson());
        j["ingredients"] = ings;
        return j;
    }

    Recipe Recipe::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("recette '" + idIn + "' : payload racine n'est pas un objet");
        }
        if (idIn.empty())
        {
            throw std::runtime_error("recette : id vide");
        }

        Recipe r;
        r.id            = idIn;
        r.name          = j.value("name", idIn);
        r.station       = j.value("station", std::string{});
        r.profession    = j.value("profession", std::string{});
        r.minLevel      = j.value("min_level", 1);
        r.requiredSkill = j.value("required_skill", std::string{});
        r.craftTimeSec  = j.value("craft_time_sec", 0);
        r.xp            = j.value("xp", 0);

        if (r.profession.empty())
        {
            throw std::runtime_error("recette '" + idIn + "' : profession vide");
        }
        if (r.minLevel < 1)
        {
            throw std::runtime_error("recette '" + idIn + "' : min_level doit etre >= 1");
        }
        if (r.xp < 0)
        {
            throw std::runtime_error("recette '" + idIn + "' : xp doit etre >= 0");
        }
        if (r.craftTimeSec < 0)
        {
            throw std::runtime_error("recette '" + idIn + "' : craft_time_sec doit etre >= 0");
        }

        if (!j.contains("output"))
        {
            throw std::runtime_error("recette '" + idIn + "' : output manquant");
        }
        r.output = ItemStack::FromJson(j["output"], "recette '" + idIn + "' / output");

        if (j.contains("ingredients"))
        {
            if (!j["ingredients"].is_array())
            {
                throw std::runtime_error("recette '" + idIn + "' : ingredients n'est pas un tableau");
            }
            int index = 0;
            for (const auto& ing : j["ingredients"])
            {
                r.ingredients.push_back(ItemStack::FromJson(
                    ing, "recette '" + idIn + "' / ingredient[" + std::to_string(index) + "]"));
                ++index;
            }
        }

        return r;
    }
}

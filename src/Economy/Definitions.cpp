// ============================================================================
// RPFramework - Economy / Definitions - implémentation
// ============================================================================
#include "Economy/Definitions.h"

#include <stdexcept>

namespace rpframework::economy
{
    nlohmann::json Currency::ToJson() const
    {
        nlohmann::json j;
        j["id"]           = id;
        j["name"]         = name;
        j["symbol"]       = symbol;
        j["max_balance"]  = maxBalance;
        j["transferable"] = transferable;
        return j;
    }

    Currency Currency::FromJson(const std::string& idIn, const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            throw std::runtime_error("currency '" + idIn + "' : payload racine n'est pas un objet");
        }
        Currency c;
        c.id            = idIn;
        c.name          = j.value("name", idIn);
        c.symbol        = j.value("symbol", std::string{});
        c.maxBalance    = j.value("max_balance", static_cast<int64_t>(0));
        c.transferable  = j.value("transferable", true);
        if (c.maxBalance < 0)
        {
            throw std::runtime_error("currency '" + idIn + "' : max_balance negatif");
        }
        if (c.id.empty())
        {
            throw std::runtime_error("currency : id vide");
        }
        return c;
    }
}

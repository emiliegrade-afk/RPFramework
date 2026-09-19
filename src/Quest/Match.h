// ============================================================================
// RPFramework - Correspondance souple objectif <-> événement monde
// ============================================================================
#pragma once

#include "Quest/Definitions.h"

#include <cctype>
#include <string>
#include <string_view>

namespace rpframework::quest
{
    inline std::string NormalizeEntity(std::string_view value)
    {
        std::string out;
        out.reserve(value.size());
        for (unsigned char c : value)
        {
            if (c == ' ' || c == '\t' || c == '-') out.push_back('_');
            else out.push_back(static_cast<char>(std::tolower(c)));
        }
        return out;
    }

    inline bool EntityMatches(std::string_view reported, std::string_view configured)
    {
        const auto got = NormalizeEntity(reported);
        const auto want = NormalizeEntity(configured);
        if (want.empty()) return false;
        if (want == "*" || want == "any") return true;
        if (got == want) return true;

        if (want.size() >= 3 && got.size() > want.size())
        {
            if (got.compare(got.size() - want.size(), want.size(), want) == 0)
            {
                const auto before = got[got.size() - want.size() - 1];
                if (before == '_') return true;
                if (std::isalpha(static_cast<unsigned char>(before))) return true;
            }
        }

        std::size_t start = 0;
        while (start < got.size())
        {
            const auto pos = got.find('_', start);
            const auto token = got.substr(start,
                pos == std::string::npos ? std::string::npos : pos - start);
            if (token == want) return true;
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
        return false;
    }

    inline bool ObjectiveMatches(std::string_view type, std::string_view entity,
                                 const Objective& objective)
    {
        if (objective.type != type) return false;
        if (EntityMatches(entity, objective.entity)) return true;
        const auto want = NormalizeEntity(objective.entity);
        if (type == "collect" && want == "harvest") return true;
        if (type == "craft" && want == "item") return true;
        return false;
    }
}

// ============================================================================
// RPFramework - Core / Version
// Implémentation de GetVersionString (le reste est constexpr dans le header).
// ============================================================================
#include "Core/Version.h"

namespace rpframework::core
{
    std::string GetVersionString()
    {
        std::string out;
        out.reserve(16);
        out.append(std::to_string(kVersionMajor));
        out.push_back('.');
        out.append(std::to_string(kVersionMinor));
        out.push_back('.');
        out.append(std::to_string(kVersionPatch));
        if (!kVersionPreRelease.empty())
        {
            out.push_back('-');
            out.append(kVersionPreRelease.data(), kVersionPreRelease.size());
        }
        return out;
    }
}

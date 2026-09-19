// ============================================================================
// RPFramework - Normalisation de chemin blueprint (pur, sans ARK)
// Compilé dans le plugin ET dans les tests.
// ============================================================================
#include "Asa/Blueprints.h"

#include <cctype>
#include <string>
#include <string_view>

namespace rpframework::asa
{
    namespace
    {
        constexpr std::string_view kWhitespace = " \t\r\n";
        constexpr std::string_view kDefaultPrefix = "Default__";
        constexpr std::string_view kClassSuffix = "_C";

        std::string_view Trim(std::string_view value)
        {
            const auto first = value.find_first_not_of(kWhitespace);
            if (first == std::string_view::npos) return {};
            const auto last = value.find_last_not_of(kWhitespace);
            return value.substr(first, last - first + 1);
        }
    }

    std::string NormalizeBlueprintPath(std::string_view raw)
    {
        auto value = Trim(raw);
        if (value.empty()) return {};

        // Wrapper Unreal : Blueprint'/Game/X.X' (éventuellement BlueprintGeneratedClass'…').
        const auto firstQuote = value.find('\'');
        if (firstQuote != std::string_view::npos)
        {
            const auto lastQuote = value.find_last_of('\'');
            if (lastQuote > firstQuote)
                value = value.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            else
                value = value.substr(firstQuote + 1);
            value = Trim(value);
        }
        // GetFullName : "ClassName /Game/X.X" — on garde le dernier segment.
        else if (const auto space = value.find_last_of(kWhitespace);
                 space != std::string_view::npos)
        {
            value = Trim(value.substr(space + 1));
        }

        if (value.empty()) return {};
        std::string out(value);

        if (const auto def = out.find(kDefaultPrefix); def != std::string::npos)
            out.erase(def, kDefaultPrefix.size());

        // size() > 2 : ne pas réduire l'entrée dégénérée "_C" à une chaîne vide.
        if (out.size() > kClassSuffix.size()
            && out.compare(out.size() - kClassSuffix.size(),
                           kClassSuffix.size(), kClassSuffix) == 0)
        {
            out.resize(out.size() - kClassSuffix.size());
        }

        return std::string(Trim(out));
    }

    // Forme de comparaison (minuscules). Déclarée dans BlueprintPath.h ;
    // conservée pour les tests déjà présents dans le worktree.
    std::string BlueprintKey(std::string_view raw)
    {
        auto out = NormalizeBlueprintPath(raw);
        for (char& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }
}

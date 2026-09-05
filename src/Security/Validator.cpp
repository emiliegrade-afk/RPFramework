// ============================================================================
// RPFramework - Security / Validator - implémentation
// ============================================================================
#include "Security/Validator.h"

namespace rpframework::security
{
    // -------------------------------------------------------------------------
    // Helpers privés : split d'un chemin-point en segments.
    // -------------------------------------------------------------------------
    namespace
    {
        std::vector<std::string> SplitDottedPath(std::string_view path)
        {
            std::vector<std::string> out;
            std::string current;
            for (char c : path)
            {
                if (c == '.')
                {
                    if (!current.empty())
                    {
                        out.push_back(std::move(current));
                        current.clear();
                    }
                }
                else
                {
                    current.push_back(c);
                }
            }
            if (!current.empty())
            {
                out.push_back(std::move(current));
            }
            return out;
        }

        const nlohmann::json* ResolvePath(const nlohmann::json& root,
                                          std::string_view dottedPath)
        {
            const nlohmann::json* cur = &root;
            for (const auto& seg : SplitDottedPath(dottedPath))
            {
                if (cur == nullptr || !cur->is_object())
                {
                    return nullptr;
                }
                auto it = cur->find(seg);
                if (it == cur->end())
                {
                    return nullptr;
                }
                cur = &(*it);
            }
            return cur;
        }
    }

    // -------------------------------------------------------------------------
    // Validateurs.
    // -------------------------------------------------------------------------

    ValidationResult Validator::NotEmpty(std::string_view value,
                                          std::string_view fieldName)
    {
        if (value.empty())
        {
            return ValidationResult::Fail(
                "empty_field",
                "champ vide",
                { {"field", std::string(fieldName)} });
        }
        return ValidationResult::Ok();
    }

    ValidationResult Validator::MaxLength(std::string_view value,
                                          std::size_t max,
                                          std::string_view fieldName)
    {
        if (value.size() > max)
        {
            return ValidationResult::Fail(
                "max_length",
                "champ trop long",
                { {"field", std::string(fieldName)},
                  {"actual", value.size()},
                  {"max", max} });
        }
        return ValidationResult::Ok();
    }

    ValidationResult Validator::InRange(int value, int min, int max,
                                         std::string_view fieldName)
    {
        if (value < min || value > max)
        {
            return ValidationResult::Fail(
                "out_of_range",
                "valeur hors plage",
                { {"field", std::string(fieldName)},
                  {"actual", value},
                  {"min", min},
                  {"max", max} });
        }
        return ValidationResult::Ok();
    }

    ValidationResult Validator::NonNegative(int value, std::string_view fieldName)
    {
        if (value < 0)
        {
            return ValidationResult::Fail(
                "negative_value",
                "valeur negative interdite",
                { {"field", std::string(fieldName)},
                  {"actual", value} });
        }
        return ValidationResult::Ok();
    }

    ValidationResult Validator::IsNonEmptyObject(const nlohmann::json& j,
                                                  std::string_view fieldName)
    {
        if (!j.is_object() || j.empty())
        {
            return ValidationResult::Fail(
                "not_object",
                "objet JSON vide ou absent",
                { {"field", std::string(fieldName)} });
        }
        return ValidationResult::Ok();
    }

    ValidationResult Validator::PathExists(const nlohmann::json& j,
                                            std::string_view dottedPath)
    {
        if (ResolvePath(j, dottedPath) == nullptr)
        {
            return ValidationResult::Fail(
                "missing_path",
                "chemin absent dans la config",
                { {"path", std::string(dottedPath)} });
        }
        return ValidationResult::Ok();
    }

    ValidationResult Validator::OneOf(std::string_view value,
                                       std::initializer_list<std::string_view> allowed,
                                       std::string_view fieldName)
    {
        for (auto v : allowed)
        {
            if (value == v)
            {
                return ValidationResult::Ok();
            }
        }
        return ValidationResult::Fail(
            "not_in_set",
            "valeur hors ensemble autorise",
            { {"field", std::string(fieldName)},
              {"actual", std::string(value)} });
    }

    ValidationResult Validator::All(std::initializer_list<ValidationResult> results)
    {
        for (const auto& r : results)
        {
            if (!r.valid)
            {
                return r;
            }
        }
        return ValidationResult::Ok();
    }
}

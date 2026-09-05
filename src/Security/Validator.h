// ============================================================================
// RPFramework - Security / Validator
//
// Validation générique pour actions sensibles (GDD §16, §17).
// Pas de logique métier ici : juste des briques de validation
// composables. Chaque module (Quest, Faction, Economy...) construit son
// pipeline de validation par-dessus.
//
// Pattern : chaque validateur renvoie un ValidationResult.
// On peut chaîner avec `All(...)` pour combiner.
// ============================================================================
#pragma once

#include "json.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace rpframework::security
{
    // -------------------------------------------------------------------------
    // Résultat de validation.
    // -------------------------------------------------------------------------
    struct ValidationResult
    {
        bool            valid = true;
        std::string     errorCode;     // ex: "missing_field", "out_of_range"
        std::string     errorMessage;  // ex: "champ 'race' manquant"
        nlohmann::json  errorContext;  // contexte sérialisable pour audit/debug

        static ValidationResult Ok()
        {
            return {};
        }

        static ValidationResult Fail(std::string code,
                                     std::string message,
                                     nlohmann::json context = {})
        {
            ValidationResult r;
            r.valid        = false;
            r.errorCode    = std::move(code);
            r.errorMessage = std::move(message);
            r.errorContext = std::move(context);
            return r;
        }
    };

    // -------------------------------------------------------------------------
    // Validateurs prêts à l'emploi.
    // -------------------------------------------------------------------------
    class Validator
    {
    public:
        // String non vide.
        static ValidationResult NotEmpty(std::string_view value,
                                          std::string_view fieldName);

        // Longueur max d'une string.
        static ValidationResult MaxLength(std::string_view value,
                                          std::size_t max,
                                          std::string_view fieldName);

        // Entier dans une plage inclusive.
        static ValidationResult InRange(int value, int min, int max,
                                         std::string_view fieldName);

        // Nombre >= 0.
        static ValidationResult NonNegative(int value, std::string_view fieldName);

        // JSON est un objet non vide.
        static ValidationResult IsNonEmptyObject(const nlohmann::json& j,
                                                  std::string_view fieldName);

        // Un chemin-point existe dans le JSON.
        static ValidationResult PathExists(const nlohmann::json& j,
                                            std::string_view dottedPath);

        // Une valeur fait partie d'un ensemble autorisé.
        static ValidationResult OneOf(std::string_view value,
                                       std::initializer_list<std::string_view> allowed,
                                       std::string_view fieldName);

        // Combine plusieurs validations : toutes doivent passer.
        // Renvoie le premier échec rencontré (log-friendly).
        static ValidationResult All(std::initializer_list<ValidationResult> results);
    };
}

// ============================================================================
// RPFramework - Micro-framework de test (header-only)
//
// Extrait de TestMain.cpp pour que chaque chantier puisse vivre dans son
// propre fichier de test sans conflit de merge. Les compteurs et le registre
// sont des entités `inline` : une seule instance est partagée par toutes les
// unités de compilation.
//
// Usage dans un nouveau fichier `tests/Test_MonChantier.cpp` :
//
//     #include "TestHarness.h"
//     #include "MonModule/MonHeader.h"
//
//     TEST(MonChantier_CasNominal)
//     {
//         EXPECT(1 + 1 == 2);
//     }
//
// Puis ajouter le fichier à `tests/RPFramework.Tests.vcxproj`. Le `main()`
// reste dans TestMain.cpp et exécute tous les tests enregistrés.
// ============================================================================
#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace test
{
    inline int g_passed = 0;
    inline int g_failed = 0;
    inline std::vector<std::string> g_failures;

    inline void Record(bool cond, const char* expr, const char* file, int line)
    {
        if (cond)
        {
            ++g_passed;
        }
        else
        {
            ++g_failed;
            std::ostringstream oss;
            oss << file << ":" << line << " - EXPECT(" << expr << ")";
            g_failures.push_back(oss.str());
        }
    }

    struct Test
    {
        std::string name;
        void (*fn)();
    };

    // Fonction inline : le static local est unique pour tout le binaire, donc
    // les Registrar de plusieurs .cpp alimentent bien le même registre.
    inline std::vector<Test>& Tests()
    {
        static std::vector<Test> t;
        return t;
    }

    struct Registrar
    {
        Registrar(const char* n, void (*f)())
        {
            Tests().push_back({n, f});
        }
    };
}

#define EXPECT(cond) ::test::Record((cond), #cond, __FILE__, __LINE__)
#define TEST(name)                                                     \
    static void name();                                                \
    static ::test::Registrar name##_reg(#name, &name);                 \
    static void name()

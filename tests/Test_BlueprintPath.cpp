// ============================================================================
// RPFramework - Tests Asa/BlueprintPath
//
// Ce fichier est volontairement livré AVEC la fonction : c'est la référence
// commune de tous les chantiers qui manipulent des clés d'items (Crafting,
// Effects, Merchant, hooks). Toute divergence de normalisation ailleurs dans
// le repo est un bug.
// ============================================================================
#include "TestHarness.h"

#include "Asa/BlueprintPath.h"

using rpframework::asa::BlueprintKey;
using rpframework::asa::NormalizeBlueprintPath;

namespace
{
    // Forme canonique de référence, telle qu'écrite dans configs/config.json.
    const std::string kSword =
        "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword"
        ".PrimalItem_WeaponSword";
}

TEST(BlueprintPath_AlreadyCanonicalIsUnchanged)
{
    EXPECT(NormalizeBlueprintPath(kSword) == kSword);
}

TEST(BlueprintPath_StripsBlueprintWrapper)
{
    EXPECT(NormalizeBlueprintPath("Blueprint'" + kSword + "'") == kSword);
    EXPECT(NormalizeBlueprintPath("Blueprint'" + kSword + "_C'") == kSword);
}

TEST(BlueprintPath_StripsTypePrefixAndDefaultMarker)
{
    EXPECT(NormalizeBlueprintPath(
        "BlueprintGeneratedClass /Game/PrimalEarth/CoreBlueprints/Weapons/"
        "PrimalItem_WeaponSword.Default__PrimalItem_WeaponSword_C") == kSword);

    EXPECT(NormalizeBlueprintPath(
        "PrimalItem_WeaponSword_C /Game/PrimalEarth/CoreBlueprints/Weapons/"
        "PrimalItem_WeaponSword.Default__PrimalItem_WeaponSword_C") == kSword);
}

TEST(BlueprintPath_StripsClassSuffix)
{
    EXPECT(NormalizeBlueprintPath(kSword + "_C") == kSword);
}

TEST(BlueprintPath_TrimsSurroundingWhitespace)
{
    EXPECT(NormalizeBlueprintPath("  " + kSword + "\t\r\n") == kSword);
}

TEST(BlueprintPath_IsIdempotent)
{
    const auto once = NormalizeBlueprintPath(
        "Blueprint'/Game/X/Y.Default__Y_C'");
    EXPECT(once == "/Game/X/Y.Y");
    EXPECT(NormalizeBlueprintPath(once) == once);
    EXPECT(NormalizeBlueprintPath(NormalizeBlueprintPath(once)) == once);
}

TEST(BlueprintPath_HandlesEmptyAndDegenerateInput)
{
    EXPECT(NormalizeBlueprintPath("").empty());
    EXPECT(NormalizeBlueprintPath("   ").empty());
    EXPECT(NormalizeBlueprintPath("_C").empty() == false);   // pas de troncature sauvage
    EXPECT(NormalizeBlueprintPath("/Game/X/Y") == "/Game/X/Y");  // sans point : toléré
}

TEST(BlueprintPath_KeyIsLowercasedButPathIsNot)
{
    // La casse est PRÉSERVÉE par la forme canonique (BPLoadClass en dépend)
    // et APLATIE par la clé de comparaison.
    EXPECT(NormalizeBlueprintPath(kSword) == kSword);
    EXPECT(BlueprintKey(kSword) != kSword);

    // Deux écritures de casse différente doivent donner la MÊME clé : c'est
    // ce qui protège d'une faute de casse dans config.json.
    EXPECT(BlueprintKey("/Game/X/Y.Y") == BlueprintKey("/game/x/y.y"));
    EXPECT(BlueprintKey("Blueprint'/GAME/X/Y.Default__Y_C'")
           == BlueprintKey("/game/x/y.y"));
}

TEST(BlueprintPath_KeyIsIdempotentToo)
{
    const auto key = BlueprintKey("Blueprint'" + kSword + "_C'");
    EXPECT(BlueprintKey(key) == key);
    EXPECT(key.find("default__") == std::string::npos);
    EXPECT(key.find('\'') == std::string::npos);
}
